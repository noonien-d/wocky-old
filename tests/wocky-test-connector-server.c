/*
 * wocky-test-connector-server.c - Source for TestConnectorServer
 * Copyright © 2009 Collabora Ltd.
 * @author Vivek Dasmohapatra <vivek@collabora.co.uk>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <gio/gio.h>

#include "wocky-test-connector-server.h"

#include <wocky/wocky-xmpp-stanza.h>
#include <wocky/wocky-xmpp-connection.h>

#include <wocky/wocky-namespaces.h>
#include <wocky/wocky-debug.h>
#include <wocky/wocky-utils.h>
#include <wocky/wocky-tls.h>

#include <sasl/sasl.h>

#define INITIAL_STREAM_ID "0-HAI"
#define DEBUG(format, ...) \
  wocky_debug (DEBUG_CONNECTOR, "%s: " format, G_STRFUNC, ##__VA_ARGS__)
#define DEBUG2(format, ...) \
  wocky_debug (DEBUG_ROSTER, "%s: " format, G_STRFUNC, ##__VA_ARGS__)

G_DEFINE_TYPE (TestConnectorServer, test_connector_server, G_TYPE_OBJECT);

typedef void (*stanza_func)(TestConnectorServer *self, WockyXmppStanza *xml);
typedef struct _stanza_handler stanza_handler;
struct _stanza_handler {
  const gchar *ns;
  const gchar *name;
  stanza_func func;
};

typedef struct _iq_handler iq_handler;
struct _iq_handler {
  WockyStanzaSubType subtype;
  const gchar *payload;
  const gchar *ns;
  stanza_func func;
};

static void xmpp_init (GObject *source, GAsyncResult *result, gpointer data);
static void starttls (GObject *source, GAsyncResult *result, gpointer data);
static void finished (GObject *source, GAsyncResult *, gpointer data);
static void quit (GObject *source, GAsyncResult *result, gpointer data);

/* ************************************************************************* */
/* test connector server object definition */
typedef enum {
  SERVER_STATE_START,
  SERVER_STATE_CLIENT_OPENED,
  SERVER_STATE_SERVER_OPENED,
  SERVER_STATE_FEATURES_SENT
} server_state;

static struct { CertSet set; const gchar *key; const gchar *crt; } certs[] =
  { { CERT_STANDARD, TLS_SERVER_KEY_FILE,  TLS_SERVER_CRT_FILE  },
    { CERT_EXPIRED,  TLS_EXP_KEY_FILE,     TLS_EXP_CRT_FILE     },
    { CERT_NOT_YET,  TLS_NEW_KEY_FILE,     TLS_NEW_CRT_FILE     },
    { CERT_UNKNOWN,  TLS_UNKNOWN_KEY_FILE, TLS_UNKNOWN_CRT_FILE },
    { CERT_SELFSIGN, TLS_SS_KEY_FILE,      TLS_SS_CRT_FILE      },
    { CERT_REVOKED,  TLS_REV_KEY_FILE,     TLS_REV_CRT_FILE     },
    { CERT_NONE,     NULL,                 NULL                 } };

typedef struct _TestConnectorServerPrivate TestConnectorServerPrivate;

struct _TestConnectorServerPrivate
{
  gboolean dispose_has_run;
  WockyXmppConnection *conn;
  GIOStream *stream;
  server_state state;
  gboolean tls_started;
  gboolean authed;

  TestSaslAuthServer *sasl;
  gchar *mech;
  gchar *user;
  gchar *pass;
  gchar *version;

  CertSet cert;
  WockyTLSSession *tls_sess;
  WockyTLSConnection *tls_conn;

  struct { ServerProblem sasl; ConnectorProblem *connector; } problem;
};

#define TEST_CONNECTOR_SERVER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), TEST_TYPE_CONNECTOR_SERVER, \
   TestConnectorServerPrivate))

static void
test_connector_server_dispose (GObject *object)
{
  TestConnectorServer *self = TEST_CONNECTOR_SERVER (object);
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */
  if (priv->conn)
    g_object_unref (priv->conn);
  priv->conn = NULL;

  if (priv->stream != NULL)
    g_object_unref (priv->stream);
  priv->stream = NULL;

  if (priv->sasl)
    g_object_unref (priv->sasl);
  priv->sasl = NULL;

  if (G_OBJECT_CLASS (test_connector_server_parent_class)->dispose)
    G_OBJECT_CLASS (test_connector_server_parent_class)->dispose (object);
}

static void
test_connector_server_finalise (GObject *object)
{
  TestConnectorServer *self = TEST_CONNECTOR_SERVER (object);
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);
  /* free any data held directly by the object here */
  g_free (priv->mech);
  g_free (priv->user);
  g_free (priv->pass);
  g_free (priv->version);

  G_OBJECT_CLASS (test_connector_server_parent_class)->finalize (object);
}

static void
test_connector_server_init (TestConnectorServer *obj)
{
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (obj);
  priv->tls_started = FALSE;
  priv->authed      = FALSE;
}

static void
test_connector_server_class_init (TestConnectorServerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (TestConnectorServerPrivate));

  object_class->dispose  = test_connector_server_dispose;
  object_class->finalize = test_connector_server_finalise;
}

/* ************************************************************************* */
/* xmpp stanza handling: */
static void xmpp_handler (GObject *source,
    GAsyncResult *result,
    gpointer user_data);
static void handle_auth     (TestConnectorServer *self,
    WockyXmppStanza *xml);
static void handle_starttls (TestConnectorServer *self,
    WockyXmppStanza *xml);

static void
after_auth (GObject *source,
    GAsyncResult *res,
    gpointer data);
static void xmpp_close (GObject *source,
    GAsyncResult *result,
    gpointer data);
static void xmpp_closed (GObject *source,
    GAsyncResult *result,
    gpointer data);

static void iq_get_query_JABBER_AUTH (TestConnectorServer *self,
    WockyXmppStanza *xml);
static void iq_set_query_JABBER_AUTH (TestConnectorServer *self,
    WockyXmppStanza *xml);
