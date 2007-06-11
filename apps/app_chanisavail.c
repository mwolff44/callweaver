/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * James Golovich <james@gnuinter.net>
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
 * \brief Check if Channel is Available
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/app.h"
#include "callweaver/devicestate.h"

static char *tdesc = "Check if channel is available";

static void *chanisavail_app;
static const char *chanisavail_name = "ChanIsAvail";
static const char *chanisavail_synopsis = "Check if channel is available";
static const char *chanisavail_syntax = "ChanIsAvail(Technology/resource[&Technology2/resource2...][, option])";
static const char *chanisavail_descrip = 
"Checks is any of the requested channels are available.  If none\n"
"of the requested channels are available the new priority will be\n"
"n+101 (unless such a priority does not exist or on error, in which\n"
"case ChanIsAvail will return -1).\n"
"If any of the requested channels are available, the next priority will be n+1,\n"
"the channel variable ${AVAILCHAN} will be set to the name of the available channel\n"
"and the ChanIsAvail app will return 0.\n"
"${AVAILORIGCHAN} is the canonical channel name that was used to create the channel.\n"
"${AVAILSTATUS} is the status code for the channel.\n"
"If the option 's' is specified (state), will consider channel unavailable\n"
"when the channel is in use at all, even if it can take another call.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int chanavail_exec(struct opbx_channel *chan, int argc, char **argv)
{
	char tmp[512], trychan[512], *peers, *tech, *number, *rest, *cur;
	struct localuser *u;
	struct opbx_channel *tempchan;
	int status;
	int res=-1, inuse=-1, option_state=0;

	if (argc < 0 || argc > 1) {
		opbx_log(LOG_ERROR, "Syntax: %s\n", chanisavail_syntax);
		return -1;
	}

	LOCAL_USER_ADD(u);

	if (argc > 1 && strchr(argv[1], 's'))
		option_state = 1;

	peers = argv[0];
	if (peers) {
		cur = peers;
		do {
			/* remember where to start next time */
			rest = strchr(cur, '&');
			if (rest) {
				*rest = 0;
				rest++;
			}
			tech = cur;
			number = strchr(tech, '/');
			if (!number) {
				opbx_log(LOG_WARNING, "ChanIsAvail argument takes format ([technology]/[device])\n");
				LOCAL_USER_REMOVE(u);
				return -1;
			}
			*number = '\0';
			number++;
			
			if (option_state) {
				/* If the pbx says in use then don't bother trying further.
				   This is to permit testing if someone's on a call, even if the 
	 			   channel can permit more calls (ie callwaiting, sip calls, etc).  */
                               
				snprintf(trychan, sizeof(trychan), "%s/%s",cur,number);
				status = inuse = opbx_device_state(trychan);
			}
			if ((inuse <= 1) && (tempchan = opbx_request(tech, chan->nativeformats, number, &status))) {
					pbx_builtin_setvar_helper(chan, "AVAILCHAN", tempchan->name);
					/* Store the originally used channel too */
					snprintf(tmp, sizeof(tmp), "%s/%s", tech, number);
					pbx_builtin_setvar_helper(chan, "AVAILORIGCHAN", tmp);
					snprintf(tmp, sizeof(tmp), "%d", status);
					pbx_builtin_setvar_helper(chan, "AVAILSTATUS", tmp);
					opbx_hangup(tempchan);
					tempchan = NULL;
					res = 1;
					break;
			} else {
				snprintf(tmp, sizeof(tmp), "%d", status);
				pbx_builtin_setvar_helper(chan, "AVAILSTATUS", tmp);
			}
			cur = rest;
		} while (cur);
	}
	if (res < 1) {
		pbx_builtin_setvar_helper(chan, "AVAILCHAN", "");
		pbx_builtin_setvar_helper(chan, "AVAILORIGCHAN", "");
		if (opbx_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101)) {
			LOCAL_USER_REMOVE(u);
			return -1;
		}
	}

	LOCAL_USER_REMOVE(u);
	return 0;
}

int unload_module(void)
{
	int res = 0;
	STANDARD_HANGUP_LOCALUSERS;
	res |= opbx_unregister_application(chanisavail_app);
	return res;
}

int load_module(void)
{
	chanisavail_app = opbx_register_application(chanisavail_name, chanavail_exec, chanisavail_synopsis, chanisavail_syntax, chanisavail_descrip);
	return 0;
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


