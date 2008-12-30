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
 * \brief Dial plan proc Implementation
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/options.h"
#include "callweaver/config.h"
#include "callweaver/utils.h"
#include "callweaver/lock.h"
#include "callweaver/keywords.h"

#define MAX_ARGS 80

/* special result value used to force proc exit */
#define PROC_EXIT_RESULT 1024

static const char tdesc[] = "Extension Procs";

static void *proc_app;
static const char proc_name[] = "Proc";
static const char proc_synopsis[] = "Proc Implementation";
static const char proc_syntax[] = "Proc(procname, arg1, arg2 ...)";
static const char proc_descrip[] =
"Executes a procedure using the context\n"
"'proc-<procname>', jumping to the 's' extension of that context and\n"
"executing each step, then returning when the steps end. \n"
"The calling extension, context, and priority are stored in ${PROC_EXTEN}, \n"
"${PROC_CONTEXT} and ${PROC_PRIORITY} respectively.  Arguments become\n"
"${ARG1}, ${ARG2}, etc in the proc context.\n"
"If you Goto out of the Proc context, the Proc will terminate and control\n"
"will be returned at the location of the Goto.\n"
"Proc returns -1 if any step in the proc returns -1, and 0 otherwise.\n" 
"If ${PROC_OFFSET} is set at termination, Proc will attempt to continue\n"
"at priority PROC_OFFSET + N + 1 if such a step exists, and N + 1 otherwise.\n";

static void *if_app;
static const char if_name[] = "ProcIf";
static const char if_synopsis[] = "Conditional Proc Implementation";
static const char if_syntax[] = "ProcIf(expr ? procname_a[, arg ...] [: procname_b[, arg ...]])";
static const char if_descrip[] =
"Executes proc defined in <procname_a> if <expr> is true\n"
"(otherwise <procname_b> if provided)\n"
"Arguments and return values as in application proc()\n";

static void *exit_app;
static const char exit_name[] = "ProcExit";
static const char exit_synopsis[] = "Exit From Proc";
static const char exit_syntax[] = "ProcExit()";
static const char exit_descrip[] =
"Causes the currently running proc to exit as if it had\n"
"ended normally by running out of priorities to execute.\n"
"If used outside a proc, will likely cause unexpected\n"
"behavior.\n";