static void iq_set_bind_XMPP_BIND (TestConnectorServer *self,
    WockyXmppStanza *xml);
static void iq_set_session_XMPP_SESSION (TestConnectorServer *self,
    WockyXmppStanza *xml);

static void iq_get_query_XEP77_REGISTER (TestConnectorServer *self,
    WockyXmppStanza *xml);
static void iq_set_query_XEP77_REGISTER (TestConnectorServer *self,
    WockyXmppStanza *xml);

static void iq_sent (GObject *source,
    GAsyncResult *result,
    gpointer data);
static void iq_sent_unregistered (GObject *source,
    GAsyncResult *result,
    gpointer data);

#define HANDLER(ns,x) { WOCKY_XMPP_NS_##ns, #x, handle_##x }
static stanza_handler handlers[] =
  {
    HANDLER (SASL_AUTH, auth),
    HANDLER (TLS, starttls),
    { NULL, NULL, NULL }
  };

#define IQH(S,s,name,nsp,ns) \
  { WOCKY_STANZA_SUB_TYPE_##S, #name, WOCKY_##nsp##_NS_##ns, \
    iq_##s##_##name##_##nsp##_##ns }

static iq_handler iq_handlers[] =
  {
    IQH (SET, set, bind, XMPP, BIND),
    IQH (SET, set, session, XMPP, SESSION),
    IQH (GET, get, query, JABBER, AUTH),
    IQH (SET, set, query, JABBER, AUTH),
    IQH (GET, get, query, XEP77, REGISTER),
    IQH (SET, set, query, XEP77, REGISTER),
    { WOCKY_STANZA_SUB_TYPE_NONE, NULL, NULL, NULL }
  };

/* ************************************************************************* */
/* error stanza                                                              */
static WockyXmppStanza *
error_stanza (const gchar *cond,
    const gchar *msg, gboolean extended)
{
  WockyXmppStanza *error = wocky_xmpp_stanza_new ("error");
  WockyXmppNode *node = error->node;

  wocky_xmpp_node_set_ns (node, WOCKY_XMPP_NS_STREAM);
  wocky_xmpp_node_add_child_ns (node, cond, WOCKY_XMPP_NS_STREAMS);

  if ((msg != NULL) && (*msg != '\0'))
    wocky_xmpp_node_add_child_with_content_ns (node, "text", msg,
        WOCKY_XMPP_NS_STREAMS);

  if (extended)
    wocky_xmpp_node_add_child_with_content_ns (node, "something", "blah",
        "urn:ietf:a:namespace:I:made:up");

  return error;
}

/* ************************************************************************* */
static void
iq_set_query_XEP77_REGISTER (TestConnectorServer *self,
    WockyXmppStanza *xml)
{
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);
  WockyXmppConnection *conn = priv->conn;
  WockyXmppStanza *iq = NULL;
  WockyXmppNode *env = xml->node;
  WockyXmppNode *query = wocky_xmpp_node_get_child (env, "query");
  const gchar *id = wocky_xmpp_node_get_attribute (env, "id");
  gpointer cb = iq_sent;

  DEBUG ("");

  if (priv->problem.connector->xep77 & XEP77_PROBLEM_ALREADY)
    {
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_RESULT,
          NULL, NULL,
          WOCKY_NODE_ATTRIBUTE, "id", id,
          WOCKY_NODE, "query", WOCKY_NODE_XMLNS, WOCKY_XEP77_NS_REGISTER,
          WOCKY_NODE, "registered", WOCKY_NODE_END,
          WOCKY_NODE, "username", WOCKY_NODE_TEXT, "foo", WOCKY_NODE_END,
          WOCKY_NODE, "password", WOCKY_NODE_TEXT, "bar", WOCKY_NODE_END,
          WOCKY_NODE_END,
          WOCKY_STANZA_END);
    }
  else if (priv->problem.connector->xep77 & XEP77_PROBLEM_FAIL_CONFLICT)
    {
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_ERROR,
          NULL, NULL,
          WOCKY_NODE_ATTRIBUTE, "id", id,
          WOCKY_NODE, "error", WOCKY_NODE_ATTRIBUTE, "type", "cancel",
          WOCKY_NODE, "conflict",
          WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_STANZAS,
          WOCKY_NODE_END,
          WOCKY_STANZA_END);
    }
  else if (priv->problem.connector->xep77 & XEP77_PROBLEM_FAIL_REJECTED)
    {
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_ERROR,
          NULL, NULL,
          WOCKY_NODE_ATTRIBUTE, "id", id,
          WOCKY_NODE, "error", WOCKY_NODE_ATTRIBUTE, "type", "modify",
          WOCKY_NODE, "not-acceptable",
          WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_STANZAS,
          WOCKY_NODE_END,
          WOCKY_STANZA_END);
    }
  else
    {
      if (wocky_xmpp_node_get_child (query, "remove") == NULL)
        {
          iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
              WOCKY_STANZA_SUB_TYPE_RESULT,
              NULL, NULL,
              WOCKY_NODE_ATTRIBUTE, "id", id,
              WOCKY_NODE_END,
              WOCKY_STANZA_END);
        }
      else
        {
          XEP77Problem problem = priv->problem.connector->xep77;
          XEP77Problem p = XEP77_PROBLEM_NONE;

          DEBUG ("handling CANCEL");

          if ((p = problem & XEP77_PROBLEM_CANCEL_REJECTED) ||
              (p = problem & XEP77_PROBLEM_CANCEL_DISABLED) ||
              (p = problem & XEP77_PROBLEM_CANCEL_FAILED))
            {
              const gchar *error = NULL;
              const gchar *etype = NULL;
              const gchar *ecode = NULL;

              switch (p)
                {
                  case XEP77_PROBLEM_CANCEL_REJECTED:
                    error = "bad-request";
                    etype = "modify";
                    ecode = "400";
                    break;
                  case XEP77_PROBLEM_CANCEL_DISABLED:
                    error = "not-allowed";
                    etype = "cancel";
                    ecode = "405";
                    break;
                  default:
                    error = "forbidden";
                    etype = "cancel";
                    ecode = "401";
                }

              DEBUG ("error: %s/%s", error, etype);
              iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
                  WOCKY_STANZA_SUB_TYPE_ERROR,
                  NULL, NULL,
                  WOCKY_NODE_ATTRIBUTE, "id", id,
                  WOCKY_NODE, "error",
                  WOCKY_NODE_ATTRIBUTE, "type", etype,
                  WOCKY_NODE_ATTRIBUTE, "code", ecode,
                  WOCKY_NODE, error, WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_STANZAS,
                  WOCKY_NODE_END,
                  WOCKY_STANZA_END);
            }
          else
            {
              if (priv->problem.connector->xep77 & XEP77_PROBLEM_CANCEL_STREAM)
                {
                  iq = error_stanza ("not-authorized", NULL, FALSE);
                  cb = finished;
                }
              else
                {
                  cb = iq_sent_unregistered;
                  iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
                      WOCKY_STANZA_SUB_TYPE_RESULT,
                      NULL, NULL,
                      WOCKY_NODE_ATTRIBUTE, "id", id,
                      WOCKY_NODE_END,
                      WOCKY_STANZA_END);
                }
            }
        }
    }

  wocky_xmpp_connection_send_stanza_async (conn, iq, NULL, cb, self);
  g_object_unref (xml);
  g_object_unref (iq);
}

