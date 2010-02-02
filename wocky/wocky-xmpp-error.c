/*
 * wocky-xmpp-error.c - Source for Wocky's XMPP error handling API
 * Copyright (C) 2006-2009 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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

#include "wocky-xmpp-error.h"

#include <stdlib.h>
#include <stdio.h>

#include "wocky-namespaces.h"
#include "wocky-utils.h"
#include "wocky-xmpp-error-enumtypes.h"

#define MAX_LEGACY_ERRORS 3

typedef struct {
    const gchar *description;
    WockyXmppErrorType type;
    const guint16 legacy_errors[MAX_LEGACY_ERRORS];
} XmppErrorSpec;

static const XmppErrorSpec xmpp_errors[NUM_WOCKY_XMPP_ERRORS] =
{
    /* undefined-condition */
    {
      "application-specific condition",
      WOCKY_XMPP_ERROR_TYPE_CANCEL,
      { 500, 0, },
    },

    /* redirect */
    {
      "the recipient or server is redirecting requests for this information "
      "to another entity",
      WOCKY_XMPP_ERROR_TYPE_MODIFY,
      { 302, 0, },
    },

    /* gone */
    {
      "the recipient or server can no longer be contacted at this address",
      WOCKY_XMPP_ERROR_TYPE_MODIFY,
      { 302, 0, },
    },

    /* bad-request */
    {
      "the sender has sent XML that is malformed or that cannot be processed",
      WOCKY_XMPP_ERROR_TYPE_MODIFY,
      { 400, 0, },
    },

    /* unexpected-request */
    {
      "the recipient or server understood the request but was not expecting "
      "it at this time",
      WOCKY_XMPP_ERROR_TYPE_WAIT,
      { 400, 0, },
    },

    /* jid-malformed */
    {
      "the sending entity has provided or communicated an XMPP address or "
      "aspect thereof (e.g., a resource identifier) that does not adhere "
      "to the syntax defined in Addressing Scheme (Section 3)",
      WOCKY_XMPP_ERROR_TYPE_MODIFY,
      { 400, 0, },
    },

    /* not-authorized */
    {
      "the sender must provide proper credentials before being allowed to "
      "perform the action, or has provided improper credentials",
      WOCKY_XMPP_ERROR_TYPE_AUTH,
      { 401, 0, },
    },

    /* payment-required */
    {
      "the requesting entity is not authorized to access the requested "
      "service because payment is required",
      WOCKY_XMPP_ERROR_TYPE_AUTH,
      { 402, 0, },
    },

    /* forbidden */
    {
      "the requesting entity does not possess the required permissions to "
      "perform the action",
      WOCKY_XMPP_ERROR_TYPE_AUTH,
      { 403, 0, },
    },

    /* item-not-found */
    {
      "the addressed JID or item requested cannot be found",
      WOCKY_XMPP_ERROR_TYPE_CANCEL,
      { 404, 0, },
    },

    /* recipient-unavailable */
    {
      "the intended recipient is temporarily unavailable",
      WOCKY_XMPP_ERROR_TYPE_WAIT,
      { 404, 0, },
    },

    /* remote-server-not-found */
    {
      "a remote server or service specified as part or all of the JID of the "
      "intended recipient (or required to fulfill a request) could not be "
      "contacted within a reasonable amount of time",
      WOCKY_XMPP_ERROR_TYPE_CANCEL,
      { 404, 0, },
    },

    /* not-allowed */
    {
      "the recipient or server does not allow any entity to perform the action",
      WOCKY_XMPP_ERROR_TYPE_CANCEL,
      { 405, 0, },
    },

    /* not-acceptable */
    {
      "the recipient or server understands the request but is refusing to "
      "process it because it does not meet criteria defined by the recipient "
      "or server (e.g., a local policy regarding acceptable words in messages)",
      WOCKY_XMPP_ERROR_TYPE_MODIFY,
      { 406, 0, },
    },

    /* registration-required */
    {
      "the requesting entity is not authorized to access the requested service "
      "because registration is required",
      WOCKY_XMPP_ERROR_TYPE_AUTH,
      { 407, 0, },
    },
    /* subscription-required */
    {
      "the requesting entity is not authorized to access the requested service "
      "because a subscription is required",
      WOCKY_XMPP_ERROR_TYPE_AUTH,
      { 407, 0, },
    },

    /* remote-server-timeout */
    {
      "a remote server or service specified as part or all of the JID of the "
      "intended recipient (or required to fulfill a request) could not be "
      "contacted within a reasonable amount of time",
      WOCKY_XMPP_ERROR_TYPE_WAIT,
      { 408, 504, 0, },
    },

    /* conflict */
    {
      "access cannot be granted because an existing resource or session exists "
      "with the same name or address",
      WOCKY_XMPP_ERROR_TYPE_CANCEL,
      { 409, 0, },
    },

    /* internal-server-error */
    {
      "the server could not process the stanza because of a misconfiguration "
      "or an otherwise-undefined internal server error",
      WOCKY_XMPP_ERROR_TYPE_WAIT,
      { 500, 0, },
    },

    /* resource-constraint */
    {
      "the server or recipient lacks the system resources necessary to service "
      "the request",
      WOCKY_XMPP_ERROR_TYPE_WAIT,
      { 500, 0, },
    },

    /* feature-not-implemented */
    {
      "the feature requested is not implemented by the recipient or server and "
      "therefore cannot be processed",
      WOCKY_XMPP_ERROR_TYPE_CANCEL,
      { 501, 0, },
    },

    /* service-unavailable */
    {
      "the server or recipient does not currently provide the requested "
      "service",
      WOCKY_XMPP_ERROR_TYPE_CANCEL,
      { 502, 503, 510, },
    },
};

