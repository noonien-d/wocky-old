/*
 * wocky-sasl-auth.c - Source for WockySaslAuth
 * Copyright (C) 2006-2009 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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

#include "wocky-sasl-auth.h"
#include "wocky-signals-marshal.h"
#include "wocky-namespaces.h"

#define DEBUG_FLAG DEBUG_SASL
#include "wocky-debug.h"

G_DEFINE_TYPE(WockySaslAuth, wocky_sasl_auth, G_TYPE_OBJECT)

/* signal enum */
enum
{
    USERNAME_REQUESTED,
    PASSWORD_REQUESTED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

typedef enum {
  WOCKY_SASL_AUTH_STATE_NO_MECH = 0,
  WOCKY_SASL_AUTH_STATE_PLAIN_STARTED,
  WOCKY_SASL_AUTH_STATE_DIGEST_MD5_STARTED,
  WOCKY_SASL_AUTH_STATE_DIGEST_MD5_SENT_AUTH_RESPONSE,
  WOCKY_SASL_AUTH_STATE_DIGEST_MD5_SENT_FINAL_RESPONSE,
  WOCKY_SASL_AUTH_STATE_SUCCEEDED,
  WOCKY_SASL_AUTH_STATE_FAILED,
} WockySaslAuthState;

typedef enum {
  WOCKY_SASL_AUTH_PLAIN = 0,
  WOCKY_SASL_AUTH_DIGEST_MD5,
  WOCKY_SASL_AUTH_NR_MECHANISMS,
} WockySaslAuthMechanism;

/* private structure */
typedef struct _WockySaslAuthPrivate WockySaslAuthPrivate;

struct _WockySaslAuthPrivate
{
  gboolean dispose_has_run;
  WockyXmppConnection *connection;
  gchar *server;
  gchar *digest_md5_rspauth;
  WockySaslAuthState state;
  WockySaslAuthMechanism mech;
  GCancellable *cancel;
  GSimpleAsyncResult *result;
};

#define WOCKY_SASL_AUTH_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), WOCKY_TYPE_SASL_AUTH, WockySaslAuthPrivate))

GQuark
wocky_sasl_auth_error_quark (void) {
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("wocky_sasl_auth_error");

  return quark;
}


static void
wocky_sasl_auth_init (WockySaslAuth *obj)
{
  //WockySaslAuthPrivate *priv = WOCKY_SASL_AUTH_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
}

static void wocky_sasl_auth_dispose (GObject *object);
static void wocky_sasl_auth_finalize (GObject *object);

static void
wocky_sasl_auth_class_init (WockySaslAuthClass *wocky_sasl_auth_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (wocky_sasl_auth_class);

  g_type_class_add_private (wocky_sasl_auth_class,
      sizeof (WockySaslAuthPrivate));

  signals[USERNAME_REQUESTED] = g_signal_new ("username-requested",
      G_OBJECT_CLASS_TYPE(wocky_sasl_auth_class),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      _wocky_signals_marshal_STRING__VOID,
      G_TYPE_STRING, 0);

  signals[PASSWORD_REQUESTED] = g_signal_new ("password-requested",
      G_OBJECT_CLASS_TYPE(wocky_sasl_auth_class),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      _wocky_signals_marshal_STRING__VOID,
      G_TYPE_STRING, 0);

  object_class->dispose = wocky_sasl_auth_dispose;
  object_class->finalize = wocky_sasl_auth_finalize;

}

void
wocky_sasl_auth_dispose (GObject *object)
{
  WockySaslAuth *self = WOCKY_SASL_AUTH (object);
  WockySaslAuthPrivate *priv = WOCKY_SASL_AUTH_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */
  if (priv->connection != NULL)
    {
      g_object_unref (priv->connection);
    }

  if (G_OBJECT_CLASS (wocky_sasl_auth_parent_class)->dispose)
    G_OBJECT_CLASS (wocky_sasl_auth_parent_class)->dispose (object);
}

void
wocky_sasl_auth_finalize (GObject *object)
{
  WockySaslAuth *self = WOCKY_SASL_AUTH (object);
  WockySaslAuthPrivate *priv = WOCKY_SASL_AUTH_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_free (priv->server);
  g_free (priv->digest_md5_rspauth);


  G_OBJECT_CLASS (wocky_sasl_auth_parent_class)->finalize (object);
}

static void
auth_reset (WockySaslAuth *sasl)
{
  WockySaslAuthPrivate *priv = WOCKY_SASL_AUTH_GET_PRIVATE (sasl);

  g_free (priv->server);
  priv->server = NULL;

  g_free (priv->digest_md5_rspauth);
  priv->digest_md5_rspauth = NULL;

  if (priv->connection != NULL)
    {
      g_object_unref (priv->connection);
      priv->connection = NULL;
    }
}

static void
auth_succeeded (WockySaslAuth *sasl)
{
  WockySaslAuthPrivate *priv = WOCKY_SASL_AUTH_GET_PRIVATE (sasl);
  GSimpleAsyncResult *r;

  DEBUG ("Authentication succeeded");
  priv->state = WOCKY_SASL_AUTH_STATE_SUCCEEDED;

  r = priv->result;
  priv->result = NULL;

  g_simple_async_result_complete (r);
  g_object_unref (r);
}

static void
auth_failed (WockySaslAuth *sasl, gint error, const gchar *format, ...)
{
  gchar *message;
  va_list args;
  GSimpleAsyncResult *r;
  WockySaslAuthPrivate *priv = WOCKY_SASL_AUTH_GET_PRIVATE (sasl);

  auth_reset (sasl);

  va_start (args, format);
  message = g_strdup_vprintf (format, args);
  va_end (args);

  DEBUG ("Authentication failed!: %s", message);
  priv->state = WOCKY_SASL_AUTH_STATE_FAILED;

  r = priv->result;
  priv->result = NULL;

  g_simple_async_result_set_error (r,
    WOCKY_SASL_AUTH_ERROR, error, "%s", message);

  g_simple_async_result_complete (r);
  g_object_unref (r);

  g_free (message);
}


WockySaslAuth *
wocky_sasl_auth_new (void)
{
  return g_object_new (WOCKY_TYPE_SASL_AUTH, NULL);
}

static gboolean
each_mechanism (WockyXmppNode *node, gpointer user_data)
{
  GSList **list = (GSList **)user_data;
  if (strcmp (node->name, "mechanism"))
    {
      return TRUE;
    }
  *list = g_slist_append (*list, g_strdup (node->content));
  return TRUE;
}

static GSList *
wocky_sasl_auth_mechanisms_to_list (WockyXmppNode *mechanisms)
{
  GSList *result = NULL;

  if (mechanisms == NULL)
    return NULL;

  wocky_xmpp_node_each_child (mechanisms, each_mechanism, &result);
  return result;
}

static gboolean
wocky_sasl_auth_has_mechanism (GSList *list, const gchar *mech) {
  GSList *t;
  for (t = list ; t != NULL ; t = g_slist_next (t)) {
    if (!strcmp ((gchar *) t->data, mech)) {
      return TRUE;
    }
  }
  return FALSE;
}

static gchar *
strndup_unescaped (const gchar *str, gsize len)
{
  const gchar *s;
  gchar *d, *ret;

  ret = g_malloc0 (len + 1);
  for (s = str, d = ret ; s < (str + len) ; s++, d++) {
    if (*s == '\\')
      s++;
    *d = *s;
  }

  return ret;
}

static GHashTable *
digest_md5_challenge_to_hash (const gchar * challenge)
{
  GHashTable *result = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_free);
  const gchar *keystart, *keyend, *valstart;
  const gchar *c = challenge;
  gchar *key, *val;