static void
iq_get_query_XEP77_REGISTER (TestConnectorServer *self,
    WockyXmppStanza *xml)
{
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);
  WockyXmppConnection *conn = priv->conn;
  WockyXmppStanza *iq = NULL;
  WockyXmppNode *env = xml->node;
  WockyXmppNode *query = NULL;
  const gchar *id = wocky_xmpp_node_get_attribute (env, "id");

  DEBUG ("");
  if (priv->problem.connector->xep77 & XEP77_PROBLEM_NOT_AVAILABLE)
    {
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_ERROR,
          NULL, NULL,
          WOCKY_NODE_ATTRIBUTE, "id", id,
          WOCKY_NODE, "error", WOCKY_NODE_ATTRIBUTE, "type", "cancel",
          WOCKY_NODE, "service-unavailable",
          WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_STANZAS,
          WOCKY_NODE_END,
          WOCKY_STANZA_END);
    }
  else if (priv->problem.connector->xep77 & XEP77_PROBLEM_QUERY_NONSENSE)
    {
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_MESSAGE,
          WOCKY_STANZA_SUB_TYPE_NONE,
          NULL, NULL,
          WOCKY_NODE_ATTRIBUTE, "id", id,
          WOCKY_NODE, "plankton", WOCKY_NODE_XMLNS, WOCKY_XEP77_NS_REGISTER,
          WOCKY_NODE_END,
          WOCKY_STANZA_END);
    }
  else if (priv->problem.connector->xep77 & XEP77_PROBLEM_QUERY_ALREADY)
    {
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_RESULT,
          NULL, NULL,
          WOCKY_NODE_ATTRIBUTE, "id", id,
          WOCKY_NODE, "query", WOCKY_NODE_XMLNS, WOCKY_XEP77_NS_REGISTER,
          WOCKY_NODE, "registered", WOCKY_NODE_END,
          WOCKY_NODE, "username", WOCKY_NODE_TEXT, "foo", WOCKY_NODE_END,
          WOCKY_NODE, "password", WOCKY_NODE_TEXT, "bar", WOCKY_NODE_END,
          WOCKY_NODE_END,
          WOCKY_STANZA_END);
    }
  else
    {
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_RESULT,
          NULL, NULL,
          WOCKY_NODE_ATTRIBUTE, "id", id,
          WOCKY_NODE, "query", WOCKY_NODE_XMLNS, WOCKY_XEP77_NS_REGISTER,
          WOCKY_NODE_END,
          WOCKY_STANZA_END);

      query = wocky_xmpp_node_get_child (iq->node, "query");

      if (!(priv->problem.connector->xep77 & XEP77_PROBLEM_NO_ARGS))
        {
          wocky_xmpp_node_add_child (query, "username");
          wocky_xmpp_node_add_child (query, "password");

          if (priv->problem.connector->xep77 & XEP77_PROBLEM_EMAIL_ARG)
            wocky_xmpp_node_add_child (query, "email");
          if (priv->problem.connector->xep77 & XEP77_PROBLEM_STRANGE_ARG)
            wocky_xmpp_node_add_child (query, "wildebeest");
        }
    }

  wocky_xmpp_connection_send_stanza_async (conn, iq, NULL, iq_sent, self);
  g_object_unref (xml);
  g_object_unref (iq);
}

static void
iq_get_query_JABBER_AUTH (TestConnectorServer *self,
    WockyXmppStanza *xml)
{
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);
  WockyXmppConnection *conn = priv->conn;
  WockyXmppStanza *iq = NULL;
  WockyXmppNode *env = xml->node;
  const gchar *id = wocky_xmpp_node_get_attribute (env, "id");

  DEBUG ("");
  if (priv->problem.connector->jabber & JABBER_PROBLEM_AUTH_NIH)
    {
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_ERROR,
          NULL, NULL,
          WOCKY_NODE_ATTRIBUTE, "id", id,
          WOCKY_NODE, "error", WOCKY_NODE_ATTRIBUTE, "type", "cancel",
          WOCKY_NODE, "service-unavailable",
          WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_STANZAS,
          WOCKY_NODE_END,
          WOCKY_STANZA_END);
    }
  else if (priv->mech != NULL)
    {
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_RESULT,
          NULL, NULL,
          WOCKY_NODE_ATTRIBUTE, "id", id,
          WOCKY_NODE, "query", WOCKY_NODE_XMLNS, WOCKY_JABBER_NS_AUTH,
          WOCKY_NODE, "username", WOCKY_NODE_END,
          WOCKY_NODE, priv->mech, WOCKY_NODE_END,
          WOCKY_NODE, "resource", WOCKY_NODE_END,
          WOCKY_NODE_END,
          WOCKY_STANZA_END);
    }
  else
    {
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_RESULT,
          NULL, NULL,
          WOCKY_NODE_ATTRIBUTE, "id", id,
          WOCKY_NODE, "query", WOCKY_NODE_XMLNS, WOCKY_JABBER_NS_AUTH,
          WOCKY_NODE, "username", WOCKY_NODE_END,
          WOCKY_NODE, "password", WOCKY_NODE_END,
          WOCKY_NODE, "resource", WOCKY_NODE_END,
          WOCKY_NODE, "digest", WOCKY_NODE_END,
          WOCKY_NODE_END,
          WOCKY_STANZA_END);
    }

  DEBUG ("responding to iq get");
  wocky_xmpp_connection_send_stanza_async (conn, iq, NULL, iq_sent, self);
  DEBUG ("sent iq get response");
  g_object_unref (xml);
  g_object_unref (iq);
}

