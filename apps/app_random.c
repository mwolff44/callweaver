/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (c) 2003 - 2005 Tilghman Lesher.  All rights reserved.
 *
 * Tilghman Lesher <openpbx__app_random__200508@the-tilghman.com>
 *
 * This code is released by the author with no restrictions on usage or distribution.
 *
 * See http://www.openpbx.org for more information about
 * the OpenPBX project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*
 *
 * Random application
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision: 1.8 $")

#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/options.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/module.h"

static char *tdesc = "Random goto";

static char *app_random = "Random";

static char *random_synopsis = "Conditionally branches, based upon a probability";

static char *random_descrip =
"Random([probability]:[[context|]extension|]priority)\n"
"  probability := INTEGER in the range 1 to 100\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static char random_state[256];

static int random_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;

	char *s;
	char *prob;
	int probint;

	if (!data) {
		ast_log(LOG_WARNING, "Random requires an argument ([probability]:[[context|]extension|]priority)\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	s = ast_strdupa((void *) data);

	prob = strsep(&s,":");
	if ((!prob) || (sscanf(prob, "%d", &probint) != 1))
		probint = 0;

	if ((random() % 100) + probint > 100) {
		res = ast_parseable_goto(chan, s);
		if (option_verbose > 2)
			ast_verbose( VERBOSE_PREFIX_3 "Random branches to (%s,%s,%d)\n",
				chan->context,chan->exten, chan->priority+1);
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app_random);
}

int load_module(void)
{
	initstate((getppid() * 65535 + getpid()) % RAND_MAX, random_state, 256);
	return ast_register_application(app_random, random_exec, random_synopsis, random_descrip);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return OPENPBX_GPL_KEY;
}