  do {
    keystart = c;
    for (; *c != '\0' && *c != '='; c++)
      ;

    if (*c == '\0' || c == keystart)
      goto error;

    keyend = c;
    c++;

    if (*c == '"')
      {
        c++;
        valstart = c;
        for (; *c != '\0' && *c != '"'; c++)
          ;
        if (*c == '\0' || c == valstart)
          goto error;
        val = strndup_unescaped (valstart, c - valstart);
        c++;
      }
    else
      {
        valstart = c;
        for (; *c !=  '\0' && *c != ','; c++)
          ;
        if (c == valstart)
          goto error;
        val = g_strndup (valstart, c - valstart);
      }

    key = g_strndup (keystart, keyend - keystart);

    g_hash_table_insert (result, key, val);

    if (*c == ',')
      c++;
  } while (*c != '\0');

  return result;

error:
  DEBUG ("Failed to parse challenge: %s", challenge);
  g_hash_table_destroy (result);
  return NULL;
}

static guint8 *
md5_hash (gchar *value)
{
  GChecksum *checksum;
  guint8 *result;
  gsize len;

  len = g_checksum_type_get_length (G_CHECKSUM_MD5);
  g_assert (len == 16);

  result = g_malloc (len);

  checksum = g_checksum_new (G_CHECKSUM_MD5);
  g_checksum_update (checksum, (guchar *) value, -1);
  g_checksum_get_digest (checksum, result, &len);
  g_checksum_free (checksum);

  g_assert (len == 16);

  return result;
}