static GList *error_domains = NULL;

GQuark
wocky_xmpp_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string (WOCKY_XMPP_NS_STANZAS);

  return quark;
}

void
wocky_xmpp_error_register_domain (WockyXmppErrorDomain *domain)
{
  error_domains = g_list_prepend (error_domains, domain);
}

static WockyXmppErrorDomain *
xmpp_error_find_domain (GQuark domain)
{
  GList *l;

  for (l = error_domains; l != NULL; l = l->next)
    {
      WockyXmppErrorDomain *d = l->data;

      if (d->domain == domain)
        return d;
    }

  return NULL;
}

GQuark
wocky_jingle_error_quark (void)
{
  static GQuark quark = 0;

  if (quark == 0)
    quark = g_quark_from_static_string (WOCKY_XMPP_NS_JINGLE_ERRORS);

  return quark;
}

static WockyXmppErrorDomain *
jingle_error_get_domain (void)
{
  static WockyXmppErrorSpecialization codes[] = {
      /* out-of-order */
      { "The request cannot occur at this point in the state machine (e.g., "
        "session-initiate after session-accept).",
        WOCKY_XMPP_ERROR_UNEXPECTED_REQUEST,
        FALSE
      },

      /* tie-break */
      { "The request is rejected because it was sent while the initiator was "
        "awaiting a reply on a similar request.",
        WOCKY_XMPP_ERROR_CONFLICT,
        FALSE
      },

      /* unknown-session */
      { "The 'sid' attribute specifies a session that is unknown to the "
        "recipient (e.g., no longer live according to the recipient's state "
        "machine because the recipient previously terminated the session).",
        WOCKY_XMPP_ERROR_ITEM_NOT_FOUND,
        FALSE
      },

      /* unsupported-info */
      { "The recipient does not support the informational payload of a "
        "session-info action.",
        WOCKY_XMPP_ERROR_FEATURE_NOT_IMPLEMENTED,
        FALSE
      }
  };
  static WockyXmppErrorDomain jingle_errors = { 0, };

  if (G_UNLIKELY (jingle_errors.domain == 0))
    {
      jingle_errors.domain = WOCKY_JINGLE_ERROR;
      jingle_errors.enum_type = WOCKY_TYPE_JINGLE_ERROR;
      jingle_errors.codes = codes;
    }

  return &jingle_errors;
}