static void
iq_set_query_JABBER_AUTH (TestConnectorServer *self,
    WockyXmppStanza *xml)
{
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);
  WockyXmppConnection *conn = priv->conn;
  WockyXmppStanza *iq = NULL;
  WockyXmppNode *env = xml->node;
  WockyXmppNode *qry = wocky_xmpp_node_get_child (env, "query");
  JabberProblem problems = priv->problem.connector->jabber;
  JabberProblem jp = JABBER_PROBLEM_NONE;
  WockyXmppNode *username = wocky_xmpp_node_get_child (qry, "username");
  WockyXmppNode *password = wocky_xmpp_node_get_child (qry, "password");
  WockyXmppNode *resource = wocky_xmpp_node_get_child (qry, "resource");
  WockyXmppNode *sha1hash = wocky_xmpp_node_get_child (qry, "digest");
  const gchar *id = wocky_xmpp_node_get_attribute (env, "id");

  DEBUG ("");
  if (username == NULL || resource == NULL)
    problems |= JABBER_PROBLEM_AUTH_PARTIAL;
  else if (password != NULL)
    {
      if (wocky_strdiff (priv->user, username->content) ||
          wocky_strdiff (priv->pass, password->content))
        problems |= JABBER_PROBLEM_AUTH_REJECT;
    }
  else if (sha1hash != NULL)
    {
      gchar *hsrc = g_strconcat (INITIAL_STREAM_ID, priv->pass, NULL);
      gchar *sha1 = g_compute_checksum_for_string (G_CHECKSUM_SHA1, hsrc, -1);
      DEBUG ("checksum: %s vs %s", sha1, sha1hash->content);
      if (wocky_strdiff (priv->user, username->content) ||
          wocky_strdiff (sha1, sha1hash->content))
        problems |= JABBER_PROBLEM_AUTH_REJECT;

      g_free (hsrc);
      g_free (sha1);
    }
  else
    problems |= JABBER_PROBLEM_AUTH_PARTIAL;

  if ((jp = problems & JABBER_PROBLEM_AUTH_REJECT)  ||
      (jp = problems & JABBER_PROBLEM_AUTH_BIND)    ||
      (jp = problems & JABBER_PROBLEM_AUTH_PARTIAL) ||
      (jp = problems & JABBER_PROBLEM_AUTH_FAILED))
    {
      const gchar *error = NULL;
      const gchar *etype = NULL;
      const gchar *ecode = NULL;

      switch (jp)
        {
          case JABBER_PROBLEM_AUTH_REJECT:
            error = "not-authorized";
            etype = "auth";
            ecode = "401";
            break;
          case JABBER_PROBLEM_AUTH_BIND:
            error = "conflict";
            etype = "cancel";
            ecode = "409";
            break;
          case JABBER_PROBLEM_AUTH_PARTIAL:
            error = "not-acceptable";
            etype = "modify";
            ecode = "406";
            break;
          default:
            error = "bad-request";
            etype = "modify";
            ecode = "500";
            break;
        }

      DEBUG ("error: %s/%s", error, etype);
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_ERROR,
          NULL, NULL,
          WOCKY_NODE_ATTRIBUTE, "id", id,
          WOCKY_NODE, "error",
          WOCKY_NODE_ATTRIBUTE, "type", etype,
          WOCKY_NODE_ATTRIBUTE, "code", ecode,
          WOCKY_NODE, error, WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_STANZAS,
          WOCKY_NODE_END,
          WOCKY_STANZA_END);
    }
  else if (problems & JABBER_PROBLEM_AUTH_STRANGE)
    {
      DEBUG ("auth WEIRD");
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_SET,
          NULL, NULL,
          WOCKY_NODE_ATTRIBUTE, "id", id,
          WOCKY_NODE, "surstromming", WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_BIND,
          WOCKY_NODE_END,
          WOCKY_STANZA_END);
    }
  else if (problems & JABBER_PROBLEM_AUTH_NONSENSE)
    {
      DEBUG ("auth NONSENSE");
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_MESSAGE,
          WOCKY_STANZA_SUB_TYPE_NONE,
          NULL, NULL,
          WOCKY_NODE_ATTRIBUTE, "id", id,
          WOCKY_NODE, "surstromming", WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_BIND,
          WOCKY_NODE_END,
          WOCKY_STANZA_END);
    }
  else
    {
      DEBUG ("auth OK");
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_RESULT,
          NULL, NULL,
          WOCKY_NODE_ATTRIBUTE, "id", id,
          WOCKY_STANZA_END);
    }

  wocky_xmpp_connection_send_stanza_async (conn, iq, NULL, iq_sent, self);
  g_object_unref (iq);
  g_object_unref (xml);
}