static gchar *
md5_hex_hash (gchar *value)
{
  return g_compute_checksum_for_string (G_CHECKSUM_MD5, value, -1);
}

static gchar *
digest_md5_generate_cnonce (void)
{
  /* RFC 2831 recommends the the nonce to be either hexadecimal or base64 with
   * at least 64 bits of entropy */
#define NR 8
  guint32 n[NR];
  int i;

  for (i = 0; i < NR; i++)
    n[i] = g_random_int ();

  return g_base64_encode ((guchar *) n, sizeof (n));
}

static gchar *
md5_prepare_response (WockySaslAuth *sasl, GHashTable *challenge)
{
  WockySaslAuthPrivate *priv = WOCKY_SASL_AUTH_GET_PRIVATE (sasl);
  GString *response = g_string_new ("");
  const gchar *realm, *nonce;
  gchar *a1, *a1h, *a2, *a2h, *kd, *kdh;
  gchar *cnonce = NULL;
  gchar *username = NULL;
  gchar *password = NULL;
  gchar *tmp;
  guint8 *digest_md5;
  gsize len;

  g_signal_emit (sasl, signals[USERNAME_REQUESTED], 0, &username);
  g_signal_emit (sasl, signals[PASSWORD_REQUESTED], 0, &password);

  if (username == NULL || password == NULL)
    {
      g_simple_async_result_set_error (priv->result, WOCKY_SASL_AUTH_ERROR,
          WOCKY_SASL_AUTH_ERROR_NO_CREDENTIALS,
          "No username or password provided");

      g_simple_async_result_complete_in_idle (priv->result);

      g_object_unref (priv->result);
      priv->result = NULL;
      goto error;
    }

  DEBUG ("Got username and password");

  nonce = g_hash_table_lookup (challenge, "nonce");
  if (nonce == NULL || nonce == '\0')
    {
      auth_failed (sasl, WOCKY_SASL_AUTH_ERROR_INVALID_REPLY,
        "Server didn't provide a nonce in the challenge");
      goto error;
    }

  cnonce = digest_md5_generate_cnonce ();

  /* FIXME challenge can contain multiple realms */
  realm = g_hash_table_lookup (challenge, "realm");
  if (realm == NULL)
    {
      realm = priv->server;
    }

  /* FIXME properly escape values */
  g_string_append_printf (response, "username=\"%s\"", username);
  g_string_append_printf (response, ",realm=\"%s\"", realm);
  g_string_append_printf (response, ",digest-uri=\"xmpp/%s\"", realm);
  g_string_append_printf (response, ",nonce=\"%s\",nc=00000001", nonce);
  g_string_append_printf (response, ",cnonce=\"%s\"", cnonce);
  /* FIXME should check if auth is in the cop challenge val */
  g_string_append_printf (response, ",qop=auth,charset=utf-8");

  tmp = g_strdup_printf ("%s:%s:%s", username, realm, password);
  digest_md5 = md5_hash (tmp);
  g_free (tmp);

  a1 = g_strdup_printf ("0123456789012345:%s:%s", nonce, cnonce);
  len = strlen (a1);
  /* MD5 hash is 16 bytes */
  memcpy (a1, digest_md5, 16);
  a1h = md5_hex_hash (a1);

  g_free (digest_md5);

  a2 = g_strdup_printf ("AUTHENTICATE:xmpp/%s", realm);
  a2h = md5_hex_hash (a2);

  kd = g_strdup_printf ("%s:%s:00000001:%s:auth:%s", a1h, nonce, cnonce, a2h);
  kdh = md5_hex_hash (kd);
  g_string_append_printf (response, ",response=%s", kdh);

  g_free (kd);
  g_free (kdh);
  g_free (a2);
  g_free (a2h);

  /* Calculate the response we expect from the server */
  a2 = g_strdup_printf (":xmpp/%s", realm);
  a2h = md5_hex_hash (a2);

  kd =  g_strdup_printf ("%s:%s:00000001:%s:auth:%s", a1h, nonce, cnonce, a2h);
  g_free (priv->digest_md5_rspauth);
  priv->digest_md5_rspauth = md5_hex_hash (kd);

  g_free (a1);
  g_free (a1h);
  g_free (a2);
  g_free (a2h);
  g_free (kd);

out:
  g_free (cnonce);
  g_free (username);
  g_free (password);

  return response != NULL ? g_string_free (response, FALSE) : NULL;

error:
  g_string_free (response, TRUE);
  response = NULL;
  goto out;
}

