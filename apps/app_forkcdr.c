/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Anthony Minessale anthmct@yahoo.com
 * Development of this app Sponsered/Funded  by TAAN Softworks Corp
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

/*! \file
 *
 * \brief Fork CDR application
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/cdr.h"
#include "callweaver/module.h"
#include "callweaver/registry.h"

static const char tdesc[] = "Fork The CDR into 2 separate entities.";

static void *forkcdr_app;
static const char forkcdr_name[] = "ForkCDR";
static const char forkcdr_synopsis[] = "Forks the Call Data Record";
static const char forkcdr_syntax[] = "ForkCDR([options])";
static const char forkcdr_descrip[] = 
"Causes the Call Data Record to fork an additional\n"
"cdr record starting from the time of the fork call\n"
"If the option 'v' is passed all cdr variables will be passed along also.\n";


static void cw_cdr_fork(struct cw_channel *chan) 
{
	struct cw_cdr *cdr;
	struct cw_cdr *newcdr;
	if (!chan || !(cdr = chan->cdr))
		return;
	while (cdr->next)
		cdr = cdr->next;
	if (!(newcdr = cw_cdr_dup(cdr)))
		return;
	cw_cdr_append(cdr, newcdr);
	cw_cdr_reset(newcdr, CW_CDR_FLAG_KEEP_VARS);
	if (!cw_test_flag(cdr, CW_CDR_FLAG_KEEP_VARS))
		cw_registry_flush(&cdr->vars);
	cw_set_flag(cdr, CW_CDR_FLAG_CHILD | CW_CDR_FLAG_LOCKED);
}

static int forkcdr_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct localuser *u;
	int res = 0;

	CW_UNUSED(result);

	LOCAL_USER_ADD(u);

	if (argc > 0)
		cw_set2_flag(chan->cdr, strchr(argv[0], 'v'), CW_CDR_FLAG_KEEP_VARS);
	
	cw_cdr_fork(chan);

	LOCAL_USER_REMOVE(u);
	return res;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(forkcdr_app);
	return res;
}

static int load_module(void)
{
	forkcdr_app = cw_register_function(forkcdr_name, forkcdr_exec, forkcdr_synopsis, forkcdr_syntax, forkcdr_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
