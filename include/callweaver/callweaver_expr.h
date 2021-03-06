/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.callweaver.org for more information about
 * the CallWeaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

#ifndef _CALLWEAVER_CALLWEAVER_EXPR_H
#define _CALLWEAVER_CALLWEAVER_EXPR_H

#include "callweaver/channel.h"
#include "callweaver/dynstr.h"


#if !defined (__P)
#  if defined (__STDC__) || defined (__GNUC__) || defined (__cplusplus)
#    define __P(protos) protos /* full-blown ANSI C */
#  else
#    define __P(protos) () /* traditional C preprocessor */
#  endif
#endif

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

extern CW_API_PUBLIC int cw_expr(struct cw_channel *chan, const char *expr, struct cw_dynstr *result);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