static int proc_exec(struct cw_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	struct cw_var_t *var;
	char *proc;
	char fullproc[80];
	char varname[80];
	char *oldargs[MAX_ARGS + 1] = { NULL, };
	int x;
	int res=0;
	char oldexten[256]="";
	int oldpriority;
	char pc[80], depthc[12];
	char oldcontext[CW_MAX_CONTEXT] = "";
	int offset, depth;
	int setproccontext=0;
	int autoloopflag;
  
	char *save_proc_exten;
	char *save_proc_context;
	char *save_proc_priority;
	char *save_proc_offset;
	struct localuser *u;
 
	if (argc < 1)
		return cw_function_syntax(proc_syntax);

	LOCAL_USER_ADD(u);

	/* Count how many levels deep the rabbit hole goes */
	depth = 0;
	if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_PROC_DEPTH, "PROC_DEPTH"))) {
		sscanf(var->value, "%d", &depth);
		cw_object_put(var);
	}

	if (depth >= 7) {
		cw_log(CW_LOG_ERROR, "Proc():  possible infinite loop detected.  Returning early.\n");
		LOCAL_USER_REMOVE(u);
		return 0;
	}
	snprintf(depthc, sizeof(depthc), "%d", depth + 1);
	pbx_builtin_setvar_helper(chan, "PROC_DEPTH", depthc);

	proc = argv[0];
	if (cw_strlen_zero(proc)) {
		cw_log(CW_LOG_WARNING, "Invalid proc name specified\n");
		LOCAL_USER_REMOVE(u);
		return 0;
	}
	snprintf(fullproc, sizeof(fullproc), "proc-%s", proc);
	if (!cw_exists_extension(chan, fullproc, "s", 1, chan->cid.cid_num)) {
  		if (!cw_context_find(fullproc)) 
			cw_log(CW_LOG_WARNING, "No such context '%s' for proc '%s'\n", fullproc, proc);
		else
	  		cw_log(CW_LOG_WARNING, "Context '%s' for proc '%s' lacks 's' extension, priority 1\n", fullproc, proc);
		LOCAL_USER_REMOVE(u);
		return 0;
	}
	
	/* Save old info */
	oldpriority = chan->priority;
	cw_copy_string(oldexten, chan->exten, sizeof(oldexten));
	cw_copy_string(oldcontext, chan->context, sizeof(oldcontext));
	if (cw_strlen_zero(chan->proc_context)) {
		cw_copy_string(chan->proc_context, chan->context, sizeof(chan->proc_context));
		cw_copy_string(chan->proc_exten, chan->exten, sizeof(chan->proc_exten));
		chan->proc_priority = chan->priority;
		setproccontext=1;
	}

	/* Save old proc variables */
	save_proc_exten = save_proc_context = save_proc_priority = save_proc_offset = NULL;

	if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_PROC_EXTEN, "PROC_EXTEN"))) {
		save_proc_exten = strdup(var->value);
		cw_object_put(var);
	}
	pbx_builtin_setvar_helper(chan, "PROC_EXTEN", oldexten);

	if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_PROC_CONTEXT, "PROC_CONTEXT"))) {
		save_proc_context = strdup(var->value);
		cw_object_put(var);
	}
	pbx_builtin_setvar_helper(chan, "PROC_CONTEXT", oldcontext);

	if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_PROC_PRIORITY, "PROC_PRIORITY"))) {
		save_proc_priority = strdup(var->value);
		cw_object_put(var);
	}
	snprintf(pc, sizeof(pc), "%d", oldpriority);
	pbx_builtin_setvar_helper(chan, "PROC_PRIORITY", pc);
  
	if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_PROC_OFFSET, "PROC_OFFSET"))) {
		save_proc_offset = strdup(var->value);
		cw_object_put(var);
	}
	pbx_builtin_setvar_helper(chan, "PROC_OFFSET", NULL);

	/* Setup environment for new run */
	chan->exten[0] = 's';
	chan->exten[1] = '\0';
	cw_copy_string(chan->context, fullproc, sizeof(chan->context));
	chan->priority = 1;

	for (x = 1; x < argc; x++) {
  		/* Save copy of old arguments if we're overwriting some, otherwise
	   	let them pass through to the other macro */
  		snprintf(varname, sizeof(varname), "ARG%d", x);
		if ((var = pbx_builtin_getvar_helper(chan, cw_hash_var_name(varname), varname))) {
			oldargs[x] = strdup(var->value);
			cw_object_put(var);
		}
		pbx_builtin_setvar_helper(chan, varname, argv[x]);
	}
	autoloopflag = cw_test_flag(chan, CW_FLAG_IN_AUTOLOOP);
	cw_set_flag(chan, CW_FLAG_IN_AUTOLOOP);
	while(cw_exists_extension(chan, chan->context, chan->exten, chan->priority, chan->cid.cid_num)) {
		/* Reset the proc depth, if it was changed in the last iteration */
		pbx_builtin_setvar_helper(chan, "PROC_DEPTH", depthc);
		if ((res = cw_exec_extension(chan, chan->context, chan->exten, chan->priority, chan->cid.cid_num))) {
			/* Something bad happened, or a hangup has been requested. */
			if (((res >= '0') && (res <= '9')) || ((res >= 'A') && (res <= 'F')) ||
		    	(res == '*') || (res == '#')) {
				/* Just return result as to the previous application as if it had been dialed */
				cw_log(CW_LOG_DEBUG, "Oooh, got something to jump out with ('%c')!\n", res);
				break;
			}
			switch(res) {
	        	case PROC_EXIT_RESULT:
				res = 0;
				goto out;
			case CW_PBX_KEEPALIVE:
				if (option_debug)
					cw_log(CW_LOG_DEBUG, "Spawn extension (%s,%s,%d) exited KEEPALIVE in proc %s on '%s'\n", chan->context, chan->exten, chan->priority, proc, chan->name);
				if (option_verbose > 1)
					cw_verbose( VERBOSE_PREFIX_2 "Spawn extension (%s, %s, %d) exited KEEPALIVE in proc '%s' on '%s'\n", chan->context, chan->exten, chan->priority, proc, chan->name);
				goto out;
				break;
			default:
				if (option_debug)
					cw_log(CW_LOG_DEBUG, "Spawn extension (%s,%s,%d) exited non-zero on '%s' in proc '%s'\n", chan->context, chan->exten, chan->priority, chan->name, proc);
				if (option_verbose > 1)
					cw_verbose( VERBOSE_PREFIX_2 "Spawn extension (%s, %s, %d) exited non-zero on '%s' in proc '%s'\n", chan->context, chan->exten, chan->priority, chan->name, proc);
				goto out;
			}
		}
		if (strcasecmp(chan->context, fullproc)) {
			if (option_verbose > 1)
				cw_verbose(VERBOSE_PREFIX_2 "Channel '%s' jumping out of proc '%s'\n", chan->name, proc);
			break;
		}
		/* don't stop executing extensions when we're in "h" */
		if (chan->_softhangup && strcasecmp(oldexten,"h")) {
			cw_log(CW_LOG_DEBUG, "Extension %s, priority %d returned normally even though call was hung up\n",
				chan->exten, chan->priority);
			goto out;
		}
		chan->priority++;
  	}
	out:
	/* Reset the depth back to what it was when the routine was entered (like if we called Proc recursively) */
	snprintf(depthc, sizeof(depthc), "%d", depth);
	pbx_builtin_setvar_helper(chan, "PROC_DEPTH", depthc);

	cw_set2_flag(chan, autoloopflag, CW_FLAG_IN_AUTOLOOP);
  	for (x=1; x<argc; x++) {
  		/* Restore old arguments and delete ours */
		snprintf(varname, sizeof(varname), "ARG%d", x);
  		if (oldargs[x]) {
			pbx_builtin_setvar_helper(chan, varname, oldargs[x]);
			free(oldargs[x]);
		} else {
			pbx_builtin_setvar_helper(chan, varname, NULL);
		}
  	}

	/* Restore proc variables */
	pbx_builtin_setvar_helper(chan, "PROC_EXTEN", save_proc_exten);
	if (save_proc_exten)
		free(save_proc_exten);
	pbx_builtin_setvar_helper(chan, "PROC_CONTEXT", save_proc_context);
	if (save_proc_context)
		free(save_proc_context);
	pbx_builtin_setvar_helper(chan, "PROC_PRIORITY", save_proc_priority);
	if (save_proc_priority)
		free(save_proc_priority);
	if (setproccontext) {
		chan->proc_context[0] = '\0';
		chan->proc_exten[0] = '\0';
		chan->proc_priority = 0;
	}

	if (!strcasecmp(chan->context, fullproc)) {
  		/* If we're leaving the proc normally, restore original information */
		chan->priority = oldpriority;
		cw_copy_string(chan->context, oldcontext, sizeof(chan->context));
		if (!(chan->_softhangup & CW_SOFTHANGUP_ASYNCGOTO)) {
			/* Copy the extension, so long as we're not in softhangup, where we could be given an asyncgoto */
			cw_copy_string(chan->exten, oldexten, sizeof(chan->exten));
			if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_PROC_OFFSET, "PROC_OFFSET"))) {
				/* Handle proc offset if it's set by checking the availability of step n + offset + 1, otherwise continue
			   	normally if there is any problem */
				if (sscanf(var->value, "%d", &offset) == 1) {
					if (cw_exists_extension(chan, chan->context, chan->exten, chan->priority + offset + 1, chan->cid.cid_num)) {
						chan->priority += offset;
					}
				}
			}
		}
	}

	pbx_builtin_setvar_helper(chan, "PROC_OFFSET", save_proc_offset);
	if (save_proc_offset)
		free(save_proc_offset);
	LOCAL_USER_REMOVE(u);
	return res;
}

