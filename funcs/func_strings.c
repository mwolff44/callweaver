/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Digium, Inc.
 * Portions Copyright (C) 2005, Tilghman Lesher.  All rights reserved.
 * Portions Copyright (C) 2005, Anthony Minessale II
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
 * \brief String manipulation dialplan functions
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <regex.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/module.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"
#include "callweaver/app.h"
#include "callweaver/localtime.h"

/* Maximum length of any variable */
#define MAXRESULT	1024


static void *fieldqty_function;
static const char *fieldqty_func_name = "FIELDQTY";
static const char *fieldqty_func_synopsis = "Count the fields, with an arbitrary delimiter";
static const char *fieldqty_func_syntax = "FIELDQTY(varname, delim)";
static const char *fieldqty_func_desc = "";

static void *regex_function;
static const char *regex_func_name = "REGEX";
static const char *regex_func_synopsis = "Regular Expression: Returns 1 if data matches regular expression.";
static const char *regex_func_syntax = "REGEX(\"regular expression\" data)";
static const char *regex_func_desc = "";

static void *len_function;
static const char *len_func_name = "LEN";
static const char *len_func_synopsis = "Returns the length of the argument given";
static const char *len_func_syntax = "LEN(string)";
static const char *len_func_desc = "";

static void *strftime_function;
static const char *strftime_func_name = "STRFTIME";
static const char *strftime_func_synopsis = "Returns the current date/time in a specified format.";
static const char *strftime_func_syntax = "STRFTIME([epoch[, timezone[, format]]])";
static const char *strftime_func_desc = "";

static void *eval_function;
static const char *eval_func_name = "EVAL";
static const char *eval_func_synopsis = "Evaluate stored variables.";
static const char *eval_func_syntax = "EVAL(variable)";
static const char *eval_func_desc =
	"Using EVAL basically causes a string to be evaluated twice.\n"
	"When a variable or expression is in the dialplan, it will be\n"
	"evaluated at runtime. However, if the result of the evaluation\n"
	"is in fact a variable or expression, using EVAL will have it\n"
	"evaluated a second time. For example, if the variable ${MYVAR}\n"
	"contains \"${OTHERVAR}\", then the result of putting ${EVAL(${MYVAR})}\n"
	"in the dialplan will be the contents of the variable, OTHERVAR.\n"
	"Normally, by just putting ${MYVAR} in the dialplan, you would be\n"
	"left with \"${OTHERVAR}\".\n";

static void *cut_function;
static const char *cut_func_name = "CUT";
static const char *cut_func_synopsis = "Slices and dices strings, based upon a named delimiter.";
static const char *cut_func_syntax = "CUT(varname, char-delim, range-spec)";
static const char *cut_func_desc =
	"  varname    - variable you want cut\n"
	"  char-delim - defaults to '-'\n"
	"  range-spec - number of the field you want (1-based offset)\n"
	"             may also be specified as a range (with -)\n"
	"             or group of ranges and fields (with &)\n";

static void *sort_function;
static const char *sort_func_name= "SORT";
static const char *sort_func_synopsis = "Sorts a list of key/vals into a list of keys, based upon the vals";
static const char *sort_func_syntax = "SORT(key1:val1[...][, keyN:valN])";
static const char *sort_func_desc =
	"Takes a comma-separated list of keys and values, each separated by a colon, and returns a\n"
	"comma-separated list of the keys, sorted by their values.  Values will be evaluated as\n"
	"floating-point numbers.\n";


struct sortable_keys {
	char *key;
	float value;
};

static char *function_fieldqty(struct opbx_channel *chan, char *cmd, int argc, char **argv, char *buf, size_t len)
{
	char *varval, workspace[256];
	int fieldcount = 0;

	if (argc != 2 || !argv[0][0] || !argv[1][0]) {
		opbx_LOG(LOG_ERROR, "Syntax: %s\n", fieldqty_func_syntax);
		return NULL;
	}

	pbx_retrieve_variable(chan, argv[0], &varval, workspace, sizeof(workspace), NULL);
	while (strsep(&varval, argv[1]))
		fieldcount++;
	snprintf(buf, len, "%d", fieldcount);
	return buf;
}


