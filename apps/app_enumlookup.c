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
static int enumlookup_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	static int dep_warning = 0;
	char tech[80];
	struct cw_dynstr dest = CW_DYNSTR_INIT;
	struct localuser *u;
	char *c, *t;
	int res = 0;

	CW_UNUSED(result);

	if (!dep_warning) {
		cw_log(CW_LOG_WARNING, "The application EnumLookup is deprecated.  Please use the ENUMLOOKUP() function instead.\n");
		dep_warning = 1;
	}

	if (argc != 1)
		return cw_function_syntax(enumlookup_syntax);
		
	LOCAL_USER_ADD(u);

	tech[0] = '\0';

	if (cw_get_enum(chan, argv[0], &dest, tech, sizeof(tech), NULL, NULL)) {
		pbx_builtin_setvar_helper(chan, "ENUMSTATUS", tech);
		/* Parse it out */
		if (res > 0 && !dest.error) {
			struct cw_dynstr tmp = CW_DYNSTR_INIT;

			if (!strcasecmp(tech, "SIP")) {
				c = dest.data;
				if (!strncmp(c, "sip:", 4))
					c += 4;
				cw_dynstr_printf(&tmp, "SIP/%s", c);
			} else if (!strcasecmp(tech, "h323")) {
				c = dest.data;
				if (!strncmp(c, "h323:", 5))
					c += 5;
				/* do a s!;.*!! on the H323 URI */
				t = strchr(c,';');
				if (t)
					*t = 0;
				cw_dynstr_printf(&tmp, "%s/%s", h323driver, c);
			} else if (!strcasecmp(tech, "iax")) {
				c = dest.data;
				if (!strncmp(c, "iax:", 4))
					c += 4;
				cw_dynstr_printf(&tmp, "IAX/%s", c);
			} else if (!strcasecmp(tech, "iax2")) {
				c = dest.data;
				if (!strncmp(c, "iax2:", 5))
					c += 5;
				cw_dynstr_printf(&tmp, "IAX2/%s", c);
			} else if (!strcasecmp(tech, "tel")) {
				c = dest.data;
				if (!strncmp(c, "tel:", 4))
					c += 4;

				if (c[0] != '+') {
					cw_log(CW_LOG_NOTICE, "tel: uri must start with a \"+\" (got '%s')\n", c);
					res = 0;
				} else {
					/* now copy over the number, skipping all non-digits and stop at ; or NULL */
					cw_dynstr_need(&tmp, dest.used);
					if (!tmp.error) {
						t = tmp.data;
						while (*c && *c != ';') {
							if (isdigit(*c))
								*(t++) = *c;
							c++;
						}
						*t = 0;
						cw_log(CW_LOG_NOTICE, "tel: ENUM set to \"%s\"\n", tmp.data);
					}
				}
			} else if (!cw_strlen_zero(tech)) {
				cw_log(CW_LOG_NOTICE, "Don't know how to handle technology '%s'\n", tech);
				pbx_builtin_setvar_helper(chan, "ENUMSTATUS", "BADURI");
				res = 0;
			}

			if (tmp.size && !tmp.error)
				pbx_builtin_setvar_helper(chan, "ENUM", tmp.data);

			cw_dynstr_free(&tmp);
		}
	} else {
		/* Look for a "busy" place */
		pbx_builtin_setvar_helper(chan, "ENUMSTATUS", "ERROR");
	}

	cw_dynstr_free(&dest);
	LOCAL_USER_REMOVE(u);
	return 0;
}

/*--- load_config: Load enum.conf and find out how to handle H.323 */
static int load_config(void)
{
	struct cw_config *cfg;
	char *s;

	cfg = cw_config_load(ENUM_CONFIG);
	if (cfg) {
		if (!(s=cw_variable_retrieve(cfg, "general", "h323driver"))) {
			strncpy(h323driver, H323DRIVERDEFAULT, sizeof(h323driver) - 1);
		} else {
			strncpy(h323driver, s, sizeof(h323driver) - 1);
		}
		cw_config_destroy(cfg);
		return 0;
	}
	cw_log(CW_LOG_NOTICE, "No ENUM Config file, using defaults\n");
	return 0;
}


/*--- unload_module: Unload this application from PBX */
static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(enumlookup_app);
	return res;
}

/*--- load_module: Load this application into PBX */
static int load_module(void)
{
	enumlookup_app = cw_register_function(enumlookup_name, enumlookup_exec, enumlookup_synopsis, enumlookup_syntax, enumlookup_descrip);
	return 0;
}

/*--- reload: Reload configuration file */
static int reload_module(void)
{
	return(load_config());
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, tdesc)