static void
digest_md5_send_initial_response (WockySaslAuth *sasl, GHashTable *challenge)
{
  WockySaslAuthPrivate *priv = WOCKY_SASL_AUTH_GET_PRIVATE (sasl);
  WockyXmppStanza *stanza;
  gchar *response, *response64;

  response = md5_prepare_response (sasl, challenge);
  if (response == NULL)
    {
      return;
    }

  DEBUG ("Prepared response: %s", response);

  response64 = g_base64_encode ((guchar *) response, strlen (response));

  stanza = wocky_xmpp_stanza_new ("response");
  wocky_xmpp_node_set_ns (stanza->node, WOCKY_XMPP_NS_SASL_AUTH);
  wocky_xmpp_node_set_content (stanza->node, response64);

  priv->state = WOCKY_SASL_AUTH_STATE_DIGEST_MD5_SENT_AUTH_RESPONSE;

  /* FIXME handle send error */
  wocky_xmpp_connection_send_stanza_async (priv->connection, stanza,
    NULL, NULL, NULL);

  g_free (response);
  g_free (response64);
  g_object_unref (stanza);
}

static void
digest_md5_check_server_response (WockySaslAuth *sasl, GHashTable *challenge)
{
  WockyXmppStanza *stanza;
  const gchar *rspauth;
  WockySaslAuthPrivate *priv = WOCKY_SASL_AUTH_GET_PRIVATE (sasl);

  rspauth = g_hash_table_lookup (challenge, "rspauth");
  if (rspauth == NULL)
    {
      auth_failed (sasl, WOCKY_SASL_AUTH_ERROR_INVALID_REPLY,
                 "Server send an invalid reply (no rspauth)");
      return;
    }

  if (strcmp (priv->digest_md5_rspauth, rspauth))
    {
      auth_failed (sasl, WOCKY_SASL_AUTH_ERROR_INVALID_REPLY,
                 "Server send an invalid reply (rspauth not matching)");
      return;
    }

  stanza = wocky_xmpp_stanza_new ("response");
  wocky_xmpp_node_set_ns (stanza->node, WOCKY_XMPP_NS_SASL_AUTH);

  priv->state = WOCKY_SASL_AUTH_STATE_DIGEST_MD5_SENT_FINAL_RESPONSE;

  /* FIXME handle send error */
  wocky_xmpp_connection_send_stanza_async (priv->connection, stanza,
    NULL, NULL, NULL);

  g_object_unref (stanza);
}

