/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Digium, Inc.
 * Copyright (C) 2005, Olle E. Johansson, Edvina.net
 * Copyright (C) 2005, Russell Bryant <russelb@clemson.edu> 
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
 *
 * MD5 digest related dialplan functions
 * 
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "openpbx.h"

/* OPENPBX_FILE_VERSION(__FILE__, "$Revision: 1.5 $") */

#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/logger.h"
#include "openpbx/utils.h"
#include "openpbx/app.h"

static char *builtin_function_md5(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	char md5[33];

	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: MD5(<data>) - missing argument!\n");
		return NULL;
	}

	ast_md5_hash(md5, data);
	ast_copy_string(buf, md5, len);
	
	return buf;
}

static char *builtin_function_checkmd5(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	int argc;
	char *argv[2];
	char *args;
	char newmd5[33];

	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: CHECK_MD5(<digest>,<data>) - missing argument!\n");
		return NULL;
	}

	args = ast_strdupa(data);	
	argc = ast_separate_app_args(args, '|', argv, sizeof(argv) / sizeof(argv[0]));

	if (argc < 2) {
		ast_log(LOG_WARNING, "Syntax: CHECK_MD5(<digest>,<data>) - missing argument!\n");
		return NULL;
	}

	ast_md5_hash(newmd5, argv[1]);

	if (!strcasecmp(newmd5, argv[0]))	/* they match */
		ast_copy_string(buf, "1", len);
	else
		ast_copy_string(buf, "0", len);
	
	return buf;
}

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function md5_function = {
	.name = "MD5",
	.synopsis = "Computes an MD5 digest",
	.syntax = "MD5(<data>)",
	.read = builtin_function_md5,
};

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function checkmd5_function = {
	.name = "CHECK_MD5",
	.synopsis = "Checks an MD5 digest",
	.desc = "Returns 1 on a match, 0 otherwise\n",
	.syntax = "CHECK_MD5(<digest>,<data>)",
	.read = builtin_function_checkmd5,
};
