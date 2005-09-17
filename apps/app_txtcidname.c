/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * Caller*id name lookup - Look up the caller's name via DNS
 * 
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision$")

#include "openpbx/lock.h"
#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/options.h"
#include "openpbx/config.h"
#include "openpbx/module.h"
#include "openpbx/enum.h"
#include "openpbx/utils.h"

static char *tdesc = "TXTCIDName";

static char *app = "TXTCIDName";

static char *synopsis = "Lookup caller name from TXT record";

static char *descrip = 
"  TXTCIDName(<CallerIDNumber>):  Looks up a Caller Name via DNS and sets\n"
"the variable 'TXTCIDNAME'. TXTCIDName will either be blank\n"
"or return the value found in the TXT record in DNS.\n" ;

#define ENUM_CONFIG "enum.conf"

static char h323driver[80] = "";
#define H323DRIVERDEFAULT "H323"

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int txtcidname_exec(struct opbx_channel *chan, void *data)
{
	int res=0;
	char tech[80];
	char txt[256] = "";
	char dest[80];

	struct localuser *u;
	if (!data || !strlen(data)) {
		opbx_log(LOG_WARNING, "TXTCIDName requires an argument (extension)\n");
		res = 1;
	}
	LOCAL_USER_ADD(u);
	if (!res) {
		res = opbx_get_txt(chan, data, dest, sizeof(dest), tech, sizeof(tech), txt, sizeof(txt));
	}
	LOCAL_USER_REMOVE(u);
	/* Parse it out */
	if (res > 0) {
		if (!opbx_strlen_zero(txt)) {
			pbx_builtin_setvar_helper(chan, "TXTCIDNAME", txt);
			if (option_debug > 1)
				opbx_log(LOG_DEBUG, "TXTCIDNAME got '%s'\n", txt);
		}
	}
	if (!res) {
		/* Look for a "busy" place */
		opbx_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
	} else if (res > 0)
		res = 0;
	return res;
}

static int load_config(void)
{
	struct opbx_config *cfg;
	char *s;

	cfg = opbx_config_load(ENUM_CONFIG);
	if (cfg) {
		if (!(s=opbx_variable_retrieve(cfg, "general", "h323driver"))) {
			opbx_copy_string(h323driver, H323DRIVERDEFAULT, sizeof(h323driver));
		} else {
			opbx_copy_string(h323driver, s, sizeof(h323driver));
		}
		opbx_config_destroy(cfg);
		return 0;
	}
	opbx_log(LOG_NOTICE, "No ENUM Config file, using defaults\n");
	return 0;
}


int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return opbx_unregister_application(app);
}

int load_module(void)
{
	int res;
	res = opbx_register_application(app, txtcidname_exec, synopsis, descrip);
	if (res)
		return(res);
	if ((res=load_config())) {
		return(res);
	}
	return(0);
}

int reload(void)
{
	return(load_config());
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



