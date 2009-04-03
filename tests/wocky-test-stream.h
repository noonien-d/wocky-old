/*
 * wocky-test-stream.h - Header for WockyTestStream
 * Copyright (C) 2009 Collabora Ltd.
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

#ifndef __WOCKY_TEST_STREAM_H__
#define __WOCKY_TEST_STREAM_H__

#include <glib-object.h>
#include <gio/gnio.h>

G_BEGIN_DECLS

typedef struct _WockyTestStream WockyTestStream;
typedef struct _WockyTestStreamClass WockyTestStreamClass;
typedef struct _WockyTestStreamPrivate WockyTestStreamPrivate;

struct _WockyTestStreamClass {
    GObjectClass parent_class;
};

struct _WockyTestStream {
    GObject parent;
    GIOStream *stream0;
    GIOStream *stream1;

    GInputStream *stream0_input;
    GOutputStream *stream0_output;

    GInputStream *stream1_input;
    GOutputStream *stream1_output;

    WockyTestStreamPrivate *priv;
};

GType wocky_test_stream_get_type(void);

/* TYPE MACROS */
#define WOCKY_TYPE_TEST_STREAM \
  (wocky_test_stream_get_type())
#define WOCKY_TEST_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), WOCKY_TYPE_TEST_STREAM, WockyTestStream))
#define WOCKY_TEST_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), WOCKY_TYPE_TEST_STREAM, WockyTestStreamClass))
#define WOCKY_IS_TEST_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), WOCKY_TYPE_TEST_STREAM))
#define WOCKY_IS_TEST_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), WOCKY_TYPE_TEST_STREAM))
#define WOCKY_TEST_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), WOCKY_TYPE_TEST_STREAM, WockyTestStreamClass))

G_END_DECLS

#endif /* #ifndef __WOCKY_TEST_STREAM_H__*/