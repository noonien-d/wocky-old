/*
 * wocky-sm.c - Source for WockySM
 * @author Ferdinand Stehle <development@kondorgulasch.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * SECTION: wocky-sm
 * @title: WockySM
 * @short_description: support for stream management stanza acking
 *
 * Support for XEP-0198 stream management.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "wocky-sm.h"

#include "wocky-namespaces.h"
#include "wocky-stanza.h"

#include <stdio.h>

#define WOCKY_DEBUG_FLAG WOCKY_DEBUG_SM
#include "wocky-debug-internal.h"

G_DEFINE_TYPE (WockySM, wocky_sm, G_TYPE_OBJECT)

/* properties */
enum
{
  PROP_PORTER = 1,
};

/* private structure */
struct _WockySMPrivate
{
  WockyPorter *porter;

  gulong sm_r_cb;
  gulong sm_a_cb;

  gboolean dispose_has_run;

  uint count_sent;
  GQueue *stanzas;
};

static gboolean sm_a_cb (WockyPorter *porter, WockyStanza *stanza,
    gpointer data);
static gboolean sm_r_cb (WockyPorter *porter, WockyStanza *stanza,
    gpointer data);

static void
wocky_sm_init (WockySM *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, WOCKY_TYPE_SM,
      WockySMPrivate);
}

