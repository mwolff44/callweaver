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

/*
 * Persistant data storage (akin to *doze registry)
 */

#ifndef _CALLWEAVER_CALLWEAVER_DB_H
#define _CALLWEAVER_CALLWEAVER_DB_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

struct opbx_db_entry {
	struct opbx_db_entry *next;
	char *key;
	char data[0];
};

int opbx_db_get(const char *family, const char *key, char *out, int outlen);

int opbx_db_put(const char *family, const char *key, char *value);

int opbx_db_del(const char *family, const char *key);

int opbx_db_deltree(const char *family, const char *keytree);

int opbx_db_deltree_with_value(const char *family, const char *keytree, const char *value);

struct opbx_db_entry *opbx_db_gettree(const char *family, const char *keytree);

void opbx_db_freetree(struct opbx_db_entry *entry);


#define opbx_db_mprintf sqlite3_mprintf
#define opbx_db_free sqlite3_free


#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