static char *builtin_function_regex(struct opbx_channel *chan, char *cmd, int argc, char **argv, char *buf, size_t len) 
{
	char *arg, *earg = NULL, *tmp, errstr[256] = "";
	int errcode;
	regex_t regexbuf;

	opbx_copy_string(buf, "0", len);
	
	/* Regex in quotes */
	arg = strchr(argv[0], '"');
	if (arg) {
		arg++;
		earg = strrchr(arg, '"');
		if (earg) {
			*earg++ = '\0';
			/* Skip over any spaces before the data we are checking */
			while (*earg == ' ')
				earg++;
		}
	} else {
		arg = argv[0];
	}

	if ((errcode = regcomp(&regexbuf, arg, REG_EXTENDED | REG_NOSUB))) {
		regerror(errcode, &regexbuf, errstr, sizeof(errstr));
		opbx_log(LOG_WARNING, "Malformed input %s(%s): %s\n", cmd, argv[0], errstr);
	} else {
		if (!regexec(&regexbuf, earg ? earg : "", 0, NULL, 0))
			opbx_copy_string(buf, "1", len); 
	}
	regfree(&regexbuf);

	return buf;
}


static char *builtin_function_len(struct opbx_channel *chan, char *cmd, int argc, char **argv, char *buf, size_t len) 
{
	int length = 0;
	if (argv[0]) {
		length = strlen(argv[0]);
	}
	snprintf(buf, len, "%d", length);
	return buf;
}


static char *acf_strftime(struct opbx_channel *chan, char *cmd, int argc, char **argv, char *buf, size_t len) 
{
	char *format, *epoch, *timezone = NULL;
	long epochi;
	struct tm time;

	if (argc < 1 || !argv[0][0] || !sscanf(epoch, "%ld", &epochi)) {
		struct timeval tv = opbx_tvnow();
		epochi = tv.tv_sec;
	}

	buf[0] = '\0';

	opbx_localtime(&epochi, &time, (argc > 1 && argv[1][0] ? argv[1] : NULL));

	if (!strftime(buf, len, (argc > 2 && argv[2][0] ? argv[2] : "%c"), &time)) {
		opbx_log(LOG_WARNING, "C function strftime() output nothing?!!\n");
	}
	buf[len - 1] = '\0';

	return buf;
}


static char *function_eval(struct opbx_channel *chan, char *cmd, int argc, char **argv, char *buf, size_t len) 
{
	if (argc != 1 || !argv[0][0]) {
		opbx_log(LOG_ERROR, "Syntax: %s\n", eval_func_syntax);
		return NULL;
	}

	pbx_substitute_variables_helper(chan, argv[0], buf, len);

	return buf;
}


static char *function_cut(struct opbx_channel *chan, char *cmd, int argc, char **argv, char *buf, size_t len)
{
	char varvalue[MAXRESULT], *tmp2;
	char *s, *field=NULL;
	char *tmp;
	char d, ds[2];
	int curfieldnum;


	if (argc != 3 || !argv[0][0] || !argv[2][0]) {
		opbx_log(LOG_ERROR, "Syntax: %s\n", cut_func_syntax);
		return NULL;
	}

	tmp = alloca(strlen(argv[0]) + 4);
	snprintf(tmp, strlen(argv[0]) + 4, "${%s}", argv[0]);

	d = (argc > 1 && argv[1][0] ? argv[1][0] : '-');
	field = (argc > 2 && argv[2] ? argv[2] : "1");

	/* String form of the delimiter, for use with strsep(3) */
	snprintf(ds, sizeof(ds), "%c", d);

	pbx_substitute_variables_helper(chan, tmp, varvalue, sizeof(varvalue));

	tmp2 = varvalue;
	curfieldnum = 1;
	while ((tmp2 != NULL) && (field != NULL)) {
		char *nextgroup = strsep(&field, "&");
		int num1 = 0, num2 = MAXRESULT;
		char trashchar;

		if (sscanf(nextgroup, "%d-%d", &num1, &num2) == 2) {
			/* range with both start and end */
		} else if (sscanf(nextgroup, "-%d", &num2) == 1) {
		/* range with end */
			num1 = 0;
		} else if ((sscanf(nextgroup, "%d%c", &num1, &trashchar) == 2) && (trashchar == '-')) {
			/* range with start */
			num2 = MAXRESULT;
		} else if (sscanf(nextgroup, "%d", &num1) == 1) {
			/* single number */
			num2 = num1;
		} else {
			opbx_log(LOG_ERROR, "Usage: CUT(<varname>,<char-delim>,<range-spec>)\n");
			return buf;
		}

		/* Get to start, if any */
		if (num1 > 0) {
			while ((tmp2 != (char *)NULL + 1) && (curfieldnum < num1)) {
				tmp2 = index(tmp2, d) + 1;
				curfieldnum++;
			}
		}

		/* Most frequent problem is the expectation of reordering fields */
		if ((num1 > 0) && (curfieldnum > num1)) {
			opbx_log(LOG_WARNING, "We're already past the field you wanted?\n");
		}

		/* Re-null tmp2 if we added 1 to NULL */
		if (tmp2 == (char *)NULL + 1)
			tmp2 = NULL;

		/* Output fields until we either run out of fields or num2 is reached */
		while ((tmp2 != NULL) && (curfieldnum <= num2)) {
			char *tmp3 = strsep(&tmp2, ds);
			int curlen = strlen(buf);

			if (curlen) {
				snprintf(buf + curlen, len - curlen, "%c%s", d, tmp3);
			} else {
				snprintf(buf, len, "%s", tmp3);
			}

			curfieldnum++;
		}
	}
	return buf;
}


