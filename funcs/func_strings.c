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
static const char fieldqty_func_name[] = "FIELDQTY";
static const char fieldqty_func_synopsis[] = "Count the fields, with an arbitrary delimiter";
static const char fieldqty_func_syntax[] = "FIELDQTY(varname, delim)";
static const char fieldqty_func_desc[] = "";

static void *filter_function;
static const char filter_func_name[] = "FILTER";
static const char filter_func_synopsis[] = "Filter the string to include only the allowed characters";
static const char filter_func_syntax[] = "FILTER(allowed-chars, string)";
static const char filter_func_desc[] = "";

static void *regex_function;
static const char regex_func_name[] = "REGEX";
static const char regex_func_synopsis[] = "Match data against a regular expression";
static const char regex_func_syntax[] = "REGEX(\"regular expression\", \"data\"[, ...])";
static const char regex_func_desc[] =
"Test each item of data against the given regular expression.\n"
"If the first item matches return 1, if the second item matches\n"
"return 2, etc. In general, if the nth item matches return n.\n"
"If no data item matches return 0\n";

static void *len_function;
static const char len_func_name[] = "LEN";
static const char len_func_synopsis[] = "Returns the length of the argument given";
static const char len_func_syntax[] = "LEN(string)";
static const char len_func_desc[] = "";

static void *strftime_function;
static const char strftime_func_name[] = "STRFTIME";
static const char strftime_func_synopsis[] = "Returns the current date/time in a specified format.";
static const char strftime_func_syntax[] = "STRFTIME([epoch[, timezone[, format]]])";
static const char strftime_func_desc[] = "";

static void *eval_function;
static const char eval_func_name[] = "EVAL";
static const char eval_func_synopsis[] = "Evaluate stored variables.";
static const char eval_func_syntax[] = "EVAL(variable)";
static const char eval_func_desc[] =
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
static const char cut_func_name[] = "CUT";
static const char cut_func_synopsis[] = "Slices and dices strings, based upon a named delimiter.";
static const char cut_func_syntax[] = "CUT(varname, char-delim, range-spec)";
static const char cut_func_desc[] =
	"  varname    - variable you want cut\n"
	"  char-delim - defaults to '-'\n"
	"  range-spec - number of the field you want (1-based offset)\n"
	"             may also be specified as a range (with -)\n"
	"             or group of ranges and fields (with &)\n";

static void *sort_function;
static const char sort_func_name[] = "SORT";
static const char sort_func_synopsis[] = "Sorts a list of key/vals into a list of keys, based upon the vals";
static const char sort_func_syntax[] = "SORT(key1:val1[...][, keyN:valN])";
static const char sort_func_desc[] =
	"Takes a comma-separated list of keys and values, each separated by a colon, and returns a\n"
	"comma-separated list of the keys, sorted by their values.  Values will be evaluated as\n"
	"floating-point numbers.\n";


struct sortable_keys {
	char *key;
	float value;
};

static int function_fieldqty(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	if (argc != 2 || !argv[0][0] || !argv[1][0])
		return cw_function_syntax(fieldqty_func_syntax);

	if (result) {
		struct cw_dynstr ds = CW_DYNSTR_INIT;
		int fieldcount = 0;

		if (!pbx_retrieve_variable(chan, NULL, argv[0], strlen(argv[0]), &ds, 0, 0)) {
			if (!ds.error) {
				char *p = ds.data;

				/* FIXME: should we use cw_separate_app_args here to get quoting right */
				while (strsep(&p, argv[1]))
					fieldcount++;
				cw_dynstr_printf(result, "%d", fieldcount);
			} else
				result->error = 1;
		}

		cw_dynstr_free(&ds);
	}

	return 0;
}


static int function_filter(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(chan);

	if (argc != 2 || !argv[0][0] || !argv[1][0]) {
		cw_log(CW_LOG_ERROR, "Syntax: %s\n", filter_func_syntax);
		return -1;
	}

	if (result) {
		char *s = argv[1];
		while (*s) {
			int n = strcspn(s, argv[0]);
			cw_dynstr_printf(result, "%.*s", n, s);
			if (!s[n])
				break;
			s += n + 1;
		}
	}

	return 0;
}


static int builtin_function_regex(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	char errstr[256] = "";
	regex_t regexbuf;
	int i, match;
	int ret = -1;

	CW_UNUSED(chan);

	if (argc < 2 || !argv[0][0] || !argv[1])
		return cw_function_syntax(regex_func_syntax);

	if (result) {
		if (!(i = regcomp(&regexbuf, argv[0], REG_EXTENDED | REG_NOSUB))) {
			match = 0;
			for (i = 1; i < argc; i++) {
				if (!regexec(&regexbuf, argv[i], 0, NULL, 0)) {
					match = i;
					break;
				}
			}

			cw_dynstr_printf(result, "%d", match);

			regfree(&regexbuf);
			ret = 0;
		} else {
			regerror(i, &regexbuf, errstr, sizeof(errstr));
			cw_log(CW_LOG_ERROR, "Malformed input %s(%s): %s\n", regex_func_name, argv[0], errstr);
		}
	} else {
		cw_log(CW_LOG_ERROR, "%s should only be used in an expression context\n", regex_func_name);
	}

	return ret;
}


