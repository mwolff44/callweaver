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

/*! \file
 *
 * \brief Transfer a caller
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/options.h"


static const char tdesc[] = "Transfer";

static void *transfer_app;
static const char transfer_name[] = "Transfer";
static const char transfer_synopsis[] = "Transfer caller to remote extension";
static const char transfer_syntax[] = "Transfer([Tech/]dest)";
static const char transfer_descrip[] = 
"Requests the remote caller be transfered\n"
"to a given extension. If TECH (SIP, IAX2, LOCAL etc) is used, only\n"
"an incoming call with the same channel technology will be transfered.\n"
"Note that for SIP, if you transfer before call is setup, a 302 redirect\n"
"SIP message will be returned to the caller.\n"
"When using with SIP, if the host:port is not specified, it will"
"try to lookup the registry address of 'dest' and use that information.\n"
"\nThe result of the application will be reported in the TRANSFERSTATUS\n"
"channel variable:\n"
"       SUCCESS       Transfer succeeded\n"
"       FAILURE      Transfer failed\n"
"       UNSUPPORTED  Transfer unsupported by channel driver\n"
"Returns -1 on hangup, or 0 on completion regardless of whether the\n"
"transfer was successful.\n\n"
"Old depraciated behaviour: If the transfer was *not* supported or\n"
"successful and there exists a priority n + 101,\n"
"then that priority will be taken next.\n" ;

static int transfer_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct localuser *u;
	const char *slash;
	const char *tech = NULL;
	const char *dest;
	const char *status;
	int len;
	int res;

	CW_UNUSED(result);

	if (argc != 1)
		return cw_function_syntax(transfer_syntax);

	LOCAL_USER_ADD(u);

	dest = argv[0];

	if ((slash = strchr(dest, '/')) && (len = (slash - dest))) {
		tech = dest;
		dest = slash + 1;
		/* Allow execution only if the Tech/destination agrees with the type of the channel */
		if (strncasecmp(chan->type, tech, len)) {
			pbx_builtin_setvar_helper(chan, "TRANSFERSTATUS", "FAILURE");
			LOCAL_USER_REMOVE(u);
			return 0;
		}
	}

	/* Check if the channel supports transfer before we try it */
	if (!chan->tech->transfer) {
		pbx_builtin_setvar_helper(chan, "TRANSFERSTATUS", "UNSUPPORTED");
		LOCAL_USER_REMOVE(u);
		return 0;
	}

	res = cw_transfer(chan, dest);

	if (res < 0) {
		status = "FAILURE";
		if (option_priority_jumping)
			cw_goto_if_exists_n(chan, chan->context, chan->exten, chan->priority + 101);
		res = 0;
	} else {
		status = "SUCCESS";
		res = 0;
	}

	pbx_builtin_setvar_helper(chan, "TRANSFERSTATUS", status);

	LOCAL_USER_REMOVE(u);

	return res;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(transfer_app);
	return res;
}

static int load_module(void)
{
	transfer_app = cw_register_function(transfer_name, transfer_exec, transfer_synopsis, transfer_syntax, transfer_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