GQuark
wocky_si_error_quark (void)
{
  static GQuark quark = 0;

  if (quark == 0)
    quark = g_quark_from_static_string (WOCKY_XMPP_NS_SI);

  return quark;
}

static WockyXmppErrorDomain *
si_error_get_domain (void)
{
  static WockyXmppErrorSpecialization codes[] = {
      /* no-valid-streams */
      { "None of the available streams are acceptable.",
        WOCKY_XMPP_ERROR_BAD_REQUEST,
        TRUE,
        WOCKY_XMPP_ERROR_TYPE_CANCEL
      },

      /* bad-profile */
      { "The profile is not understood or invalid. The profile MAY supply a "
        "profile-specific error condition.",
        WOCKY_XMPP_ERROR_BAD_REQUEST,
        TRUE,
        WOCKY_XMPP_ERROR_TYPE_MODIFY
      }
  };
  static WockyXmppErrorDomain si_errors = { 0, };

  if (G_UNLIKELY (si_errors.domain == 0))
    {
      si_errors.domain = WOCKY_SI_ERROR;
      si_errors.enum_type = WOCKY_TYPE_SI_ERROR;
      si_errors.codes = codes;
    }

  return &si_errors;
}

/* Static, but bears documenting.
 *
 * xmpp_error_from_node_for_ns:
 * @node: a node believed to contain an error child
 * @ns: the namespace for errors corresponding to @enum_type
 * @enum_type: a GEnum of error codes
 * @code: location at which to store an error code
 *
 * Scans @node's children for nodes in @ns whose name corresponds to a nickname
 * of a value of @enum_type, storing the value in @code if found.
 *
 * Returns: %TRUE if an error code was retrieved.
 */
static gboolean
xmpp_error_from_node_for_ns (
    WockyXmppNode *node,
    GQuark ns,
    GType enum_type,
    gint *code)
{
  GSList *l;

  for (l = node->children; l != NULL; l = l->next)
    {
      WockyXmppNode *child = l->data;

      if (wocky_xmpp_node_has_ns_q (child, ns) &&
          wocky_enum_from_nick (enum_type, child->name, code))
        return TRUE;
    }

  return FALSE;
}

static WockyXmppError
xmpp_error_from_code (WockyXmppNode *error_node,
    WockyXmppErrorType *type)
{
  const gchar *code = wocky_xmpp_node_get_attribute (error_node, "code");
  gint error_code, i, j;

  if (code == NULL)
    goto out;

  error_code = atoi (code);

  /* skip UNDEFINED_CONDITION, we want code 500 to be translated
   * to INTERNAL_SERVER_ERROR */
  for (i = 1; i < NUM_WOCKY_XMPP_ERRORS; i++)
    {
      const XmppErrorSpec *spec = &xmpp_errors[i];

      for (j = 0; j < MAX_LEGACY_ERRORS; j++)
        {
          gint cur_code = spec->legacy_errors[j];
          if (cur_code == 0)
            break;

          if (cur_code == error_code)
            {
              if (type != NULL)
                *type = spec->type;

              return i;
            }
        }
    }

out:
  if (type != NULL)
    *type = WOCKY_XMPP_ERROR_TYPE_CANCEL;

  return WOCKY_XMPP_ERROR_UNDEFINED_CONDITION;
}

WockyXmppError
wocky_xmpp_error_from_node (WockyXmppNode *error_node)
{
  gint code;

  g_return_val_if_fail (error_node != NULL,
      WOCKY_XMPP_ERROR_UNDEFINED_CONDITION);

  /* First, try to look it up the modern way */
  if (xmpp_error_from_node_for_ns (error_node, WOCKY_XMPP_ERROR,
          WOCKY_TYPE_XMPP_ERROR, &code))
    return code;

  /* Ok, do it the legacy way */
  return xmpp_error_from_code (error_node, NULL);
}