static int builtin_function_len(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(chan);
	CW_UNUSED(argc);

	if (result)
		cw_dynstr_printf(result, "%lu", (unsigned long)(argv[0] ? strlen(argv[0]) : 0UL));

	return 0;
}


static int acf_strftime(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct tm now;
	struct timeval tv;
	char *tz = NULL;
	const char *format = "%c";
	size_t mark;
	int need, n;

	CW_UNUSED(chan);

	if (argc < 1 || !argv[0][0] || !sscanf(argv[0], "%ld", &tv.tv_sec))
		tv = cw_tvnow();

	if (argc > 1 && argv[1][0]) tz = argv[1];
	if (argc > 2 && argv[2][0]) format = argv[2];

	cw_localtime(&tv.tv_sec, &now, tz);

	mark = cw_dynstr_end(result);
	need = 0;
	do {
		cw_dynstr_truncate(result, mark);

		need += 256;
		cw_dynstr_need(result, need);
		if (result->error)
			break;

		n = strftime(result->data, need, format, &now);
	} while (n == 0 || n == need);

	return 0;
}


static int function_eval(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	if (argc != 1 || !argv[0][0])
		return cw_function_syntax(eval_func_syntax);

	pbx_substitute_variables(chan, (chan ? &chan->vars : NULL), argv[0], result);
	return 0;
}


static int function_cut(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct cw_dynstr varvalue = CW_DYNSTR_INIT;
	char one[] = "1";
	char *tmp2;
	char *field=NULL;
	char *tmp;
	int curfieldnum;
	int first = 1;
	char d, ds[2];

	if (argc != 3 || !argv[0][0] || !argv[2][0])
		return cw_function_syntax(cut_func_syntax);

	if (result) {
		tmp = alloca(strlen(argv[0]) + 4);
		snprintf(tmp, strlen(argv[0]) + 4, "${%s}", argv[0]);

		d = (argc > 1 && argv[1][0] ? argv[1][0] : '-');
		field = (argc > 2 && argv[2] ? argv[2] : one);

		/* String form of the delimiter, for use with strsep(3) */
		snprintf(ds, sizeof(ds), "%c", d);

		pbx_substitute_variables(chan, (chan ? &chan->vars : NULL), tmp, &varvalue);

		tmp2 = varvalue.data;
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
				cw_log(CW_LOG_ERROR, "Usage: CUT(<varname>,<char-delim>,<range-spec>)\n");
				return -1;
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
				cw_log(CW_LOG_WARNING, "We're already past the field you wanted?\n");
			}

			/* Re-null tmp2 if we added 1 to NULL */
			if (tmp2 == (char *)NULL + 1)
				tmp2 = NULL;

			/* Output fields until we either run out of fields or num2 is reached */
			while ((tmp2 != NULL) && (curfieldnum <= num2)) {
				char *tmp3 = strsep(&tmp2, ds);

				cw_dynstr_printf(result, "%s%s", (!first ? ds : ""), tmp3);

				first = 0;
				curfieldnum++;
			}
		}

		cw_dynstr_free(&varvalue);
	}

	return 0;
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

static int function_sort(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct sortable_keys *sortable_keys;
	char *p;
	int count2;

	CW_UNUSED(chan);

	if (argc < 1 || !argv[0][0])
		return cw_function_syntax(sort_func_syntax);

	if (result) {
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
			int i;

			/* Sort the structs */
			qsort(sortable_keys, count2, sizeof(struct sortable_keys), sort_subroutine);

			cw_dynstr_printf(result, "%s", sortable_keys[0].key);
			for (i = 1; i < count2; i++)
				cw_dynstr_printf(result, ",%s", sortable_keys[i].key);
		}
	}

	return 0;
}


static const char tdesc[] = "string functions";

static int unload_module(void)
{
        int res = 0;

	res |= cw_unregister_function(fieldqty_function);
	res |= cw_unregister_function(filter_function);
	res |= cw_unregister_function(regex_function);
	res |= cw_unregister_function(len_function);
	res |= cw_unregister_function(strftime_function);
	res |= cw_unregister_function(eval_function);
	res |= cw_unregister_function(cut_function);
	res |= cw_unregister_function(sort_function);

        return res;
}

static int load_module(void)
{
	fieldqty_function = cw_register_function(fieldqty_func_name, function_fieldqty, fieldqty_func_synopsis, fieldqty_func_syntax, fieldqty_func_desc);
	filter_function = cw_register_function(filter_func_name, function_filter, filter_func_synopsis, filter_func_syntax, filter_func_desc);
	regex_function = cw_register_function(regex_func_name, builtin_function_regex, regex_func_synopsis, regex_func_syntax, regex_func_desc);
	len_function = cw_register_function(len_func_name, builtin_function_len, len_func_synopsis, len_func_syntax, len_func_desc);
	strftime_function = cw_register_function(strftime_func_name, acf_strftime, strftime_func_synopsis, strftime_func_syntax, strftime_func_desc);
	eval_function = cw_register_function(eval_func_name, function_eval, eval_func_synopsis, eval_func_syntax, eval_func_desc);
	cut_function = cw_register_function(cut_func_name, function_cut, cut_func_synopsis, cut_func_syntax, cut_func_desc);
	sort_function = cw_register_function(sort_func_name, function_sort, sort_func_synopsis, sort_func_syntax, sort_func_desc);

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
