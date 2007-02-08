
#ifndef __DEBUG_H__
#define __DEBUG_H_

#include "config.h"

#include <glib.h>

#ifdef ENABLE_DEBUG

G_BEGIN_DECLS

typedef enum
{
  DEBUG_NET           = 1 << 0,
  DEBUG_XMPP_READER   = 1 << 1,
  DEBUG_XMPP_WRITER   = 1 << 2,
} DebugFlags;

#define DEBUG_XMPP (DEBUG_XMPP_READER | DEBUG_XMPP_WRITER)

void wocky_debug_set_flags_from_env ();
void wocky_debug_set_flags (DebugFlags flags);
gboolean wocky_debug_flag_is_set (DebugFlags flag);
void wocky_debug (DebugFlags flag, const gchar *format, ...)
    G_GNUC_PRINTF (2, 3);

#ifdef DEBUG_FLAG

#define DEBUG(format, ...) \
  wocky_debug(DEBUG_FLAG, "%s: " format, G_STRFUNC, ##__VA_ARGS__)

#define DEBUGGING debug_flag_is_set(DEBUG_FLAG)

#endif /* DEBUG_FLAG */

#else /* ENABLE_DEBUG */

#ifdef DEBUG_FLAG

#define DEBUG(format, ...)

#define DEBUGGING 0

#define NODE_DEBUG(n, s)

#endif /* DEBUG_FLAG */

#endif /* ENABLE_DEBUG */

G_END_DECLS

#endif