static void
iq_set_bind_XMPP_BIND (TestConnectorServer *self,
    WockyXmppStanza *xml)
{
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);
  WockyXmppConnection *conn = priv->conn;
  WockyXmppStanza *iq = NULL;
  BindProblem problems = priv->problem.connector->bind;
  BindProblem bp = BIND_PROBLEM_NONE;

  DEBUG("");
  if ((bp = problems & BIND_PROBLEM_INVALID)  ||
      (bp = problems & BIND_PROBLEM_DENIED)   ||
      (bp = problems & BIND_PROBLEM_CONFLICT) ||
      (bp = problems & BIND_PROBLEM_REJECTED))
    {
      const gchar *error = NULL;
      const gchar *etype = NULL;
      switch (bp)
        {
        case BIND_PROBLEM_INVALID:
          error = "bad-request";
          etype = "modify";
          break;
        case BIND_PROBLEM_DENIED:
          error = "not-allowed";
          etype = "cancel";
          break;
        case BIND_PROBLEM_CONFLICT:
          error = "conflict";
          etype = "cancel";
          break;
        default:
          error = "badger-badger-badger-mushroom";
          etype = "moomins";
        }
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_ERROR,
          NULL, NULL,
          WOCKY_NODE, "bind", WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_BIND,
          WOCKY_NODE_END,
          WOCKY_NODE, "error", WOCKY_NODE_ATTRIBUTE, "type", etype,
          WOCKY_NODE, error, WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_STANZAS,
          WOCKY_NODE_END,
          WOCKY_STANZA_END);
    }
  else if (problems & BIND_PROBLEM_FAILED)
    {
      /* deliberately nonsensical response */
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_SET,
          NULL, NULL,
          WOCKY_NODE, "bind", WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_BIND,
          WOCKY_NODE_END,
          WOCKY_STANZA_END);
    }
  else if (problems & BIND_PROBLEM_NONSENSE)
    {
      /* deliberately nonsensical response */
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_MESSAGE,
          WOCKY_STANZA_SUB_TYPE_NONE,
          NULL, NULL,
          WOCKY_NODE, "bind", WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_BIND,
          WOCKY_NODE_END,
          WOCKY_STANZA_END);
    }
  else if (problems & BIND_PROBLEM_CLASH)
    {
      iq = error_stanza ("conflict", NULL, FALSE);
    }
  else
    {
      WockyXmppNode *ciq = xml->node;
      WockyXmppNode *bind =
        wocky_xmpp_node_get_child_ns (ciq, "bind", WOCKY_XMPP_NS_BIND);
      WockyXmppNode *res = wocky_xmpp_node_get_child (bind, "resource");
      const gchar *uniq = NULL;
      gchar *jid = NULL;

      if (res != NULL)
        uniq = res->content;
      if (uniq == NULL)
        uniq = "a-made-up-resource";

      if (problems & BIND_PROBLEM_NO_JID)
        {
          iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
              WOCKY_STANZA_SUB_TYPE_RESULT, NULL, NULL,
              WOCKY_NODE, "bind", WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_BIND,
              WOCKY_NODE_END, WOCKY_STANZA_END);
        }
      else
        {
          jid = g_strdup_printf ("user@some.doma.in/%s", uniq);
          iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
              WOCKY_STANZA_SUB_TYPE_RESULT,
              NULL, NULL,
              WOCKY_NODE, "bind", WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_BIND,
              WOCKY_NODE, "jid", WOCKY_NODE_TEXT, jid, WOCKY_NODE_END,
              WOCKY_NODE_END,
              WOCKY_STANZA_END);
          g_free (jid);
        }
    }

  wocky_xmpp_connection_send_stanza_async (conn, iq, NULL, iq_sent, self);
  g_object_unref (xml);
  g_object_unref (iq);
}

static void
iq_set_session_XMPP_SESSION (TestConnectorServer *self,
    WockyXmppStanza *xml)
{
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);
  WockyXmppConnection *conn = priv->conn;
  WockyXmppStanza *iq = NULL;
  SessionProblem problems = priv->problem.connector->session;
  SessionProblem sp = SESSION_PROBLEM_NONE;

  DEBUG ("");
  if ((sp = problems & SESSION_PROBLEM_FAILED)   ||
      (sp = problems & SESSION_PROBLEM_DENIED)   ||
      (sp = problems & SESSION_PROBLEM_CONFLICT) ||
      (sp = problems & SESSION_PROBLEM_REJECTED))
    {
      const gchar *error = NULL;
      const gchar *etype = NULL;
      switch (sp)
        {
        case SESSION_PROBLEM_FAILED:
          error = "internal-server-error";
          etype = "wait";
          break;
        case SESSION_PROBLEM_DENIED:
          error = "forbidden";
          etype = "auth";
          break;
        case SESSION_PROBLEM_CONFLICT:
          error = "conflict";
          etype = "cancel";
          break;
        default:
          error = "snaaaaake";
          etype = "mushroom";
          break;
        }
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_ERROR,
          NULL, NULL,
          WOCKY_NODE, "session", WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_SESSION,
          WOCKY_NODE_END,
          WOCKY_NODE, "error", WOCKY_NODE_ATTRIBUTE, "type", etype,
          WOCKY_NODE, error, WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_STANZAS,
          WOCKY_NODE_END,
          WOCKY_STANZA_END);
    }
  else if (problems & SESSION_PROBLEM_NO_SESSION)
    {
      iq = error_stanza ("resource-constraint", "Out of Cheese Error", FALSE);
    }
  else if (problems & SESSION_PROBLEM_NONSENSE)
    {
      /* deliberately nonsensical response */
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_MESSAGE,
          WOCKY_STANZA_SUB_TYPE_NONE,
          NULL, NULL,
          WOCKY_NODE, "surstromming", WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_BIND,
          WOCKY_NODE_END,
          WOCKY_STANZA_END);
    }
  else
    {
      iq = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_RESULT,
          NULL, NULL,
          WOCKY_NODE, "session", WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_SESSION,
          WOCKY_NODE_END,
          WOCKY_STANZA_END);
    }

  wocky_xmpp_connection_send_stanza_async (conn, iq, NULL, iq_sent, self);
  g_object_unref (xml);
  g_object_unref (iq);
}