static void
wocky_sm_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  WockySM *self = WOCKY_SM (object);
  WockySMPrivate *priv = self->priv;

  switch (property_id)
    {
    case PROP_PORTER:
      priv->porter = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
wocky_sm_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  WockySM *self = WOCKY_SM (object);
  WockySMPrivate *priv = self->priv;

  switch (property_id)
    {
    case PROP_PORTER:
      g_value_set_object (value, priv->porter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
wocky_sm_constructed (GObject *object)
{
  WockySM *self = WOCKY_SM (object);
  WockySMPrivate *priv = self->priv;

  g_assert (priv->porter != NULL);

  g_warning ("Register SM R handler");
  priv->sm_r_cb = wocky_porter_register_handler_from_anyone (
      WOCKY_PORTER (priv->porter),
      WOCKY_STANZA_TYPE_SM_R, WOCKY_STANZA_SUB_TYPE_NONE,
      WOCKY_PORTER_HANDLER_PRIORITY_NORMAL, sm_r_cb, self, NULL);

  priv->sm_a_cb = wocky_porter_register_handler_from_anyone (
      WOCKY_PORTER (priv->porter),
      WOCKY_STANZA_TYPE_SM_A, WOCKY_STANZA_SUB_TYPE_NONE,
      WOCKY_PORTER_HANDLER_PRIORITY_NORMAL, sm_a_cb, self, NULL);

  //Session stanza has been sent after sm-establishment
  priv->count_sent = 1;
  priv->stanzas = g_queue_new ();
}

static void
wocky_sm_dispose (GObject *object)
{
  WockySM *self = WOCKY_SM (object);
  WockySMPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->sm_a_cb != 0)
    {
      wocky_porter_unregister_handler (WOCKY_PORTER (priv->porter),
          priv->sm_a_cb);
      priv->sm_a_cb = 0;
    }
  if (priv->sm_r_cb != 0)
    {
      wocky_porter_unregister_handler (WOCKY_PORTER (priv->porter),
          priv->sm_r_cb);
      priv->sm_r_cb = 0;
    }

  g_object_unref (priv->porter);
  priv->porter = NULL;

  if (G_OBJECT_CLASS (wocky_sm_parent_class)->dispose)
    G_OBJECT_CLASS (wocky_sm_parent_class)->dispose (object);
}

static void
wocky_sm_class_init (WockySMClass *wocky_sm_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (wocky_sm_class);
  GParamSpec *spec;

  g_type_class_add_private (wocky_sm_class,
      sizeof (WockySMPrivate));

  object_class->constructed = wocky_sm_constructed;
  object_class->set_property = wocky_sm_set_property;
  object_class->get_property = wocky_sm_get_property;
  object_class->dispose = wocky_sm_dispose;

  spec = g_param_spec_object ("porter", "Wocky C2S porter",
      "the wocky porter to set up sm acks on",
      WOCKY_TYPE_C2S_PORTER,
      G_PARAM_READWRITE |
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PORTER, spec);
}

WockySM *
wocky_sm_new (WockyC2SPorter *porter)
{
  g_return_val_if_fail (WOCKY_IS_C2S_PORTER (porter), NULL);

  return g_object_new (WOCKY_TYPE_SM,
      "porter", porter,
      NULL);
}

/** wocky_sm_send_a
 * @porter: WockyPorter object
 * @recv_count: Number of received stanzas to write in the ack
 *
 * Builds and sends an ack-stanza.
 */
void wocky_sm_send_a (WockyPorter* porter, uint recv_count)
{
  WockyStanza *stanza_a = wocky_stanza_new ("a", WOCKY_XMPP_NS_STREAM_MANAGEMENT);

  if (stanza_a != NULL)
  {
    WockyNode* node = wocky_stanza_get_top_node (stanza_a);
    char buffer[12];

    sprintf (buffer, "%d", recv_count);
    wocky_node_set_attribute (node, "h", buffer);

    DEBUG("Sending sm-ack h=%d", recv_count);

    wocky_porter_send (porter, stanza_a);
    g_object_unref (stanza_a);
  }
}
/** wocky_sm_send_r
 * @porter: WockyPorter object
 * @recv_count: Number of received stanzas to write in the ack
 *
 * Builds and sends an ack-stanza.
 */
void wocky_sm_send_r (WockyPorter* porter, uint count_sent)
{
  WockyStanza *stanza_r = wocky_stanza_new ("r", WOCKY_XMPP_NS_STREAM_MANAGEMENT);

  if (stanza_r != NULL)
  {
    DEBUG("Sending sm-request, sent_count = %d", count_sent);

    wocky_porter_send (porter, stanza_r);
    g_object_unref (stanza_r);
  }
}
/**
* sm_r_cb
* @porter: WockyPorter object
* @stanza: The originally received stanza
* @data: unused user data
*
* Callback function handling stream management <r />-stanzas by replying
* the number of received stanzas.
*/
static gboolean
sm_r_cb (WockyPorter *porter, WockyStanza *stanza, gpointer data)
{
  DEBUG("Got sm-request");
  wocky_sm_send_a (porter,  wocky_stanza_get_recv_count (stanza));

  return TRUE;
}

/**
* sm_a_cb
* @porter: WockyPorter object
* @stanza: The originally received stanza
* @data: unused user data
*
* Callback function handling stream management <a />-stanzas
*/
static gboolean
sm_a_cb (WockyPorter *porter, WockyStanza *stanza_a, gpointer data)
{
  WockySM *self = WOCKY_SM(data);
  WockySMPrivate *priv = self->priv;

  WockyNode* node = wocky_stanza_get_top_node (stanza_a);

  if (node != NULL)
    {
      const gchar *val_h = wocky_node_get_attribute (node, "h");
      if (val_h != NULL)
        {
          WockyStanza *stanza = g_queue_pop_head (priv->stanzas);

          if (stanza != NULL)
            {
              DEBUG("Got sm-ack h=%s, shouldbe=%d", val_h, wocky_stanza_get_recv_count(stanza));
              g_object_unref (stanza);
            }
          else
            {
              DEBUG("Got sm-ack h=%s, QUEUE IS EMPTY", val_h);
            }
        }
      else
        {
          g_warning ("Failed to get h-attribute");
        }
    }
  else
    {
      g_warning ("Failed to get_top_node");
    }

  return TRUE;
}

void wocky_sm_request_for_stanza (WockySM *self, WockyStanza *stanza)
{
  WockySMPrivate *priv = self->priv;

  priv->count_sent ++;

  wocky_stanza_set_recv_count (stanza, priv->count_sent);

  g_queue_push_tail (priv->stanzas, g_object_ref (stanza));

  wocky_sm_send_r (priv->porter, priv->count_sent);
}

gboolean wocky_sm_is_unacked_stanza (WockySM *self)
{
  DEBUG ("unacked stanza count: %d", g_queue_get_length (self->priv->stanzas));

  return (g_queue_get_length (self->priv->stanzas) > 0);
}

WockyStanza *
wocky_sm_pop_unacked_stanza (WockySM *self)
{
  WockySMPrivate *priv = self->priv;

  return WOCKY_STANZA(g_queue_pop_head (priv->stanzas));
}