/**
 * wocky_xmpp_node_unpack_error:
 *
 * @node: a #WockyXmppNode
 * @type: gchar ** into which to write the XMPP Stanza error type
 * @text: #WockyXmppNode ** to hold the node containing the error description
 * @orig: #WockyXmppNode ** to hold the original XMPP Stanza that triggered
 *        the error: XMPP does not require this to be provided in the error
 * @extra: #WockyXmppNode ** to hold any extra domain-specific XML tags
 *         for the error received.
 * @errnum: #WockyXmppError * to hold the value mapping to the error condition
 *
 * Given an XMPP Stanza error #WockyXmppNode see RFC 3920) this function
 * extracts useful error info.
 *
 * The above parameters are all optional, pass NULL to ignore them.
 *
 * The above data are all optional in XMPP, except for @type, which
 * the XMPP spec requires in all stanza errors. See RFC 3920 [9.3.2].
 *
 * None of the above parameters need be freed, they are owned by the
 * parent #WockyXmppNode @node.
 *
 * Returns: a const gchar * indicating the error condition
 */

const gchar *
wocky_xmpp_error_unpack_node (WockyXmppNode *node,
    WockyXmppErrorType *type,
    WockyXmppNode **text,
    WockyXmppNode **orig,
    WockyXmppNode **extra,
    WockyXmppError *errnum)
{
  WockyXmppNode *error = NULL;
  WockyXmppNode *mesg = NULL;
  WockyXmppNode *xtra = NULL;
  const gchar *cond = NULL;
  GSList *child = NULL;
  GQuark stanza = g_quark_from_string (WOCKY_XMPP_NS_STANZAS);

  g_assert (node != NULL);

  error = wocky_xmpp_node_get_child (node, "error");

  /* not an error? weird, in any case */
  if (error == NULL)
    return NULL;

  /* The type='' attributes being present and one of the defined five is a
   * MUST; if the other party is getting XMPP *that* wrong, 'cancel' seems like
   * a sensible default.
   */
  if (type != NULL)
    {
      const gchar *type_attr = wocky_xmpp_node_get_attribute (error, "type");
      gint type_i = WOCKY_XMPP_ERROR_TYPE_CANCEL;

      if (type_attr != NULL)
        wocky_enum_from_nick (WOCKY_TYPE_XMPP_ERROR_TYPE, type_attr, &type_i);

      *type = type_i;
    }

  for (child = error->children; child != NULL; child = g_slist_next (child))
    {
      WockyXmppNode *c = child->data;
      if (c->ns != stanza)
        xtra = c;
      else if (wocky_strdiff (c->name, "text"))
        {
          cond = c->name;
        }
      else
        mesg = c;
    }

  if (text != NULL)
    *text = mesg;

  if (extra != NULL)
    *extra = xtra;

  if (orig != NULL)
    {
      WockyXmppNode *first = wocky_xmpp_node_get_first_child (node);
      if (first != error)
        *orig = first;
      else
        *orig = NULL;
    }

  if (errnum != NULL)
    *errnum = wocky_xmpp_error_from_node (error);

  return cond;
}

/*
 * See RFC 3920: 4.7 Stream Errors, 9.3 Stanza Errors.
 */
WockyXmppNode *
wocky_xmpp_error_to_node (WockyXmppError error,
    WockyXmppNode *parent_node,
    const gchar *errmsg)
{
  GError e = { WOCKY_XMPP_ERROR, error, (gchar *) errmsg };

  g_return_val_if_fail (error != WOCKY_XMPP_ERROR_UNDEFINED_CONDITION &&
      error < NUM_WOCKY_XMPP_ERRORS, NULL);

  return wocky_stanza_error_to_node (&e, parent_node);
}

/**
 * wocky_g_error_to_node:
 * @error: an error in the domain #WOCKY_XMPP_ERROR, or in an
 *         application-specific domain registered with
 *         wocky_xmpp_error_register_domain()
 * @parent_node: the node to which to add an error (such as an IQ error)
 *
 * Adds an <error/> node to a stanza corresponding to the error described by
 * @error. If @error is in a domain other than #WOCKY_XMPP_ERROR, both the
 * application-specific error name and the error from #WOCKY_XMPP_ERROR will be
 * created. See RFC 3902 (XMPP Core) §9.3, “Stanza Errors”.
 *
 * There is currently no way to override the type='' of an XMPP Core stanza
 * error without creating an application-specific error code which does so.
 *
 * Returns: the newly-created <error/> node
 */
