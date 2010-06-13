/*
 * wocky-ping.c - Source for WockyPing
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
 * SECTION: wocky-ping
 * @title: WockyPing
 * @short_description: support for pings/keepalives
 *
 * Support for XEP-0199 pings.
 */

#include "wocky-ping.h"

#include "wocky-namespaces.h"
#include "wocky-stanza.h"

#define DEBUG_FLAG DEBUG_PING
#include "wocky-debug.h"

G_DEFINE_TYPE (WockyPing, wocky_ping, G_TYPE_OBJECT)

/* properties */
enum
{
  PROP_PORTER = 1,
  PROP_PING_INTERVAL,
};

/* private structure */
struct _WockyPingPrivate
{
  WockyPorter *porter;

  guint ping_interval;

  gulong porter_sending_id;
  gulong ping_iq_cb;
  gulong ping_timeout_id;

  gboolean dispose_has_run;
};

static void reset_ping_timeout (WockyPing *self);
static void porter_sending_cb (WockyPorter *porter, WockyPing *self);
static gboolean ping_iq_cb (WockyPorter *porter, WockyStanza *stanza,
    gpointer data);

static void
wocky_ping_init (WockyPing *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, WOCKY_TYPE_PING,
      WockyPingPrivate);
}

static void
wocky_ping_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  WockyPing *self = WOCKY_PING (object);
  WockyPingPrivate *priv = self->priv;

  switch (property_id)
    {
    case PROP_PORTER:
      priv->porter = g_value_dup_object (value);
      break;
    case PROP_PING_INTERVAL:
      priv->ping_interval = g_value_get_uint (value);
      reset_ping_timeout (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
wocky_ping_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  WockyPing *self = WOCKY_PING (object);
  WockyPingPrivate *priv = self->priv;

  switch (property_id)
    {
    case PROP_PORTER:
      g_value_set_object (value, priv->porter);
      break;
    case PROP_PING_INTERVAL:
      g_value_set_uint (value, priv->ping_interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
wocky_ping_constructed (GObject *object)
{
  WockyPing *self = WOCKY_PING (object);
  WockyPingPrivate *priv = self->priv;

  g_assert (priv->porter != NULL);

  priv->porter_sending_id = g_signal_connect (priv->porter, "sending",
    G_CALLBACK (porter_sending_cb), self);
  g_assert (priv->porter_sending_id != 0);

  priv->ping_iq_cb = wocky_porter_register_handler (priv->porter,
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_GET, NULL,
      WOCKY_PORTER_HANDLER_PRIORITY_NORMAL, ping_iq_cb, self,
      '(', "ping",
          ':', WOCKY_XMPP_NS_PING,
      ')', NULL);
}

static void
wocky_ping_dispose (GObject *object)
{
  WockyPing *self = WOCKY_PING (object);
  WockyPingPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_signal_handler_disconnect (priv->porter, priv->porter_sending_id);
  priv->porter_sending_id = 0;

  if (priv->ping_iq_cb != 0)
    {
      wocky_porter_unregister_handler (priv->porter, priv->ping_iq_cb);
      priv->ping_iq_cb = 0;
    }

  g_object_unref (priv->porter);
  priv->porter = NULL;

  priv->ping_interval = 0;
  reset_ping_timeout (self);

  if (G_OBJECT_CLASS (wocky_ping_parent_class)->dispose)
    G_OBJECT_CLASS (wocky_ping_parent_class)->dispose (object);
}

static void
wocky_ping_class_init (WockyPingClass *wocky_ping_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (wocky_ping_class);
  GParamSpec *spec;

  g_type_class_add_private (wocky_ping_class,
      sizeof (WockyPingPrivate));

  object_class->constructed = wocky_ping_constructed;
  object_class->set_property = wocky_ping_set_property;
  object_class->get_property = wocky_ping_get_property;
  object_class->dispose = wocky_ping_dispose;

  spec = g_param_spec_object ("porter", "Wocky porter",
      "the wocky porter to set up keepalive pings on",
      WOCKY_TYPE_PORTER,
      G_PARAM_READWRITE |
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PORTER, spec);

  spec = g_param_spec_uint ("ping-interval", "Ping interval",
      "keepalive ping interval in seconds, or 0 to disable",
      0, G_MAXUINT, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PING_INTERVAL, spec);

}

WockyPing *
wocky_ping_new (WockyPorter *porter, guint interval)
{
  g_return_val_if_fail (WOCKY_IS_PORTER (porter), NULL);

  return g_object_new (WOCKY_TYPE_PING,
      "porter", porter,
      "ping-interval", interval,
      NULL);
}

static void
send_xmpp_ping (WockyPing *self)
{
  WockyPingPrivate *priv = self->priv;
  WockyStanza *iq;

  iq = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_GET, NULL, NULL,
      '(', "ping",
          ':', WOCKY_XMPP_NS_PING,
      ')', NULL);

  wocky_porter_send_iq_async (priv->porter, iq, NULL, NULL, NULL);
  g_object_unref (iq);
}

static gboolean
ping_timeout_cb (gpointer data)
{
  WockyPing *self = WOCKY_PING (data);
  WockyPingPrivate *priv = self->priv;

  DEBUG ("sending ping after %d seconds", priv->ping_interval);

  /* Sending the ping stanza will call back to us to reset
   * the timeout, so no need to keep this one running. */
  priv->ping_timeout_id = 0;
  send_xmpp_ping (self);
  return FALSE;
}

static void
porter_sending_cb (WockyPorter *porter, WockyPing *ping)
{
  g_assert (WOCKY_IS_PING (ping));

  reset_ping_timeout (ping);
}

static void
reset_ping_timeout (WockyPing *self)
{
  WockyPingPrivate *priv = self->priv;

  if (priv->ping_timeout_id != 0)
    {
      g_source_remove (priv->ping_timeout_id);
      priv->ping_timeout_id = 0;
    }

  if (priv->ping_interval > 0)
    {
      priv->ping_timeout_id = g_timeout_add_seconds (priv->ping_interval,
        ping_timeout_cb, self);
    }
}

static gboolean
ping_iq_cb (WockyPorter *porter, WockyStanza *stanza, gpointer data)
{
  WockyStanza *reply;

  DEBUG ("replying to ping request");

  /* As a side-effect of sending pong, our keepalive timer will be
   * reset, so we don't have to do it manually. */
  reply = wocky_stanza_build_iq_result (stanza, NULL);
  wocky_porter_send (porter, reply);
  g_object_unref (reply);

  return TRUE;
}

