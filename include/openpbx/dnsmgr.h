/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
 *
 * See http://www.openpbx.org for more information about
 * the OpenPBX project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*
 * Background DNS update manager
 */

#ifndef _OPENPBX_DNSMGR_H
#define _OPENPBX_DNSMGR_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <netinet/in.h>

struct opbx_dnsmgr_entry;

struct opbx_dnsmgr_entry *opbx_dnsmgr_get(const char *name, struct in_addr *result);

void opbx_dnsmgr_release(struct opbx_dnsmgr_entry *entry);

int opbx_dnsmgr_lookup(const char *name, struct in_addr *result, struct opbx_dnsmgr_entry **dnsmgr);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif /* c_plusplus */

#endif /* OPENPBX_DNSMGR_H */