static void
iq_sent_unregistered (GObject *source,
    GAsyncResult *result,
    gpointer data)
{
  GError *error = NULL;
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (data);
  WockyXmppConnection *conn = priv->conn;
  WockyXmppStanza *es = NULL;

  DEBUG("");

  if (!wocky_xmpp_connection_send_stanza_finish (conn, result, &error))
    {
      DEBUG ("send iq response failed: %s", error->message);
      g_error_free (error);
      return;
    }

  es = error_stanza ("not-authorized", NULL, FALSE);
  wocky_xmpp_connection_send_stanza_async (conn, es, NULL, finished, data);
  g_object_unref (es);
}


static void
iq_sent (GObject *source,
    GAsyncResult *result,
    gpointer data)
{
  GError *error = NULL;
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (data);
  WockyXmppConnection *conn = priv->conn;

  DEBUG("");

  if (!wocky_xmpp_connection_send_stanza_finish (conn, result, &error))
    {
      DEBUG ("send iq response failed: %s", error->message);
      g_error_free (error);
      return;
    }

  DEBUG ("waiting for next stanza from client");
  wocky_xmpp_connection_recv_stanza_async (conn, NULL, xmpp_handler, data);
}

static void
handle_auth (TestConnectorServer *self,
    WockyXmppStanza *xml)
{
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE(self);
  GObject *sasl = G_OBJECT (priv->sasl);

  DEBUG ("");
  /* after this the sasl auth server object is in charge:
     control should return to us after the auth stages, at the point
     when we need to send our final feature stanza:
     the stream does not return to us */
  /* this will also unref *xml when it has finished with it */
  test_sasl_auth_server_auth_async (sasl, priv->conn, xml, after_auth, self);
}

static void
handle_starttls (TestConnectorServer *self,
    WockyXmppStanza *xml)
{
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);

  DEBUG ("");
  if (!priv->tls_started)
    {
      WockyXmppConnection *conn = priv->conn;
      ConnectorProblem *problem = priv->problem.connector;
      WockyXmppStanza *reply = NULL;
      GAsyncReadyCallback cb = finished;

      if (problem->xmpp & XMPP_PROBLEM_TLS_LOAD)
        {
          reply = error_stanza ("resource-constraint", "Load Too High", FALSE);
        }
      else if (problem->xmpp & XMPP_PROBLEM_TLS_REFUSED)
        {
          reply = wocky_xmpp_stanza_new ("failure");
          wocky_xmpp_node_set_ns (reply->node, WOCKY_XMPP_NS_TLS);
        }
      else
        {
          reply = wocky_xmpp_stanza_new ("proceed");
          wocky_xmpp_node_set_ns (reply->node, WOCKY_XMPP_NS_TLS);
          cb = starttls;
          /* set up the tls server session */
          /* gnutls_global_set_log_function ((gnutls_log_func)debug_gnutls);
           * gnutls_global_set_log_level (10); */
          if (problem->death & SERVER_DEATH_TLS_NEG)
            priv->tls_sess = wocky_tls_session_server_new (priv->stream,
                1024, NULL, NULL);
          else
            {
              int x;
              const gchar *key = TLS_SERVER_KEY_FILE;
              const gchar *crt = TLS_SERVER_CRT_FILE;

              for (x = 0; certs[x].set != CERT_NONE; x++)
                {
                  if (certs[x].set == priv->cert)
                    {
                      key = certs[x].key;
                      crt = certs[x].crt;
                      break;
                    }
                }
              DEBUG2 ("cert file: %s", crt);
              priv->tls_sess =
                wocky_tls_session_server_new (priv->stream, 1024, key, crt);
            }

        }
      wocky_xmpp_connection_send_stanza_async (conn, reply, NULL, cb, self);
      g_object_unref (reply);
    }
  g_object_unref (xml);
}

static void
finished (GObject *source,
    GAsyncResult *result,
    gpointer data)
{
  TestConnectorServer *self = TEST_CONNECTOR_SERVER (data);
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);
  DEBUG ("");
  wocky_xmpp_connection_send_close_async (priv->conn, NULL, quit, data);
}

static void
quit (GObject *source,
    GAsyncResult *result,
    gpointer data)
{
  TestConnectorServer *self = TEST_CONNECTOR_SERVER (data);
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);
  GError *error = NULL;

  DEBUG ("");
  wocky_xmpp_connection_send_close_finish (priv->conn, result, &error);
  g_object_unref (self);
  exit (0);
}


static void
starttls (GObject *source,
    GAsyncResult *result,
    gpointer data)
{
  GError *error = NULL;
  TestConnectorServer *self = TEST_CONNECTOR_SERVER (data);
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);
  WockyXmppConnection *conn = WOCKY_XMPP_CONNECTION (source);

  DEBUG ("");
  if (!wocky_xmpp_connection_send_stanza_finish (conn, result, &error))
    {
      DEBUG ("Sending starttls '<proceed...>' failed: %s", error->message);
      return;
    }

  priv->tls_conn = wocky_tls_session_handshake (priv->tls_sess, NULL, &error);

  if (priv->tls_conn == NULL)
    {
      g_error ("TLS Server Setup failed: %s\n", error->message);
      exit (0);
    }

  if (priv->conn != NULL)
    g_object_unref (priv->conn);
  priv->state = SERVER_STATE_START;
  priv->conn = wocky_xmpp_connection_new (G_IO_STREAM (priv->tls_conn));
  priv->tls_started = TRUE;
  xmpp_init (NULL,NULL,self);
}


