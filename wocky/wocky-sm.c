/*
 * wocky-sm.c - Source for WockySM
 * Copyright (C) 2010 Collabora Ltd.
 * @author Senko Rasic <senko.rasic@collabora.co.uk>
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
  WockyC2SPorter *porter;

  gulong sm_iq_cb;

  gboolean dispose_has_run;
};

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

  g_warning("Register SM R handler");
  priv->sm_iq_cb = wocky_porter_register_handler_from_anyone (
      WOCKY_PORTER (priv->porter),
      WOCKY_STANZA_TYPE_SM_R, WOCKY_STANZA_SUB_TYPE_NONE,
      WOCKY_PORTER_HANDLER_PRIORITY_NORMAL, sm_r_cb, self, NULL);
}

static void
wocky_sm_dispose (GObject *object)
{
  WockySM *self = WOCKY_SM (object);
  WockySMPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->sm_iq_cb != 0)
    {
      wocky_porter_unregister_handler (WOCKY_PORTER (priv->porter),
          priv->sm_iq_cb);
      priv->sm_iq_cb = 0;
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
  //BUILD A stanza
  WockyStanza *result = wocky_stanza_new("a", WOCKY_NS_STREAM_MANAGEMENT);
  WockyNode* node = wocky_stanza_get_top_node(result);

  guint count = wocky_stanza_get_recv_count(stanza);

  char buffer[12];

  sprintf(buffer, "%d", count);

  wocky_node_set_attribute (node, "h", buffer);

  //SEND stanza
  if (result != NULL)
  {
    wocky_porter_send (porter, result);
    g_object_unref (result);
  }

  return TRUE;
}