static void
digest_md5_handle_challenge (WockySaslAuth *sasl, WockyXmppStanza *stanza)
{
  WockySaslAuthPrivate *priv = WOCKY_SASL_AUTH_GET_PRIVATE(sasl);
  gchar *challenge = NULL;
  gsize len;
  GHashTable *h = NULL;

  if (stanza->node->content != NULL)
    {
      challenge = (gchar *) g_base64_decode (stanza->node->content, &len);
      DEBUG("Got digest-md5 challenge: %s", challenge);
      h = digest_md5_challenge_to_hash (challenge);
      g_free (challenge);
    }
  else
    {
      DEBUG ("Got empty challenge!");
    }


  if (h == NULL)
    {
      auth_failed (sasl, WOCKY_SASL_AUTH_ERROR_INVALID_REPLY,
        "Server send an invalid challenge");
      return;
    }

  switch (priv->state) {
    case WOCKY_SASL_AUTH_STATE_DIGEST_MD5_STARTED:
      digest_md5_send_initial_response (sasl, h);
      break;
    case WOCKY_SASL_AUTH_STATE_DIGEST_MD5_SENT_AUTH_RESPONSE:
      digest_md5_check_server_response (sasl, h);
      break;
    default:
      auth_failed (sasl, WOCKY_SASL_AUTH_ERROR_INVALID_REPLY,
          "Server send a challenge at the wrong time");
  }
  g_hash_table_destroy (h);
}

static void
digest_md5_handle_failure (WockySaslAuth *sasl, WockyXmppStanza *stanza)
{
  WockyXmppNode *reason = NULL;
  if (stanza->node->children != NULL)
    {
      /* TODO add a wocky xmpp node utility to either get the first child or
       * iterate the children list */
      reason = (WockyXmppNode *) stanza->node->children->data;
    }
  /* TODO Handle the different error cases in a different way. i.e.
   * make it clear for the user if it's credentials were wrong, if the server
   * just has a temporary error or if the authentication procedure itself was
   * at fault (too weak, invalid mech etc) */
  auth_failed (sasl, WOCKY_SASL_AUTH_ERROR_FAILURE,
      "Authentication failed: %s",
      reason == NULL ? "Unknown reason" : reason->name);
}

static void
digest_md5_handle_success (WockySaslAuth *sasl, WockyXmppStanza *stanza)
{
  WockySaslAuthPrivate *priv = WOCKY_SASL_AUTH_GET_PRIVATE (sasl);
  if (priv->state != WOCKY_SASL_AUTH_STATE_DIGEST_MD5_SENT_FINAL_RESPONSE)
    {
      auth_failed (sasl, WOCKY_SASL_AUTH_ERROR_INVALID_REPLY,
          "Server send success before finishing authentication");
      return;
    }

  auth_succeeded (sasl);
}


static void
plain_handle_challenge (WockySaslAuth *sasl, WockyXmppStanza *stanza)
{
  auth_failed (sasl, WOCKY_SASL_AUTH_ERROR_INVALID_REPLY,
      "Server send an unexpected challenge");
}

static void
plain_handle_success (WockySaslAuth *sasl, WockyXmppStanza *stanza)
{
  WockySaslAuthPrivate *priv = WOCKY_SASL_AUTH_GET_PRIVATE(sasl);
  if (priv->state != WOCKY_SASL_AUTH_STATE_PLAIN_STARTED)
    {
      auth_failed (sasl, WOCKY_SASL_AUTH_ERROR_INVALID_REPLY,
          "Server send success before finishing authentication");
      return;
    }
  auth_succeeded (sasl);
}

