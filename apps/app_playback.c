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
 * \brief Trivial application to playback a sound file
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h> 
#include <string.h>
#include <stdlib.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/translate.h"
#include "callweaver/utils.h"

static char *tdesc = "Sound File Playback Application";

static void *playback_app;
static char *playback_name = "Playback";
static char *playback_synopsis = "Play a file";
static char *playback_syntax = "Playback(filename[&filename2...][, option])";
static char *playback_descrip = 
"Plays back given filenames (do not put\n"
"extension). Options may also be  included following a pipe symbol. The 'skip'\n"
"option causes the playback of the message to  be  skipped  if  the  channel\n"
"is not in the 'up' state (i.e. it hasn't been  answered  yet. If 'skip' is \n"
"specified, the application will return immediately should the channel not be\n"
"off hook.  Otherwise, unless 'noanswer' is specified, the channel channel will\n"
"be answered before the sound is played. Not all channels support playing\n"
"messages while still hook. Returns -1 if the channel was hung up.\n"
"The channel variable PLAYBACKSTATUS is set to SUCCESS or FAILED on termination."
"\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int playback_exec(struct opbx_channel *chan, int argc, char **argv)
{
	struct localuser *u;
	char *front = NULL, *back = NULL;
	int res = 0, mres = 0;
	int option_skip = 0;
	int option_noanswer = 0;
	int i;

	if (argc < 1) {
		opbx_log(LOG_ERROR, "Syntax: %s\n", playback_syntax);
		return -1;
	}

	LOCAL_USER_ADD(u);

        pbx_builtin_setvar_helper(chan, "PLAYBACKSTATUS", "");

	for (i = 1; i < argc; i++) {
		if (!strcasecmp(argv[i], "skip"))
			option_skip = 1;
		else if (!strcasecmp(argv[i], "noanswer"))
			option_noanswer = 1;
	}
	
	if (chan->_state != OPBX_STATE_UP) {
		if (option_skip) {
			/* At the user's option, skip if the line is not up */
                        pbx_builtin_setvar_helper(chan, "PLAYBACKSTATUS", "SUCCESS");
			LOCAL_USER_REMOVE(u);
			return 0;
		} else if (!option_noanswer)
			/* Otherwise answer unless we're supposed to send this while on-hook */
			res = opbx_answer(chan);
	}
	if (!res) {
		opbx_stopstream(chan);
		front = argv[0];
		while (!res && front) {
			if ((back = strchr(front, '&'))) {
				*back = '\0';
				back++;
			}
			res = opbx_streamfile(chan, front, chan->language);
			if (!res) { 
				res = opbx_waitstream(chan, "");	
				opbx_stopstream(chan);
			} else {
				opbx_log(LOG_WARNING, "opbx_streamfile failed on %s for %s\n", chan->name, argv[0]);
				res = 0;
				mres = 1;
			}
			front = back;
		}
	}
	if (mres)
		pbx_builtin_setvar_helper(chan, "PLAYBACKSTATUS", "FAILED");
	else
		pbx_builtin_setvar_helper(chan, "PLAYBACKSTATUS", "SUCCESS");
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int res = 0;
	STANDARD_HANGUP_LOCALUSERS;
	res |= opbx_unregister_application(playback_app);
	return res;
}

int load_module(void)
{
	playback_app = opbx_register_application(playback_name, playback_exec, playback_synopsis, playback_syntax, playback_descrip);
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


