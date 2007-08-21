/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (c) 2003 - 2005 Tilghman Lesher.  All rights reserved.
 *
 * Tilghman Lesher <asterisk__app_random__200508@the-tilghman.com>
 *
 * This code is released by the author with no restrictions on usage or distribution.
 *
 * See http://www.callweaver.org for more information about
 * the CallWeaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*! \file
 *
 * \brief Random application
 *
 * \author Tilghman Lesher <asterisk__app_random__200508 at the-tilghman.com>
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"

static const char tdesc[] = "Random goto";

static void *random_app;
static const char random_name[] = "Random";
static const char random_synopsis[] = "Conditionally branches, based upon a probability";
static const char random_syntax[] = "Random([probability]:[[context, ]extension, ]priority)";
static const char random_descrip[] =
"Conditionally branches, based upon a probability\n"
"  probability := INTEGER in the range 1 to 100\n";


static char random_state[256];

static int random_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	int res=0;
	struct localuser *u;
	char *s, *context, *exten;
	int probint;
	
	if (argc < 1 || argc > 3)
		return opbx_function_syntax(random_syntax);

	LOCAL_USER_ADD(u);

	if ((s = strchr(argv[0], ':'))) {
		probint = atoi(argv[0]);
		argv[0] = s + 1;
	}

	/* FIXME: is this really what was intended? */
	if ((random() % 100) + probint > 100) {
		exten = (argc > 1 ? argv[argc-2] : NULL);
		context = (argc > 2 ? argv[argc-3] : NULL);
		res = opbx_explicit_gotolabel(chan, context, exten, argv[argc-1]);
		if (!res && option_verbose > 2)
			opbx_verbose( VERBOSE_PREFIX_3 "Random branches to (%s,%s,%d)\n",
				chan->context,chan->exten, chan->priority+1);
	}

	LOCAL_USER_REMOVE(u);
	return res;
}

static int unload_module(void)
{
	int res = 0;

	res |= opbx_unregister_function(random_app);
	return res;
}

static int load_module(void)
{
	/* Don't allow unload, since rand(3) depends upon this module being here. */
	opbx_module_get(get_modinfo()->self);

	initstate((getppid() * 65535 + getpid()) % RAND_MAX, random_state, 256);
	random_app = opbx_register_function(random_name, random_exec, random_synopsis, random_syntax, random_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