static void
plain_handle_failure (WockySaslAuth *sasl, WockyXmppStanza *stanza)
{
  WockyXmppNode *reason = NULL;

  if (stanza->node->children != NULL)
    {
      /* TODO add a wocky xmpp node utility to either get the first child or
       * iterate the children list */
      reason = (WockyXmppNode *) stanza->node->children->data;
    }
    /* TODO Handle the different error cases in a different way. i.e.
     * make it clear for the user if it's credentials were wrong, if the server
     * just has a temporary error or if the authentication procedure itself was
     * at fault (too weak, invalid mech etc) */

  auth_failed (sasl, WOCKY_SASL_AUTH_ERROR_FAILURE,
      "Authentication failed: %s",
      reason == NULL ? "Unknown reason" : reason->name);
}


#define HANDLE(x, y) { #x, y##_handle_##x }
#define HANDLERS(x) { HANDLE(challenge, x),  \
                      HANDLE(failure, x),     \
                      HANDLE(success, x),    \
                      { NULL, NULL }         \
                    }
static void
sasl_auth_stanza_received (GObject *source,
  GAsyncResult *res,
  gpointer user_data)
{
  WockySaslAuth *sasl = WOCKY_SASL_AUTH (user_data);
  WockySaslAuthPrivate *priv = WOCKY_SASL_AUTH_GET_PRIVATE (sasl);
  WockyXmppStanza *stanza;
  int i;
  struct {
    const gchar *name;
    void (*func)(WockySaslAuth *sasl, WockyXmppStanza *stanza);
  } handlers[WOCKY_SASL_AUTH_NR_MECHANISMS][4] = { HANDLERS(plain),
                                                   HANDLERS(digest_md5) };

  stanza = wocky_xmpp_connection_recv_stanza_finish (
    WOCKY_XMPP_CONNECTION (priv->connection), res, NULL);

  g_assert (stanza != NULL);

  if (strcmp (
      wocky_xmpp_node_get_ns (stanza->node), WOCKY_XMPP_NS_SASL_AUTH))
    {
      auth_failed (sasl, WOCKY_SASL_AUTH_ERROR_INVALID_REPLY,
          "Server send a reply not in the %s namespace",
          WOCKY_XMPP_NS_SASL_AUTH);
      return;
    }

  for (i = 0 ; handlers[priv->mech][i].name != NULL; i++)
    {
      if (!strcmp (stanza->node->name, handlers[priv->mech][i].name))
        {
          handlers[priv->mech][i].func (sasl, stanza);
          if (priv->state < WOCKY_SASL_AUTH_STATE_SUCCEEDED)
            {
              wocky_xmpp_connection_recv_stanza_async (priv->connection,
                  NULL, sasl_auth_stanza_received, sasl);
            }
          g_object_unref (stanza);
          return;
        }
    }

  auth_failed (sasl, WOCKY_SASL_AUTH_ERROR_INVALID_REPLY,
      "Server send an invalid reply (%s)",
      stanza->node->name);
}

static gboolean
wocky_sasl_auth_start_mechanism (WockySaslAuth *sasl,
    WockySaslAuthMechanism mech)
{
  WockyXmppStanza *stanza;
  WockySaslAuthPrivate *priv = WOCKY_SASL_AUTH_GET_PRIVATE(sasl);
  gboolean ret = TRUE;

  priv->mech = mech;

  stanza = wocky_xmpp_stanza_new ("auth");
  wocky_xmpp_node_set_ns (stanza->node, WOCKY_XMPP_NS_SASL_AUTH);
  switch (mech) {
    case WOCKY_SASL_AUTH_PLAIN:
      {
        GString *str = g_string_new ("");
        gchar *username = NULL, *password = NULL;
        gchar *cstr;

        g_signal_emit (sasl, signals[USERNAME_REQUESTED], 0, &username);
        g_signal_emit (sasl, signals[PASSWORD_REQUESTED], 0, &password);

        if (username == NULL || password == NULL)
          {
            auth_failed (sasl, WOCKY_SASL_AUTH_ERROR_NO_CREDENTIALS,
                      "No username or password provided");
            goto out;
          }

        DEBUG ("Got username and password");
        g_string_append_c (str, '\0');
        g_string_append (str, username);
        g_string_append_c (str, '\0');
        g_string_append (str, password);
        cstr = g_base64_encode ((guchar *) str->str, str->len);

        wocky_xmpp_node_set_attribute (stanza->node, "mechanism", "PLAIN");
        wocky_xmpp_node_set_content (stanza->node, cstr);

        g_string_free (str, TRUE);
        g_free (cstr);
        g_free (username);
        g_free (password);

        priv->state = WOCKY_SASL_AUTH_STATE_PLAIN_STARTED;
        break;
      }
    case WOCKY_SASL_AUTH_DIGEST_MD5:
      wocky_xmpp_node_set_attribute (stanza->node, "mechanism", "DIGEST-MD5");
      priv->state = WOCKY_SASL_AUTH_STATE_DIGEST_MD5_STARTED;
      break;
    default:
      g_assert_not_reached ();
  }

  /* FIXME handle send error */
  wocky_xmpp_connection_send_stanza_async (priv->connection, stanza,
    NULL, NULL, NULL);
  wocky_xmpp_connection_recv_stanza_async (priv->connection,
    NULL, sasl_auth_stanza_received, sasl);

out:
  g_object_unref (stanza);

  return ret;
}


