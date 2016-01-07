/*
 * wocky-sm.h - Header for WockySM (Stream Management)
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
#if !defined (WOCKY_H_INSIDE) && !defined (WOCKY_COMPILATION)
# error "Only <wocky/wocky.h> can be included directly."
#endif

#ifndef __WOCKY_SM_H__
#define __WOCKY_SM_H__

#include <glib-object.h>

#include "wocky-types.h"
#include "wocky-c2s-porter.h"

G_BEGIN_DECLS

typedef struct _WockySM WockySM;

/**
 * WockySMClass:
 *
 * The class of a #WockySM.
 */
typedef struct _WockySMClass WockySMClass;
typedef struct _WockySMPrivate WockySMPrivate;

GQuark wocky_sm_error_quark (void);

struct _WockySMClass {
  /*<private>*/
  GObjectClass parent_class;
};

struct _WockySM {
  /*<private>*/
  GObject parent;

  WockySMPrivate *priv;
};

GType wocky_sm_get_type (void);

#define WOCKY_TYPE_SM \
  (wocky_sm_get_type ())
#define WOCKY_SM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), WOCKY_TYPE_SM, \
   WockySM))
#define WOCKY_SM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), WOCKY_TYPE_SM, \
   WockySMClass))
#define WOCKY_IS_SM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), WOCKY_TYPE_SM))
#define WOCKY_IS_SM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), WOCKY_TYPE_SM))
#define WOCKY_SM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), WOCKY_TYPE_SM, \
   WockySMClass))

WockySM * wocky_sm_new (WockyC2SPorter *porter);
void wocky_sm_send_a (WockyPorter* porter, uint recv_count);
void wocky_sm_send_r (WockyPorter* porter, uint sent_count);
void wocky_sm_request_for_stanza (WockySM *self, WockyStanza *stanza);
gboolean wocky_sm_is_unacked_stanza (WockySM *self);
WockyStanza * wocky_sm_pop_unacked_stanza (WockySM *self);

G_END_DECLS

#endif /* #ifndef __WOCKY_SM_H__ */
