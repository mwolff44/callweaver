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
 * \brief Echo application -- play back what you hear to evaluate latency
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision: 2615 $")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"

static char *tdesc = "Simple Echo Application";

static void *echo_app;
static const char *echo_name = "Echo";
static const char *echo_synopsis = "Echo audio read back to the user";
static const char *echo_syntax = "Echo()";
static const char *echo_descrip = 
"Echo audio read from channel back to the channel. Returns 0\n"
"if the user exits with the '#' key, or -1 if the user hangs up.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int echo_exec(struct opbx_channel *chan, int argc, char **argv)
{
	struct localuser *u;
	struct opbx_frame *f;
	int res = -1;

	if (argc != 0) {
		opbx_log(LOG_ERROR, "Syntax: %s\n", echo_syntax);
		return -1;
	}

	LOCAL_USER_ADD(u);

	opbx_set_write_format(chan, opbx_best_codec(chan->nativeformats));
	opbx_set_read_format(chan, opbx_best_codec(chan->nativeformats));
	/* Do our thing here */
	while(opbx_waitfor(chan, -1) > -1) {
		f = opbx_read(chan);
		if (!f)
			break;
		f->delivery.tv_sec = 0;
		f->delivery.tv_usec = 0;
		if (f->frametype == OPBX_FRAME_VOICE) {
			if (opbx_write(chan, f)) 
				break;
		} else if (f->frametype == OPBX_FRAME_VIDEO) {
			if (opbx_write(chan, f)) 
				break;
		} else if (f->frametype == OPBX_FRAME_DTMF) {
			if (f->subclass == '#') {
				res = 0;
				break;
			} else
				if (opbx_write(chan, f))
					break;
		}
		opbx_fr_free(f);
	}

	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int res = 0;
	STANDARD_HANGUP_LOCALUSERS;
	res |= opbx_unregister_application(echo_app);
	return res;
}

int load_module(void)
{
	echo_app = opbx_register_application(echo_name, echo_exec, echo_synopsis, echo_syntax, echo_descrip);
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


