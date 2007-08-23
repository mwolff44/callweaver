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
 * \brief Block all calls without Caller*ID, require phone # to be entered
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
#include "callweaver/utils.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/translate.h"
#include "callweaver/image.h"
#include "callweaver/phone_no_utils.h"
#include "callweaver/app.h"
#include "callweaver/config.h"

#define PRIV_CONFIG "privacy.conf"

static const char tdesc[] = "Require phone number to be entered, if no CallerID sent";

static void *privacy_app;
static const char privacy_name[] = "PrivacyManager";
static const char privacy_synopsis[] = "Require phone number to be entered, if no CallerID sent";
static const char privacy_syntax[] = "PrivacyManager()";
static const char privacy_descrip[] =
  "If no Caller*ID was received, PrivacyManager answers the\n"
  "channel and asks the caller to enter their phone number.\n"
  "The caller is given the configured attempts.  If after the attempts, they do not enter\n"
  "at least a configured count of digits of a number, PRIVACYMGRSTATUS is set to FAIL.\n"
  "Otherwise, SUCCESS is flagged by PRIVACYMGRSTATUS.\n"
  "Always returns 0.\n"
  "  Configuration file privacy.conf contains two variables:\n"
  "   maxretries  default 3  -maximum number of attempts the caller is allowed to input a callerid.\n"
  "   minlength   default 10 -minimum allowable digits in the input callerid number.\n"
;


static int privacy_exec (struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	char phone[30];
	struct localuser *u;
	struct opbx_config *cfg;
	char *s;
	int res=0;
	int retries;
	int maxretries = 3;
	int minlength = 10;
	int x;

	LOCAL_USER_ADD (u);

	if (!opbx_strlen_zero(chan->cid.cid_num)) {
		if (option_verbose > 2)
			opbx_verbose (VERBOSE_PREFIX_3 "CallerID Present: Skipping\n");
		pbx_builtin_setvar_helper(chan, "PRIVACYMGRSTATUS", "SUCCESS");
	} else {
		/*Answer the channel if it is not already*/
		if (chan->_state != OPBX_STATE_UP) {
			res = opbx_answer(chan);
			if (res) {
				pbx_builtin_setvar_helper(chan, "PRIVACYMGRSTATUS", "FAIL");
				LOCAL_USER_REMOVE(u);
				return 0;
			}
		}
		/*Read in the config file*/
		cfg = opbx_config_load(PRIV_CONFIG);
		
		
		/*Play unidentified call*/
		res = opbx_safe_sleep(chan, 1000);
		if (!res)
			res = opbx_streamfile(chan, "privacy-unident", chan->language);
		if (!res)
			res = opbx_waitstream(chan, "");

        if (cfg && (s = opbx_variable_retrieve(cfg, "general", "maxretries"))) {
                if (sscanf(s, "%d", &x) == 1) {
                        maxretries = x;
                } else {
                        opbx_log(LOG_WARNING, "Invalid max retries argument\n");
                }
        }
        if (cfg && (s = opbx_variable_retrieve(cfg, "general", "minlength"))) {
                if (sscanf(s, "%d", &x) == 1) {
                        minlength = x;
                } else {
                        opbx_log(LOG_WARNING, "Invalid min length argument\n");
                }
        }
			
		/*Ask for 10 digit number, give 3 attempts*/
		for (retries = 0; retries < maxretries; retries++) {
			if (!res ) 
				res = opbx_app_getdata(chan, "privacy-prompt", phone, sizeof(phone), 0);

			if (res < 0)
				break;

			/*Make sure we get at least our minimum of digits*/
			if (strlen(phone) >= minlength ) 
				break;
			else {
				res = opbx_streamfile(chan, "privacy-incorrect", chan->language);
				if (!res)
					res = opbx_waitstream(chan, "");
			}
		}
		
		/*Got a number, play sounds and send them on their way*/
		if ((retries < maxretries) && res == 1 ) {
			res = opbx_streamfile(chan, "privacy-thankyou", chan->language);
			if (!res)
				res = opbx_waitstream(chan, "");
			opbx_set_callerid (chan, phone, "Privacy Manager", NULL);
			if (option_verbose > 2)
				opbx_verbose (VERBOSE_PREFIX_3 "Changed Caller*ID to %s\n",phone);
			pbx_builtin_setvar_helper(chan, "PRIVACYMGRSTATUS", "SUCCESS");
		} else {
			/* Flag Failure  */
			pbx_builtin_setvar_helper(chan, "PRIVACYMGRSTATUS", "FAIL");
		}
		if (cfg) 
			opbx_config_destroy(cfg);
	}

  LOCAL_USER_REMOVE (u);
  return 0;
}

static int
unload_module (void)
{
  int res = 0;

  res |= opbx_unregister_function (privacy_app);
  return res;
}

static int
load_module (void)
{
	privacy_app = opbx_register_function(privacy_name, privacy_exec, privacy_synopsis, privacy_syntax, privacy_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