WockyXmppNode *
wocky_stanza_error_to_node (const GError *error,
    WockyXmppNode *parent_node)
{
  WockyXmppNode *error_node, *node;
  WockyXmppErrorDomain *domain = NULL;
  WockyXmppError core_error;
  const XmppErrorSpec *spec;
  WockyXmppErrorType type;
  gchar str[6];

  g_return_val_if_fail (parent_node != NULL, NULL);

  error_node = wocky_xmpp_node_add_child (parent_node, "error");

  g_return_val_if_fail (error != NULL, error_node);

  if (error->domain == WOCKY_XMPP_ERROR)
    {
      core_error = error->code;
      spec = &(xmpp_errors[core_error]);
      type = spec->type;
    }
  else
    {
      WockyXmppErrorSpecialization *s;

      domain = xmpp_error_find_domain (error->domain);
      g_return_val_if_fail (domain != NULL, error_node);

      /* This will crash if you mess up and pass a code that's not in the
       * domain. */
      s = &(domain->codes[error->code]);
      core_error = s->specializes;
      spec = &(xmpp_errors[core_error]);

      if (s->override_type)
        type = s->type;
      else
        type = spec->type;
    }

  sprintf (str, "%d", spec->legacy_errors[0]);
  wocky_xmpp_node_set_attribute (error_node, "code", str);

  wocky_xmpp_node_set_attribute (error_node, "type",
      wocky_enum_to_nick (WOCKY_TYPE_XMPP_ERROR_TYPE, spec->type));

  node = wocky_xmpp_node_add_child (error_node,
      wocky_enum_to_nick (WOCKY_TYPE_XMPP_ERROR, core_error));
  wocky_xmpp_node_set_ns (node, WOCKY_XMPP_NS_STANZAS);

  if (domain != NULL)
    {
      const gchar *name = wocky_enum_to_nick (domain->enum_type, error->code);

      node = wocky_xmpp_node_add_child (error_node, name);
      wocky_xmpp_node_set_ns_q (node, domain->domain);
    }

  if (error->message != NULL && *error->message != '\0')
    {
      node = wocky_xmpp_node_add_child (error_node, "text");
      wocky_xmpp_node_set_content (node, error->message);
    }

  return error_node;
}

const gchar *
wocky_xmpp_error_string (WockyXmppError error)
{
  return wocky_enum_to_nick (WOCKY_TYPE_XMPP_ERROR, error);
}

const gchar *
wocky_xmpp_error_description (WockyXmppError error)
{
  if (error < NUM_WOCKY_XMPP_ERRORS)
    return xmpp_errors[error].description;
  else
    return NULL;
}

/**
 * wocky_xmpp_stream_error_quark
 *
 * Get the error quark used for stream errors
 *
 * Returns: the quark for stream errors.
 */
GQuark
wocky_xmpp_stream_error_quark (void)
{
  static GQuark quark = 0;

  if (quark == 0)
    quark = g_quark_from_static_string (WOCKY_XMPP_NS_STREAMS);

  return quark;
}

WockyXmppStreamError
wocky_xmpp_stream_error_from_node (WockyXmppNode *node)
{
  gint code;

  if (xmpp_error_from_node_for_ns (node, WOCKY_XMPP_STREAM_ERROR,
          WOCKY_TYPE_XMPP_STREAM_ERROR, &code))
    return code;
  else
    return WOCKY_XMPP_STREAM_ERROR_UNKNOWN;
}

void
wocky_xmpp_error_init ()
{
  if (error_domains == NULL)
    {
      /* Register standard domains */
      wocky_xmpp_error_register_domain (jingle_error_get_domain ());
      wocky_xmpp_error_register_domain (si_error_get_domain ());
    }
}

void
wocky_xmpp_error_deinit ()
{
  g_list_free (error_domains);
  error_domains = NULL;
}