static void
xmpp_handler (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  TestConnectorServer *self;
  TestConnectorServerPrivate *priv;
  WockyXmppStanza *xml = NULL;
  WockyXmppConnection *conn = NULL;
  const gchar *ns = NULL;
  const gchar *name = NULL;
  gboolean handled = FALSE;
  GError *error = NULL;
  WockyStanzaType type = WOCKY_STANZA_TYPE_NONE;
  WockyStanzaSubType subtype = WOCKY_STANZA_SUB_TYPE_NONE;
  int i;

  DEBUG ("");
  self = TEST_CONNECTOR_SERVER (user_data);
  priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);
  conn = priv->conn;
  xml  = wocky_xmpp_connection_recv_stanza_finish (conn, result, &error);

  /* A real XMPP server would need to do some error handling here, but if
   * we got this far, we can just exit: The client (ie the test) will
   * report any error that actually needs reporting - we don't need to */
  if (error != NULL)
      exit (0);

  ns   = wocky_xmpp_node_get_ns (xml->node);
  name = xml->node->name;
  wocky_xmpp_stanza_get_type_info (xml, &type, &subtype);

  /* if we find a handler, the handler is responsible for listening for the
     next stanza and setting up the next callback in the chain: */
  if (type == WOCKY_STANZA_TYPE_IQ)
    for (i = 0; iq_handlers[i].payload != NULL; i++)
      {
        iq_handler *iq = &iq_handlers[i];
        WockyXmppNode *payload =
          wocky_xmpp_node_get_child_ns (xml->node, iq->payload, iq->ns);
        /* namespace, stanza subtype and payload tag name must match: */
        if ((payload == NULL) || (subtype != iq->subtype))
          continue;
        DEBUG ("test_connector_server:invoking iq handler %s\n", iq->payload);
        (iq->func) (self, xml);
        handled = TRUE;
        break;
      }
  else
    for (i = 0; handlers[i].ns != NULL; i++)
      {
        if (!strcmp (ns, handlers[i].ns) && !strcmp (name, handlers[i].name))
          {
            DEBUG ("test_connector_server:invoking handler %s.%s\n", ns, name);
            (handlers[i].func) (self, xml);
            handled = TRUE;
            break;
          }
      }

  /* no handler found: just complain and sit waiting for the next stanza */
  if (!handled)
    {
      DEBUG ("<%s xmlns=\"%s\"… not handled\n", name, ns);
      wocky_xmpp_connection_recv_stanza_async (conn, NULL, xmpp_handler, self);
      g_object_unref (xml);
    }
}
/* ************************************************************************* */
/* resume control after the sasl auth server is done:                        */
static void
after_auth (GObject *source,
    GAsyncResult *res,
    gpointer data)
{
  GError *error = NULL;
  WockyXmppStanza *feat = NULL;
  WockyXmppNode *node = NULL;
  TestSaslAuthServer *tsas = TEST_SASL_AUTH_SERVER (source);
  TestConnectorServer *tcs = TEST_CONNECTOR_SERVER (data);
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (tcs);
  WockyXmppConnection *conn = priv->conn;

  DEBUG ("");
  if (!test_sasl_auth_server_auth_finish (tsas, res, &error))
    {
      wocky_xmpp_connection_send_close_async (conn, NULL, xmpp_close, data);
      return;
    }

  feat = wocky_xmpp_stanza_new ("stream:features");
  node = feat->node;

  if (!(priv->problem.connector->xmpp & XMPP_PROBLEM_NO_SESSION))
    wocky_xmpp_node_add_child_ns (node, "session", WOCKY_XMPP_NS_SESSION);

  if (!(priv->problem.connector->xmpp & XMPP_PROBLEM_CANNOT_BIND))
    wocky_xmpp_node_add_child_ns (node, "bind", WOCKY_XMPP_NS_BIND);

  priv->state = SERVER_STATE_FEATURES_SENT;
  wocky_xmpp_connection_send_stanza_async (conn, feat, NULL, xmpp_init, data);

  g_object_unref (feat);
}

/* ************************************************************************* */
/* initial XMPP stream setup, up to sending features stanza */
static WockyXmppStanza *
feature_stanza (TestConnectorServer *self)
{
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);
  XmppProblem problem = priv->problem.connector->xmpp;
  const gchar *name = NULL;
  WockyXmppStanza *feat = NULL;
  WockyXmppNode *node = NULL;

  DEBUG ("");
  if (problem & XMPP_PROBLEM_OTHER_HOST)
    return error_stanza ("host-unknown", "some sort of DNS error", TRUE);

  name = (problem & XMPP_PROBLEM_FEATURES) ? "badger" : "features";
  feat = wocky_xmpp_stanza_new (name);
  node = feat->node;

  DEBUG ("constructing <%s...>... stanza\n", name);
  wocky_xmpp_node_set_ns (node, WOCKY_XMPP_NS_STREAM);

  if (priv->problem.sasl != SERVER_PROBLEM_NO_SASL)
    {
      priv->sasl = test_sasl_auth_server_new (NULL, priv->mech,
          priv->user, priv->pass, NULL, priv->problem.sasl, FALSE);
      test_sasl_auth_server_set_mechs (G_OBJECT (priv->sasl), feat);
    }

  if (problem & XMPP_PROBLEM_OLD_AUTH_FEATURE)
    wocky_xmpp_node_add_child_ns (node, "auth", WOCKY_JABBER_NS_AUTH_FEATURE);

  if (!(problem & XMPP_PROBLEM_NO_TLS) && !priv->tls_started)
    wocky_xmpp_node_add_child_ns (node, "starttls", WOCKY_XMPP_NS_TLS);

  return feat;
}

static void
xmpp_close (GObject *source,
    GAsyncResult *result,
    gpointer data)
{
  TestConnectorServer *self = TEST_CONNECTOR_SERVER (data);
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);

  DEBUG ("");
  wocky_xmpp_connection_send_close_async (priv->conn, NULL, xmpp_closed, self);
}

static void
xmpp_closed (GObject *source,
    GAsyncResult *result,
    gpointer data)
{
  GError *error = NULL;
  TestConnectorServer *self = TEST_CONNECTOR_SERVER (data);
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);
  DEBUG ("");
  wocky_xmpp_connection_send_close_finish (priv->conn, result, &error);
}

