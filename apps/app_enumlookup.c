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
 * \brief Enumlookup - lookup entry in ENUM
 *
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/options.h"
#include "callweaver/config.h"
#include "callweaver/module.h"
#include "callweaver/enum.h"
#include "callweaver/utils.h"

static const char tdesc[] = "ENUM Lookup";

static void *enumlookup_app;
static const char enumlookup_name[] = "EnumLookup";
static const char enumlookup_synopsis[] = "Lookup number in ENUM";
static const char enumlookup_syntax[] = "EnumLookup(exten)";
static const char enumlookup_descrip[] =
"Looks up an extension via ENUM and sets\n"
"the variable 'ENUM'. For VoIP URIs this variable will \n"
"look like 'TECHNOLOGY/URI' with the appropriate technology.\n"
"Returns -1 on hangup, or 0 on completion\n"
"Currently, the enumservices SIP, H323, IAX, IAX2 and TEL are recognized. \n"
"\nReturns status in the ENUMSTATUS channel variable:\n"
"    ERROR	Failed to do a lookup\n"
"    <tech>	Technology of the successful lookup: SIP, H323, IAX, IAX2 or TEL\n"
"    BADURI	Got URI CallWeaver does not understand.\n";

#define ENUM_CONFIG "enum.conf"

static char h323driver[80] = "";
#define H323DRIVERDEFAULT "H323"


/*--- enumlookup_exec: Look up number in ENUM and return result */
static int enumlookup_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	static int dep_warning = 0;
	char tech[80];
	char dest[80];
	char tmp[256];
	struct localuser *u;
	char *c, *t;
	int res = 0;

	if (!dep_warning) {
		opbx_log(OPBX_LOG_WARNING, "The application EnumLookup is deprecated.  Please use the ENUMLOOKUP() function instead.\n");
		dep_warning = 1;
	}

	if (argc != 1)
		return opbx_function_syntax(enumlookup_syntax);
		
	LOCAL_USER_ADD(u);

	tech[0] = '\0';

	res = opbx_get_enum(chan, argv[0], dest, sizeof(dest), tech, sizeof(tech), NULL, NULL);
	
	if (!res) {	/* Failed to do a lookup */
		/* Look for a "busy" place */
		pbx_builtin_setvar_helper(chan, "ENUMSTATUS", "ERROR");
		LOCAL_USER_REMOVE(u);
		return 0;
	}
	pbx_builtin_setvar_helper(chan, "ENUMSTATUS", tech);
	/* Parse it out */
	if (res > 0) {
		if (!strcasecmp(tech, "SIP")) {
			c = dest;
			if (!strncmp(c, "sip:", 4))
				c += 4;
			snprintf(tmp, sizeof(tmp), "SIP/%s", c);
			pbx_builtin_setvar_helper(chan, "ENUM", tmp);
		} else if (!strcasecmp(tech, "h323")) {
			c = dest;
			if (!strncmp(c, "h323:", 5))
				c += 5;
			snprintf(tmp, sizeof(tmp), "%s/%s", h323driver, c);
/* do a s!;.*!! on the H323 URI */
			t = strchr(c,';');
                       if (t)
				*t = 0;
			pbx_builtin_setvar_helper(chan, "ENUM", tmp);
		} else if (!strcasecmp(tech, "iax")) {
			c = dest;
			if (!strncmp(c, "iax:", 4))
				c += 4;
			snprintf(tmp, sizeof(tmp), "IAX/%s", c);
			pbx_builtin_setvar_helper(chan, "ENUM", tmp);
		} else if (!strcasecmp(tech, "iax2")) {
			c = dest;
			if (!strncmp(c, "iax2:", 5))
				c += 5;
			snprintf(tmp, sizeof(tmp), "IAX2/%s", c);
			pbx_builtin_setvar_helper(chan, "ENUM", tmp);
		} else if (!strcasecmp(tech, "tel")) {
			c = dest;
			if (!strncmp(c, "tel:", 4))
				c += 4;

			if (c[0] != '+') {
				opbx_log(OPBX_LOG_NOTICE, "tel: uri must start with a \"+\" (got '%s')\n", c);
				res = 0;
			} else {
/* now copy over the number, skipping all non-digits and stop at ; or NULL */
                               t = tmp;
				while( *c && (*c != ';') && (t - tmp < (sizeof(tmp) - 1))) {
					if (isdigit(*c))
						*t++ = *c;
					c++;
				}
				*t = 0;
				pbx_builtin_setvar_helper(chan, "ENUM", tmp);
				opbx_log(OPBX_LOG_NOTICE, "tel: ENUM set to \"%s\"\n", tmp);
			}
		} else if (!opbx_strlen_zero(tech)) {
			opbx_log(OPBX_LOG_NOTICE, "Don't know how to handle technology '%s'\n", tech);
			pbx_builtin_setvar_helper(chan, "ENUMSTATUS", "BADURI");
			res = 0;
		}
	}

	LOCAL_USER_REMOVE(u);

	return 0;
}

/*--- load_config: Load enum.conf and find out how to handle H.323 */
static int load_config(void)
{
	struct opbx_config *cfg;
	char *s;

	cfg = opbx_config_load(ENUM_CONFIG);
	if (cfg) {
		if (!(s=opbx_variable_retrieve(cfg, "general", "h323driver"))) {
			strncpy(h323driver, H323DRIVERDEFAULT, sizeof(h323driver) - 1);
		} else {
			strncpy(h323driver, s, sizeof(h323driver) - 1);
		}
		opbx_config_destroy(cfg);
		return 0;
	}
	opbx_log(OPBX_LOG_NOTICE, "No ENUM Config file, using defaults\n");
	return 0;
}


/*--- unload_module: Unload this application from PBX */
static int unload_module(void)
{
	int res = 0;

	res |= opbx_unregister_function(enumlookup_app);
	return res;
}

/*--- load_module: Load this application into PBX */
static int load_module(void)
{
	enumlookup_app = opbx_register_function(enumlookup_name, enumlookup_exec, enumlookup_synopsis, enumlookup_syntax, enumlookup_descrip);
	return 0;
}

/*--- reload: Reload configuration file */
static int reload_module(void)
{
	return(load_config());
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, tdesc)
