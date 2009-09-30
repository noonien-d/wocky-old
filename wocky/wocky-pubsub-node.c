/*
 * wocky-pubsub-node.c - WockyPubsubNode
 * Copyright (C) 2009 Collabora Ltd.
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

#include "wocky-pubsub-node.h"

#include "wocky-porter.h"
#include "wocky-utils.h"
#include "wocky-namespaces.h"
#include "wocky-signals-marshal.h"

#define DEBUG_FLAG DEBUG_PUBSUB
#include "wocky-debug.h"

G_DEFINE_TYPE (WockyPubsubNode, wocky_pubsub_node, G_TYPE_OBJECT)

/* signal enum */
#if 0
enum
{
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = {0};
#endif

enum
{
  PROP_SERVICE = 1,
  PROP_NAME,
};

/* private structure */
typedef struct _WockyPubsubNodePrivate WockyPubsubNodePrivate;

struct _WockyPubsubNodePrivate
{
  WockyPubsubService *service;
  WockyPorter *porter;

  gchar *service_jid;
  gchar *name;

  gboolean dispose_has_run;
};

#define WOCKY_PUBSUB_NODE_GET_PRIVATE(o)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), WOCKY_TYPE_PUBSUB_NODE, \
    WockyPubsubNodePrivate))

static void
wocky_pubsub_node_init (WockyPubsubNode *obj)
{
  /*
  WockyPubsubNode *self = WOCKY_PUBSUB_NODE (obj);
  WockyPubsubNodePrivate *priv = WOCKY_PUBSUB_NODE_GET_PRIVATE (self);
  */
}

static void
wocky_pubsub_node_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  WockyPubsubNode *self = WOCKY_PUBSUB_NODE (object);
  WockyPubsubNodePrivate *priv = WOCKY_PUBSUB_NODE_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_SERVICE:
        priv->service = g_value_dup_object (value);
        break;
      case PROP_NAME:
        priv->name = g_value_dup_string (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
wocky_pubsub_node_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  WockyPubsubNode *self = WOCKY_PUBSUB_NODE (object);
  WockyPubsubNodePrivate *priv = WOCKY_PUBSUB_NODE_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_SERVICE:
        g_value_set_object (value, priv->service);
        break;
      case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}


static void
wocky_pubsub_node_dispose (GObject *object)
{
  WockyPubsubNode *self = WOCKY_PUBSUB_NODE (object);
  WockyPubsubNodePrivate *priv = WOCKY_PUBSUB_NODE_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_object_unref (priv->service);

  if (priv->porter != NULL)
    g_object_unref (priv->porter);

  if (G_OBJECT_CLASS (wocky_pubsub_node_parent_class)->dispose)
    G_OBJECT_CLASS (wocky_pubsub_node_parent_class)->dispose (object);
}

static void
wocky_pubsub_node_finalize (GObject *object)
{
  WockyPubsubNode *self = WOCKY_PUBSUB_NODE (object);
  WockyPubsubNodePrivate *priv = WOCKY_PUBSUB_NODE_GET_PRIVATE (self);

  g_free (priv->name);
  g_free (priv->service_jid);

  G_OBJECT_CLASS (wocky_pubsub_node_parent_class)->finalize (object);
}

static void
wocky_pubsub_node_constructed (GObject *object)
{
  WockyPubsubNode *self = WOCKY_PUBSUB_NODE (object);
  WockyPubsubNodePrivate *priv = WOCKY_PUBSUB_NODE_GET_PRIVATE (self);
  WockySession *session;

  g_assert (priv->service != NULL);
  g_assert (priv->name != NULL);

  g_object_get (priv->service,
      "jid", &(priv->service_jid),
      "session", &session,
      NULL);
  g_assert (priv->service_jid != NULL);

  g_assert (session != NULL);
  priv->porter = wocky_session_get_porter (session);
  g_object_ref (priv->porter);
  g_object_unref (session);
}

static void
wocky_pubsub_node_class_init (
    WockyPubsubNodeClass *wocky_pubsub_node_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (wocky_pubsub_node_class);
  GParamSpec *param_spec;

  g_type_class_add_private (wocky_pubsub_node_class,
      sizeof (WockyPubsubNodePrivate));

  object_class->set_property = wocky_pubsub_node_set_property;
  object_class->get_property = wocky_pubsub_node_get_property;
  object_class->dispose = wocky_pubsub_node_dispose;
  object_class->finalize = wocky_pubsub_node_finalize;
  object_class->constructed = wocky_pubsub_node_constructed;

  param_spec = g_param_spec_object ("service", "service",
      "the Wocky Pubsub service associated with this pubsub node",
      WOCKY_TYPE_PUBSUB_SERVICE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SERVICE, param_spec);

  param_spec = g_param_spec_string ("name", "name",
      "The name of the pubsub node",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_NAME, param_spec);
}

WockyPubsubNode *
wocky_pubsub_node_new (WockyPubsubService *service,
    const gchar *name)
{
  return g_object_new (WOCKY_TYPE_PUBSUB_NODE,
      "service", service,
      "name", name,
      NULL);
}

const gchar *
wocky_pubsub_node_get_name (WockyPubsubNode *self)
{
  WockyPubsubNodePrivate *priv = WOCKY_PUBSUB_NODE_GET_PRIVATE (self);

  return priv->name;
}

static void
delete_node_iq_cb (GObject *source,
    GAsyncResult *res,
    gpointer user_data)
{
  GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (user_data);
  GError *error = NULL;
  WockyXmppStanza *reply;

  reply = wocky_porter_send_iq_finish (WOCKY_PORTER (source), res, &error);
  if (reply == NULL)
    {
      g_simple_async_result_set_from_error (result, error);
      g_error_free (error);
      goto out;
    }

  if (wocky_xmpp_stanza_extract_errors (reply, NULL, &error, NULL, NULL))
    {
      g_simple_async_result_set_from_error (result, error);
      g_error_free (error);
      goto out;
    }

  DEBUG ("node deleted");

out:
  g_simple_async_result_complete (result);
  g_object_unref (result);
  if (reply != NULL)
    g_object_unref (reply);
}

void
wocky_pubsub_node_delete_async (WockyPubsubNode *self,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  WockyPubsubNodePrivate *priv = WOCKY_PUBSUB_NODE_GET_PRIVATE (self);
  WockyXmppStanza *stanza;
  GSimpleAsyncResult *result;

  stanza = wocky_xmpp_stanza_build (
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
      NULL, priv->service_jid,
      WOCKY_NODE, "pubsub",
        WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_PUBSUB_OWNER,
        WOCKY_NODE, "delete",
          WOCKY_NODE_ATTRIBUTE, "node", priv->name,
        WOCKY_NODE_END,
      WOCKY_NODE_END, WOCKY_STANZA_END);

  result = g_simple_async_result_new (G_OBJECT (self), callback, user_data,
    wocky_pubsub_node_delete_finish);

  wocky_porter_send_iq_async (priv->porter, stanza, NULL, delete_node_iq_cb,
      result);

  g_object_unref (stanza);
}

gboolean
wocky_pubsub_node_delete_finish (WockyPubsubNode *self,
    GAsyncResult *result,
    GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
      error))
    return FALSE;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
    G_OBJECT (self), wocky_pubsub_node_delete_finish), FALSE);

  return TRUE;
}