static void startssl (TestConnectorServer *self)
{
  GError *error = NULL;
  TestConnectorServerPrivate *priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);
  WockyXmppConnection *conn = priv->conn;
  ConnectorProblem *problem = priv->problem.connector;

  if (priv->tls_started)
    return;

  DEBUG ("creating SSL Session [server]");
  if (problem->death & SERVER_DEATH_TLS_NEG)
    priv->tls_sess =
      wocky_tls_session_server_new (priv->stream, 1024, NULL, NULL);
  else
    {
      int x;
      const gchar *key = TLS_SERVER_KEY_FILE;
      const gchar *crt = TLS_SERVER_CRT_FILE;

      for (x = 0; certs[x].set != CERT_NONE; x++)
        {
          if (certs[x].set == priv->cert)
            {
              key = certs[x].key;
              crt = certs[x].crt;
              break;
            }
        }

      priv->tls_sess =
        wocky_tls_session_server_new (priv->stream, 1024, key, crt);
    }

  DEBUG ("starting server SSL handshake");
  priv->tls_conn = wocky_tls_session_handshake (priv->tls_sess, NULL, &error);
  if (priv->tls_conn == NULL)
    {
      g_error ("SSL Server Setup failed: %p %s\n",
          priv->tls_sess, error->message);
      exit (0);
    }
  DEBUG ("server SSL handshake complete");

  priv->conn = wocky_xmpp_connection_new (G_IO_STREAM (priv->tls_conn));
  priv->tls_started = TRUE;
  if (conn != NULL)
    g_object_unref (conn);

}

static void
xmpp_init (GObject *source,
    GAsyncResult *result,
    gpointer data)
{
  TestConnectorServer *self;
  TestConnectorServerPrivate *priv;
  WockyXmppStanza *xml;
  WockyXmppConnection *conn;

  self = TEST_CONNECTOR_SERVER (data);
  priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);
  conn = priv->conn;

  DEBUG ("test_connector_server:xmpp_init %d", priv->state);
  DEBUG ("connection: %p", conn);

  switch (priv->state)
    {
      /* wait for <stream:stream… from the client */
    case SERVER_STATE_START:
      DEBUG ("SERVER_STATE_START");

      if (priv->problem.connector->xmpp & XMPP_PROBLEM_OLD_SSL)
        {
          startssl (self);
          conn = priv->conn;
        }
      DEBUG ("SERVER_STATE_START: SSL");
      priv->state = SERVER_STATE_CLIENT_OPENED;

      wocky_xmpp_connection_recv_open_async (conn, NULL, xmpp_init, self);
      if (priv->problem.connector->death & SERVER_DEATH_SERVER_START)
          exit (0);
      break;

      /* send our own <stream:stream… */
    case SERVER_STATE_CLIENT_OPENED:
      DEBUG ("SERVER_STATE_CLIENT_OPENED\n");
      priv->state = SERVER_STATE_SERVER_OPENED;
      wocky_xmpp_connection_recv_open_finish (conn, result,
          NULL, NULL, NULL, NULL, NULL, NULL);
      wocky_xmpp_connection_send_open_async (conn, NULL, "testserver",
          priv->version, NULL, INITIAL_STREAM_ID, NULL, xmpp_init, self);
      if (priv->problem.connector->death & SERVER_DEATH_CLIENT_OPEN)
          exit (0);
      break;

      /* send our feature set */
    case SERVER_STATE_SERVER_OPENED:
      DEBUG ("SERVER_STATE_SERVER_OPENED\n");
      priv->state = SERVER_STATE_FEATURES_SENT;
      wocky_xmpp_connection_send_open_finish (conn, result, NULL);

      if (priv->problem.connector->death & SERVER_DEATH_SERVER_OPEN)
          exit (0);

      if (priv->problem.connector->xmpp & XMPP_PROBLEM_OLD_SERVER)
        {
          DEBUG ("diverting to old-jabber-auth");
          wocky_xmpp_connection_recv_stanza_async (priv->conn, NULL,
              xmpp_handler, self);
        }
      else
        {
          xml = feature_stanza (self);
          wocky_xmpp_connection_send_stanza_async (conn, xml,
              NULL, xmpp_init, self);
          g_object_unref (xml);
        }
      break;

      /* ok, we're done with initial stream setup */
    case SERVER_STATE_FEATURES_SENT:
      DEBUG ("SERVER_STATE_FEATURES_SENT\n");
      wocky_xmpp_connection_send_stanza_finish (conn, result, NULL);
      wocky_xmpp_connection_recv_stanza_async (conn, NULL, xmpp_handler, self);
      if (priv->problem.connector->death & SERVER_DEATH_FEATURES)
        {
          sleep (1);
          exit (0);
        }
      break;

    default:
      DEBUG ("Unknown Server state. Broken code flow\n");
    }
}

/* ************************************************************************* */
/* exposed methods */

TestConnectorServer *
test_connector_server_new (GIOStream *stream,
    gchar *mech,
    const gchar *user,
    const gchar *pass,
    const gchar *version,
    ConnectorProblem *problem,
    ServerProblem sasl_problem,
    CertSet cert)
{
  TestConnectorServer *self;
  TestConnectorServerPrivate *priv;

  DEBUG ("test_connector_server_new\n");

  self = g_object_new (TEST_TYPE_CONNECTOR_SERVER, NULL);
  priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);

  priv->stream = g_object_ref (stream);
  priv->mech   = g_strdup (mech);
  priv->user   = g_strdup (user);
  priv->pass   = g_strdup (pass);
  priv->problem.sasl      = sasl_problem;
  priv->problem.connector = problem;
  priv->conn   = wocky_xmpp_connection_new (stream);
  priv->cert   = cert;

  DEBUG ("connection: %p", priv->conn);

  if (problem->xmpp & XMPP_PROBLEM_OLD_SERVER)
    priv->version = g_strdup ((version == NULL) ? "0.9" : version);
  else
    priv->version = g_strdup ((version == NULL) ? "1.0" : version);

  return self;
}

void
test_connector_server_start (GObject *object)
{
  TestConnectorServer *self;
  TestConnectorServerPrivate *priv;

  DEBUG("test_connector_server_start\n");

  self = TEST_CONNECTOR_SERVER (object);
  priv = TEST_CONNECTOR_SERVER_GET_PRIVATE (self);
  priv->state = SERVER_STATE_START;
  DEBUG ("connection: %p", priv->conn);
  xmpp_init (NULL,NULL,self);
}
