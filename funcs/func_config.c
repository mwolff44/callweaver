/* comments and license {{{
 * vim:ts=4:sw=4:smartindent:cindent:autoindent:foldmethod=marker
 *
 * CallWeaver -- an open source telephony toolkit.
 *
 * Copyright (c) 1999 - 2005, digium, inc.
 *
 * Roy Sigurd Karlsbakk <roy@karlsbakk.net>
 *
 * See http://www.callweaver.org for more information about
 * the callweaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and irc
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the gnu general public license version 2. See the license file
 * at the top of the source tree.
 *
 * }}} */

/* includes and so on {{{ */
/*! \roy
 *
 * \brief functions for reading global configuration data
 * 
 */
#include <stdio.h>
#include <stdlib.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/module.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/utils.h"
/* }}} */

/* globals {{{ */
static void *config_function;
static const char config_func_name[] = "CONFIG";
static const char config_func_synopsis[] = "Read configuration values set in callweaver.conf";
static const char config_func_syntax[] = "CONFIG(name)";
static const char config_func_desc[] = "This function will read configuration values set in callweaver.conf.\n"
			"Possible values include cwctlpermissions, cwctlowner, cwctlgroup,\n"
			"cwctl, cwdb, cwetcdir, cwconfigdir, cwspooldir, cwvarlibdir,\n"
			"cwvardir, cwdbdir, cwlogdir, cwogidir, cwsoundsdir, and cwrundir\n";
/* }}} */

/* function_config_read() {{{ */
static int function_config_rw(struct cw_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	static struct {
		char *key, *value;
	} keytab[] = {
#if 0
		/* These doesn't seem to be available outside callweaver.c */
		{ "cwrunuser", cw_config_CW_RUN_USER },
		{ "cwrungroup", cw_config_CW_RUN_GROUP },
		{ "cwmoddir", cw_config_CW_MOD_DIR },
#endif
		{ "cwctlpermissions", cw_config_CW_CTL_PERMISSIONS },
		{ "cwctlowner", cw_config_CW_CTL_OWNER },
		{ "cwctlgroup", cw_config_CW_CTL_GROUP },
		{ "cwctl", cw_config_CW_CTL },
		{ "cwdb", cw_config_CW_DB },
		{ "cwetcdir", cw_config_CW_CONFIG_DIR },
		{ "cwconfigdir", cw_config_CW_CONFIG_DIR },
		{ "cwspooldir", cw_config_CW_SPOOL_DIR },
		{ "cwvarlibdir", cw_config_CW_VAR_DIR },
		{ "cwvardir", cw_config_CW_VAR_DIR },
		{ "cwdbdir", cw_config_CW_DB_DIR },
		{ "cwlogdir", cw_config_CW_LOG_DIR },
		{ "cwogidir", cw_config_CW_OGI_DIR },
		{ "cwsoundsdir", cw_config_CW_SOUNDS_DIR },
		{ "cwrundir", cw_config_CW_RUN_DIR },
		{ "systemname", cw_config_CW_SYSTEM_NAME },
		{ "enableunsafeunload", cw_config_CW_ENABLE_UNSAFE_UNLOAD },
	};
	int i;

	if (buf) {
		for (i = 0; i < arraysize(keytab); i++) {
			if (!strcasecmp(keytab[i].key, argv[0])) {
				cw_copy_string(buf, keytab[i].value, len);
				return 0;
			}
		}
		cw_log(CW_LOG_ERROR, "Config setting '%s' not known.\n", argv[0]);
		return -1;
	}

	cw_log(CW_LOG_ERROR, "This function currently cannot be used to change the CallWeaver config. Modify callweaver.conf manually and restart.\n");
	return -1;
}
/* function_config_read() }}} */

/* globals {{{ */
static const char tdesc[] = "CONFIG function";
/* globals }}} */

/* unload_module() {{{ */
static int unload_module(void)
{
        return cw_unregister_function(config_function);
}
/* }}} */

/* load_module() {{{ */
static int load_module(void)
{
        config_function = cw_register_function(config_func_name, function_config_rw, config_func_synopsis, config_func_syntax, config_func_desc);
	return 0;
}
/* }}} */


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)


/* tail {{{

local variables:
mode: c
c-file-style: "linux"
indent-tabs-mode: nil
end:

function_config_read() }}} */