gboolean
wocky_sasl_auth_authenticate_finish (WockySaslAuth *sasl,
  GAsyncResult *result,
  GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
      error))
    return FALSE;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
    G_OBJECT (sasl), wocky_sasl_auth_authenticate_finish), FALSE);

  auth_reset (sasl);

  return TRUE;
}

/* Initiate sasl auth. features should containt the stream features stanza as
 * receiver from the server */
void
wocky_sasl_auth_authenticate_async (WockySaslAuth *sasl,
    const gchar *server, WockyXmppConnection *connection,
    WockyXmppStanza *features, gboolean allow_plain,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  WockySaslAuthPrivate *priv = WOCKY_SASL_AUTH_GET_PRIVATE (sasl);
  WockyXmppNode *mech_node;
  GSList *mechanisms, *t;

  g_assert (sasl != NULL);
  g_assert (server != NULL);
  g_assert (connection != NULL);
  g_assert (features != NULL);

  mech_node = wocky_xmpp_node_get_child_ns (features->node, "mechanisms",
      WOCKY_XMPP_NS_SASL_AUTH);

  mechanisms = wocky_sasl_auth_mechanisms_to_list (mech_node);

  if (G_UNLIKELY (mechanisms == NULL))
    {
      g_simple_async_report_error_in_idle (G_OBJECT (sasl),
          callback, user_data,
          WOCKY_SASL_AUTH_ERROR, WOCKY_SASL_AUTH_ERROR_SASL_NOT_SUPPORTED,
          "Server doesn't have any sasl mechanisms");
      goto out;
    }

  priv->connection = g_object_ref (connection);
  priv->server = g_strdup (server);
  priv->result = g_simple_async_result_new (G_OBJECT (sasl),
    callback, user_data, wocky_sasl_auth_authenticate_finish);

  if (wocky_sasl_auth_has_mechanism (mechanisms, "DIGEST-MD5"))
    {
      DEBUG ("Choosing DIGEST-MD5 as auth mechanism");
      wocky_sasl_auth_start_mechanism (sasl, WOCKY_SASL_AUTH_DIGEST_MD5);
    }
  else if (allow_plain &&
      wocky_sasl_auth_has_mechanism (mechanisms, "PLAIN"))
    {
      DEBUG ("Choosing PLAIN as auth mechanism");
      wocky_sasl_auth_start_mechanism (sasl, WOCKY_SASL_AUTH_PLAIN);
    }
  else
    {
      DEBUG ("No supported mechanisms found");

      g_object_unref (priv->result);
      priv->result = NULL;

      g_simple_async_report_error_in_idle (G_OBJECT (sasl),
          callback, user_data,
          WOCKY_SASL_AUTH_ERROR,
          WOCKY_SASL_AUTH_ERROR_NO_SUPPORTED_MECHANISMS,
          "No supported mechanisms found");

      goto out;
    }

out:
  for (t = mechanisms ; t != NULL; t = g_slist_next (t))
    {
      g_free (t->data);
    }

  g_slist_free (mechanisms);
}
