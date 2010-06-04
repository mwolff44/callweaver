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
 * \brief Channel timeout related dialplan functions
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/module.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"
#include "callweaver/app.h"
#include "callweaver/options.h"


static void *timeout_function;
static const char timeout_func_name[] = "TIMEOUT";
static const char timeout_func_synopsis[] = "Gets or sets timeouts on the channel.";
static const char timeout_func_syntax[] = "TIMEOUT(timeouttype[, value])";
static const char timeout_func_desc[] =
	"Gets or sets various channel timeouts. The timeouts that can be\n"
	"manipulated are:\n"
	"\n"
	"absolute: The absolute maximum amount of time permitted for a call.  A\n"
	"	   setting of 0 disables the timeout.\n"
	"\n"
	"digit:    The maximum amount of time permitted between digits when the\n"
	"          user is typing in an extension.  When this timeout expires,\n"
	"          after the user has started to type in an extension, the\n"
	"          extension will be considered complete, and will be\n"
	"          interpreted.  Note that if an extension typed in is valid,\n"
	"          it will not have to timeout to be tested, so typically at\n"
	"          the expiry of this timeout, the extension will be considered\n"
	"          invalid (and thus control would be passed to the 'i'\n"
	"          extension, or if it doesn't exist the call would be\n"
	"          terminated).  The default timeout is 5 seconds.\n"
	"\n"
	"response: The maximum amount of time permitted after falling through a\n"
	"	   series of priorities for a channel in which the user may\n"
	"	   begin typing an extension.  If the user does not type an\n"
	"	   extension in this amount of time, control will pass to the\n"
	"	   't' extension if it exists, and if not the call would be\n"
	"	   terminated.  The default timeout is 10 seconds.\n";


static int builtin_function_timeout_rw(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	if (argc < 1 || argc > 2 || !argv[0][0])
		return cw_function_syntax(timeout_func_syntax);

        if (!chan) {
                cw_log(CW_LOG_ERROR, "No channel! Timeout only works on channels\n");
                return -1;
        }

	if (argc > 1) {
		int x = atoi(argv[1]);

		switch (argv[0][0]) {
		case 'a':
		case 'A':
			cw_channel_setwhentohangup(chan, x);
			if (option_verbose > 2) {
				if (chan->whentohangup) {
					char timestr[64];
					struct tm myt;
					strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S UTC", gmtime_r(&chan->whentohangup, &myt));
					cw_verbose( VERBOSE_PREFIX_3 "%s will hangup at %s.\n", chan->name, timestr);
				} else {
					cw_verbose( VERBOSE_PREFIX_3 "%s hangup cancelled.\n", chan->name);
				} 
			}
			break;

		case 'r':
		case 'R':
			if (chan->pbx) {
				chan->pbx->rtimeout = x;
				if (option_verbose > 2)
					cw_verbose( VERBOSE_PREFIX_3 "%s response timeout set to %d\n", chan->name, chan->pbx->rtimeout);
			}
			break;

		case 'd':
		case 'D':
			if (chan->pbx) {
				chan->pbx->dtimeout = x;
				if (option_verbose > 2)
					cw_verbose( VERBOSE_PREFIX_3 "%s digit timeout set to %d\n", chan->name, chan->pbx->dtimeout);
			}
			break;

		default:
			return cw_function_syntax(timeout_func_syntax);
		}
	}
	
	if (result) {
		switch(argv[0][0]) {
		case 'a':
		case 'A':
			if (chan->whentohangup == 0)
				cw_dynstr_printf(result, "0");
			else
				cw_dynstr_printf(result, "%d", (int) (chan->whentohangup - time(NULL)));
			break;

		case 'r':
		case 'R':
			if (chan->pbx)
				cw_dynstr_printf(result, "%d", chan->pbx->rtimeout);
			break;

		case 'd':
		case 'D':
			if (chan->pbx)
				cw_dynstr_printf(result, "%d", chan->pbx->dtimeout);
			break;

		default:
			return cw_function_syntax(timeout_func_syntax);
		}
	}

	return 0;
}


static const char tdesc[] = "string functions";

static int unload_module(void)
{
        return cw_unregister_function(timeout_function);
}

static int load_module(void)
{
        timeout_function = cw_register_function(timeout_func_name, builtin_function_timeout_rw, timeout_func_synopsis, timeout_func_syntax, timeout_func_desc);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)

/*
Local Variables:
mode: C
c-file-style: "linux"
indent-tabs-mode: nil
End:
*/