static int procif_exec(struct cw_channel *chan, int argc, char **argv, char *result, size_t result_max) 
{
	char *s, *q;
	int i;

	/* First argument is "<condition ? ..." */
	if (argc < 1 || !(s = strchr(argv[0], '?')))
		return cw_function_syntax(if_syntax);

	/* Trim trailing space from the condition */
	q = s;
	do { *(q--) = '\0'; } while (q >= argv[0] && isspace(*q));

	do { *(s++) = '\0'; } while (isspace(*s));

	if (pbx_checkcondition(argv[0])) {
		/* True: we want everything between '?' and ':' */
		argv[0] = s;
		for (i = 0; i < argc; i++) {
			if ((s = strchr(argv[i], ':'))) {
				do { *(s--) = '\0'; } while (s >= argv[i] && isspace(*s));
				argc = i + 1;
				break;
			}
		}
		return proc_exec(chan, argc, argv, NULL, 0);
	} else {
		/* False: we want everything after ':' (if anything) */
		argv[0] = s;
		for (i = 0; i < argc; i++) {
			if ((s = strchr(argv[i], ':'))) {
				do { *(s++) = '\0'; } while (isspace(*s));
				argv[i] = s;
				return proc_exec(chan, argc - i, argv + i, NULL, 0);
			}
		}
		/* No ": ..." so we just drop through */
		return 0;
	}
}
			
static int proc_exit_exec(struct cw_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	return PROC_EXIT_RESULT;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(if_app);
	res |= cw_unregister_function(exit_app);
	res |= cw_unregister_function(proc_app);
	return res;
}

static int load_module(void)
{
	exit_app = cw_register_function(exit_name, proc_exit_exec, exit_synopsis, exit_syntax, exit_descrip);
	if_app = cw_register_function(if_name, procif_exec, if_synopsis, if_syntax, if_descrip);
	proc_app = cw_register_function(proc_name, proc_exec, proc_synopsis, proc_syntax, proc_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
