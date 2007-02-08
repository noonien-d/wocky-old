/*
 * wocky-xmpp-reader.c - Source for WockyXmppReader
 * Copyright (C) 2006 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd@luon.net>
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

#include <libxml/parser.h>

#include "wocky-xmpp-reader.h"
#include "wocky-xmpp-reader-signals-marshal.h"

#include "wocky-xmpp-stanza.h"

#define XMPP_STREAM_NAMESPACE "http://etherx.jabber.org/streams"

#define DEBUG_FLAG DEBUG_XMPP_READER
#include "wocky-debug.h"

G_DEFINE_TYPE(WockyXmppReader, wocky_xmpp_reader, G_TYPE_OBJECT)

/* signal enum */
enum {
  RECEIVED_STANZA,
  STREAM_OPENED,
  STREAM_CLOSED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* Parser prototypes */
static void _start_element_ns(void *user_data,
                              const xmlChar *localname,
                              const xmlChar *prefix,
                              const xmlChar *uri,
                              int nb_namespaces,
                              const xmlChar **namespaces,
                              int nb_attributes,
                              int nb_defaulted,
                              const xmlChar **attributes);

static void _end_element_ns(void *user_data, const xmlChar *localname,
                            const xmlChar *prefix, const xmlChar *URI);

static void _characters (void *user_data, const xmlChar *ch, int len);

static void _error(void *user_data, xmlErrorPtr error);

static xmlSAXHandler parser_handler = {
  .initialized = XML_SAX2_MAGIC,
  .startElementNs = _start_element_ns,
  .endElementNs   = _end_element_ns,
  .characters     = _characters,
  .serror         = _error,
};

/* private structure */
typedef struct _WockyXmppReaderPrivate WockyXmppReaderPrivate;

struct _WockyXmppReaderPrivate
{
  xmlParserCtxtPtr parser;
  guint depth;
  WockyXmppStanza *stanza;
  WockyXmppNode *node;
  GQueue *nodes;
  gboolean dispose_has_run;
  gboolean error;
  gboolean stream_mode;
};

#define WOCKY_XMPP_READER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), WOCKY_TYPE_XMPP_READER, WockyXmppReaderPrivate))


static void
wocky_init_xml_parser(WockyXmppReader *obj) {
  WockyXmppReaderPrivate *priv = WOCKY_XMPP_READER_GET_PRIVATE (obj);

  xmlFreeParserCtxt(priv->parser);
  priv->parser = xmlCreatePushParserCtxt(&parser_handler, obj, NULL, 0, NULL);
  xmlCtxtUseOptions(priv->parser, XML_PARSE_NOENT);
}

static void
wocky_xmpp_reader_init (WockyXmppReader *obj)
{
  WockyXmppReaderPrivate *priv = WOCKY_XMPP_READER_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  wocky_init_xml_parser(obj);

  priv->depth = 0;
  priv->stanza = NULL;
  priv->nodes = g_queue_new();
  priv->node = NULL;
  priv->error = FALSE;
  priv->stream_mode = TRUE;
}

static void wocky_xmpp_reader_dispose (GObject *object);
static void wocky_xmpp_reader_finalize (GObject *object);

static void
wocky_xmpp_reader_class_init (WockyXmppReaderClass *wocky_xmpp_reader_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (wocky_xmpp_reader_class);

  g_type_class_add_private (wocky_xmpp_reader_class, sizeof (WockyXmppReaderPrivate));

  object_class->dispose = wocky_xmpp_reader_dispose;
  object_class->finalize = wocky_xmpp_reader_finalize;

  signals[RECEIVED_STANZA] = 
    g_signal_new("received-stanza", 
                 G_OBJECT_CLASS_TYPE(wocky_xmpp_reader_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__OBJECT,
                 G_TYPE_NONE, 1, WOCKY_TYPE_XMPP_STANZA);
  signals[STREAM_OPENED] = 
    g_signal_new("stream-opened", 
                 G_OBJECT_CLASS_TYPE(wocky_xmpp_reader_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 wocky_xmpp_reader_marshal_VOID__STRING_STRING_STRING,
                 G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
  signals[STREAM_CLOSED] = 
    g_signal_new("stream-closed", 
                 G_OBJECT_CLASS_TYPE(wocky_xmpp_reader_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);
}

void
wocky_xmpp_reader_dispose (GObject *object)
{
  WockyXmppReader *self = WOCKY_XMPP_READER (object);
  WockyXmppReaderPrivate *priv = WOCKY_XMPP_READER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (wocky_xmpp_reader_parent_class)->dispose)
    G_OBJECT_CLASS (wocky_xmpp_reader_parent_class)->dispose (object);
}

void
wocky_xmpp_reader_finalize (GObject *object)
{
  WockyXmppReader *self = WOCKY_XMPP_READER (object);
  WockyXmppReaderPrivate *priv = WOCKY_XMPP_READER_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  if (priv->parser != NULL) {
    xmlFreeParserCtxt(priv->parser);
    priv->parser = NULL;
  }

  G_OBJECT_CLASS (wocky_xmpp_reader_parent_class)->finalize (object);
}


WockyXmppReader * 
wocky_xmpp_reader_new(void) {
  return g_object_new(WOCKY_TYPE_XMPP_READER, NULL);
}

WockyXmppReader *
wocky_xmpp_reader_new_no_stream(void) {
  WockyXmppReader *result = g_object_new(WOCKY_TYPE_XMPP_READER, NULL);
  WockyXmppReaderPrivate *priv = WOCKY_XMPP_READER_GET_PRIVATE (result);

  priv->stream_mode = FALSE;

  return result;
}

static void _start_element_ns(void *user_data,
                             const xmlChar *localname,
                             const xmlChar *prefix,
                             const xmlChar *uri,
                             int nb_namespaces,
                             const xmlChar **namespaces,
                             int nb_attributes,
                             int nb_defaulted,
                             const xmlChar **attributes) {
  WockyXmppReader *self = WOCKY_XMPP_READER (user_data);
  WockyXmppReaderPrivate *priv = WOCKY_XMPP_READER_GET_PRIVATE (self);
  int i;

  if (prefix) {
    DEBUG("Element %s:%s started, depth %d", prefix, localname, priv->depth);
  } else {
    DEBUG("Element %s started, depth %d", localname, priv->depth);
  }

  if (priv->stream_mode && G_UNLIKELY(priv->depth == 0)) {
    gchar *to = NULL;
    gchar *from = NULL;
    gchar *version = NULL;

    if (strcmp("stream", (gchar *)localname)
         || strcmp(XMPP_STREAM_NAMESPACE, (gchar *)uri)) {
      priv->error = TRUE;
      g_assert_not_reached();
      return;
    }
    for (i = 0; i < nb_attributes * 5; i+=5) {
      if (!strcmp((gchar *)attributes[i], "to")) {
        to = g_strndup((gchar *)attributes[i+3],
                         (gsize) (attributes[i+4] - attributes[i+3]));
      }
      if (!strcmp((gchar *)attributes[i], "from")) {
        from = g_strndup((gchar *)attributes[i+3],
                         (gsize) (attributes[i+4] - attributes[i+3]));
      }
      if (!strcmp((gchar *)attributes[i], "version")) {
        version = g_strndup((gchar *)attributes[i+3],
                         (gsize) (attributes[i+4] - attributes[i+3]));
      }
    }
    g_signal_emit(self, signals[STREAM_OPENED], 0, to, from, version);
    priv->depth++;
    return;
  } 

  if (priv->stanza == NULL) {
    priv->stanza = wocky_xmpp_stanza_new((gchar *)localname);
    priv->node = priv->stanza->node;
  } else {
    g_queue_push_tail(priv->nodes, priv->node);
    priv->node = wocky_xmpp_node_add_child(priv->node, (gchar *)localname);
  }
  wocky_xmpp_node_set_ns(priv->node, (gchar *)uri);

  for (i = 0; i < nb_attributes * 5; i+=5) {
    /* Node is localname, prefix, uri, valuestart, valueend */
    if (attributes[i+1] != NULL
        && !strcmp((gchar *)attributes[i+1], "xml") 
        && !strcmp((gchar *)attributes[i], "lang")) {
      wocky_xmpp_node_set_language_n(priv->node, 
                                   (gchar *)attributes[i+3],
                                   (gsize) (attributes[i+4] - attributes[i+3]));
    } else {
      wocky_xmpp_node_set_attribute_n_ns(priv->node, 
                                   (gchar *)attributes[i], 
                                   (gchar *)attributes[i+3],
                                   (gsize)(attributes[i+4] - attributes[i+3]),
                                   (gchar *)attributes[i+2]);
    }
  }
  priv->depth++;
}

static void 
_characters (void *user_data, const xmlChar *ch, int len) {
  WockyXmppReader *self = WOCKY_XMPP_READER (user_data);
  WockyXmppReaderPrivate *priv = WOCKY_XMPP_READER_GET_PRIVATE (self);

  if (priv->node != NULL) { 
    wocky_xmpp_node_append_content_n(priv->node, (const gchar *)ch, (gsize)len);
  }
}

static void 
_end_element_ns(void *user_data, const xmlChar *localname, 
                const xmlChar *prefix, const xmlChar *uri) {
  WockyXmppReader *self = WOCKY_XMPP_READER (user_data);
  WockyXmppReaderPrivate *priv = WOCKY_XMPP_READER_GET_PRIVATE (self);

  priv->depth--;

  if (prefix) {
    DEBUG("Element %s:%s ended, depth %d", prefix, localname, priv->depth);
  } else {
    DEBUG("Element %s ended, depth %d", localname, priv->depth);
  }

  if (priv->node && priv->node->content) {
    /* Remove content if it's purely whitespace */
    const char *c;
    for (c = priv->node->content;*c != '\0' && g_ascii_isspace(*c); c++) 
      ;
    if (*c == '\0') 
      wocky_xmpp_node_set_content(priv->node, NULL);
  }

  if (priv->stream_mode && priv->depth == 0) {
    g_signal_emit(self, signals[STREAM_CLOSED], 0);
  } else if (priv->depth == (priv->stream_mode ? 1 : 0) ) {
    g_assert(g_queue_get_length(priv->nodes) == 0);
    g_signal_emit(self, signals[RECEIVED_STANZA], 0, priv->stanza);
    g_object_unref(priv->stanza);
    priv->stanza = NULL;
    priv->node = NULL;
  } else {
    priv->node = (WockyXmppNode *)g_queue_pop_tail(priv->nodes);
  }
}

static void 
_error(void *user_data, xmlErrorPtr error) {
  WockyXmppReader *self = WOCKY_XMPP_READER (user_data);
  WockyXmppReaderPrivate *priv = WOCKY_XMPP_READER_GET_PRIVATE (self);
  priv->error = TRUE;

  DEBUG("Parsing failed %s", error->message);
}

gboolean 
wocky_xmpp_reader_push(WockyXmppReader *reader, 
                       const guint8 *data, gsize length,
                       GError **error) {
  WockyXmppReaderPrivate *priv = WOCKY_XMPP_READER_GET_PRIVATE (reader);

  g_assert(!priv->error);
  DEBUG("Parsing chunk: %.*s", length, data);

  xmlParseChunk(priv->parser, (const char*)data, length, FALSE);

 if (!priv->stream_mode) {
    wocky_init_xml_parser(reader);
  }
  return !priv->error;
}