static int sort_subroutine(const void *arg1, const void *arg2)
{
	const struct sortable_keys *one=arg1, *two=arg2;
	if (one->value < two->value) {
		return -1;
	} else if (one->value == two->value) {
		return 0;
	} else {
		return 1;
	}
}

static char *function_sort(struct opbx_channel *chan, char *cmd, int argc, char **argv, char *buf, size_t len)
{
	struct sortable_keys *sortable_keys;
	char *p;
	int count2;

	if (argc < 1 || !argv[0][0]) {
		opbx_log(LOG_ERROR, "Syntax: %s\n", sort_func_syntax);
		return NULL;
	}

	sortable_keys = alloca(argc * sizeof(struct sortable_keys));
	memset(sortable_keys, 0, argc * sizeof(struct sortable_keys));

	/* Parse each into a struct */
	count2 = 0;
	for (; argc; argv++, argc--) {
		if (!(p= strchr(argv[0], ':')))
			continue;
		*(p++) = '\0';
		sortable_keys[count2].key = argv[0];
		sscanf(p, "%f", &sortable_keys[count2].value);
		count2++;
	}

	if (count2 > 0) {
		int i, l, first = 1;

		/* Sort the structs */
		qsort(sortable_keys, count2, sizeof(struct sortable_keys), sort_subroutine);

		len--; /* one for the terminating null */
		p = buf;
		for (i = 0; len && i < count2; i++) {
			if (len > 0 && !first) {
				*(p++) = ',';
				len--;
			} else
				first = 0;
			l = strlen(sortable_keys[i].key);
			if (l > len)
				l = len;
			memcpy(p, sortable_keys[i].key, l);
			p += l;
			len -= l;
		}
	}
	*p = '\0';

	return buf;
}


static char *tdesc = "string functions";

int unload_module(void)
{
        int res = 0;

	res |= opbx_unregister_function(fieldqty_function);
	res |= opbx_unregister_function(regex_function);
	res |= opbx_unregister_function(len_function);
	res |= opbx_unregister_function(strftime_function);
	res |= opbx_unregister_function(eval_function);
	res |= opbx_unregister_function(cut_function);
	res |= opbx_unregister_function(sort_function);

        return res;
}

int load_module(void)
{
	fieldqty_function = opbx_register_function(fieldqty_func_name, function_fieldqty, NULL, fieldqty_func_synopsis, fieldqty_func_syntax, fieldqty_func_desc);
	regex_function = opbx_register_function(regex_func_name, builtin_function_regex, NULL, regex_func_synopsis, regex_func_syntax, regex_func_desc);
	len_function = opbx_register_function(len_func_name, builtin_function_len, NULL, len_func_synopsis, len_func_syntax, len_func_desc);
	strftime_function = opbx_register_function(strftime_func_name, acf_strftime, NULL, strftime_func_synopsis, strftime_func_syntax, strftime_func_desc);
	eval_function = opbx_register_function(eval_func_name, function_eval, NULL, eval_func_synopsis, eval_func_syntax, eval_func_desc);
	cut_function = opbx_register_function(cut_func_name, function_cut, NULL, cut_func_synopsis, cut_func_syntax, cut_func_desc);
	sort_function = opbx_register_function(sort_func_name, function_sort, NULL, sort_func_synopsis, sort_func_syntax, sort_func_desc);

        return 0;
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	return 0;
}

/*
Local Variables:
mode: C
c-file-style: "linux"
indent-tabs-mode: nil
End:
*/
