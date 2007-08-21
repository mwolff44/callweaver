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
 * \brief Populate and remember extensions from static config file
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/pbx.h"
#include "callweaver/config.h"
#include "callweaver/options.h"
#include "callweaver/module.h"
#include "callweaver/logger.h"
#include "callweaver/cli.h"
#include "callweaver/phone_no_utils.h"

#ifdef __OPBX_DEBUG_MALLOC
static void FREE(void *ptr)
{
	free(ptr);
}
#else
#define FREE free
#endif

static const char dtext[] = "Text Extension Configuration";
static char *config = "extensions.conf";
static char *registrar = "pbx_config";

static int static_config = 0;
static int write_protect_config = 1;
static int autofallthrough_config = 0;
static int clearglobalvars_config = 0;

OPBX_MUTEX_DEFINE_STATIC(save_dialplan_lock);

static struct opbx_context *local_contexts = NULL;

/*
 * Help for commands provided by this module ...
 */
static char context_dont_include_help[] =
"Usage: dont include <context> in <context>\n"
"       Remove an included context from another context.\n";

static char context_remove_extension_help[] =
"Usage: remove extension exten@context [priority]\n"
"       Remove an extension from a given context. If a priority\n"
"       is given, only that specific priority from the given extension\n"
"       will be removed.\n";

static char context_add_include_help[] =
"Usage: include <context> in <context>\n"
"       Include a context in another context.\n";

static char save_dialplan_help[] =
"Usage: save dialplan [/path/to/extension/file]\n"
"       Save dialplan created by pbx_config module.\n"
"\n"
"Example: save dialplan                 (/etc/callweaver/extensions.conf)\n"
"         save dialplan /home/markster  (/home/markster/extensions.conf)\n";

static char context_add_extension_help[] =
"Usage: add extension <exten>,<priority>,<app>,<app-data> into <context>\n"
"       [replace]\n\n"
"       This command will add new extension into <context>. If there is an\n"
"       existence of extension with the same priority and last 'replace'\n"
"       arguments is given here we simply replace this extension.\n"
"\n"
"Example: add extension 6123,1,Dial,IAX/216.207.245.56/6123 into local\n"
"         Now, you can dial 6123 and talk to Markster :)\n";

static char context_add_ignorepat_help[] =
"Usage: add ignorepat <pattern> into <context>\n"
"       This command adds a new ignore pattern into context <context>\n"
"\n"
"Example: add ignorepat _3XX into local\n";

static char context_remove_ignorepat_help[] =
"Usage: remove ignorepat <pattern> from <context>\n"
"       This command removes an ignore pattern from context <context>\n"
"\n"
"Example: remove ignorepat _3XX from local\n";

static char reload_extensions_help[] =
"Usage: reload extensions.conf without reloading any other modules\n"
"       This command does not delete global variables unless\n"
"       clearglobalvars is set to yes in extensions.conf\n"
"\n"
"Example: extensions reload\n";


/*
 * Implementation of functions provided by this module
 */

/*
 * REMOVE INCLUDE command stuff
 */
static int handle_context_dont_include(int fd, int argc, char *argv[])
{
	if (argc != 5) return RESULT_SHOWUSAGE;

	if (strcmp(argv[3], "in")) return RESULT_SHOWUSAGE;

	if (!opbx_context_remove_include(argv[4], argv[2], registrar)) {
		opbx_cli(fd, "We are not including '%s' in '%s' now\n",
			argv[2], argv[4]);
		return RESULT_SUCCESS;
	}

	opbx_cli(fd, "Failed to remove '%s' include from '%s' context\n",
		argv[2], argv[4]);
	return RESULT_FAILURE;
}

static char *complete_context_dont_include(char *line, char *word,
	int pos, int state)
{
	int which = 0;

	/*
	 * Context completion ...
	 */
	if (pos == 2) {
		struct opbx_context *c;

		if (opbx_lock_contexts()) {
			opbx_log(LOG_ERROR, "Failed to lock context list\n");
			return NULL;
		}

		/* walk pbx_get_contexts ... */
		c = opbx_walk_contexts(NULL); 
		while (c) {
			struct opbx_include *i;

			if (opbx_lock_context(c)) {
				c = opbx_walk_contexts(c);
				continue;
			}

			i = opbx_walk_context_includes(c, NULL);
			while (i) {
				if (!strlen(word) ||
					!strncmp(opbx_get_include_name(i), word, strlen(word))) {
					struct opbx_context *nc;
					int already_served = 0;

					/* check if this include is already served or not */

					/* go through all contexts again till we reach actuall
					 * context or already_served = 1
					 */
					nc = opbx_walk_contexts(NULL);
					while (nc && nc != c && !already_served) {
						if (!opbx_lock_context(nc)) {
							struct opbx_include *ni;

							ni = opbx_walk_context_includes(nc, NULL);
							while (ni && !already_served) {
								if (!strcmp(opbx_get_include_name(i),
									opbx_get_include_name(ni)))
									already_served = 1;
								ni = opbx_walk_context_includes(nc, ni);
							}	
							
							opbx_unlock_context(nc);
						}
						nc = opbx_walk_contexts(nc);
					}

					if (!already_served) {
						if (++which > state) {
							char *res =
								strdup(opbx_get_include_name(i));
							opbx_unlock_context(c);
							opbx_unlock_contexts();
							return res;
						}
					}
				}
				i = opbx_walk_context_includes(c, i);
			}

			opbx_unlock_context(c);
			c = opbx_walk_contexts(c);
		}

		opbx_unlock_contexts();
		return NULL;
	}

	/*
	 * 'in' completion ... (complete only if previous context is really
	 * included somewhere)
	 */
	if (pos == 3) {
		struct opbx_context *c;
		char *context, *dupline, *duplinet;

		if (state > 0) return NULL;

		/* take 'context' from line ... */
		if (!(dupline = strdup(line))) {
			opbx_log(LOG_ERROR, "Out of free memory\n");
			return NULL;
		}

		duplinet = dupline;
		strsep(&duplinet, " "); /* skip 'dont' */
		strsep(&duplinet, " "); /* skip 'include' */
		context = strsep(&duplinet, " ");

		if (!context) {
			free(dupline);
			return NULL;
		}

		if (opbx_lock_contexts()) {
			opbx_log(LOG_WARNING, "Failed to lock contexts list\n");
			free(dupline);
			return NULL;
		}

		/* go through all contexts and check if is included ... */
		c = opbx_walk_contexts(NULL);
		while (c) {
			struct opbx_include *i;
			if (opbx_lock_context(c)) {
				free(dupline);
				opbx_unlock_contexts();
				return NULL;
			}

			i = opbx_walk_context_includes(c, NULL);
			while (i) {
				/* is it our context? */
				if (!strcmp(opbx_get_include_name(i), context)) {
					/* yes, it is, context is really included, so
					 * complete "in" command
					 */
					free(dupline);
					opbx_unlock_context(c);
					opbx_unlock_contexts();
					return strdup("in");
				}
				i = opbx_walk_context_includes(c, i);
			}
			opbx_unlock_context(c);
			c = opbx_walk_contexts(c);
		}
		free(dupline);
		opbx_unlock_contexts();
		return NULL;
	}

	/*
	 * Context from which we removing include ... 
	 */
	if (pos == 4) {
		struct opbx_context *c;
		char *context, *dupline, *duplinet, *in;

		if (!(dupline = strdup(line))) {
			opbx_log(LOG_ERROR, "Out of free memory\n");
			return NULL;
		}

		duplinet = dupline;

		strsep(&duplinet, " "); /* skip 'dont' */
		strsep(&duplinet, " "); /* skip 'include' */

		if (!(context = strsep(&duplinet, " "))) {
			free(dupline);
			return NULL;
		}

		/* third word must be in */
		in = strsep(&duplinet, " ");
		if (!in ||
			strcmp(in, "in")) {
			free(dupline);
			return NULL;
		}

		if (opbx_lock_contexts()) {
			opbx_log(LOG_ERROR, "Failed to lock context list\n");
			free(dupline);
			return NULL;
		}

		/* walk through all contexts ... */
		c = opbx_walk_contexts(NULL);
		while (c) {
			struct opbx_include *i;
			if (opbx_lock_context(c)) {
				free(dupline);
				return NULL;
			}
	
			/* walk through all includes and check if it is our context */	
			i = opbx_walk_context_includes(c, NULL);
			while (i) {
				/* is in this context included another on which we want to
				 * remove?
				 */
				if (!strcmp(context, opbx_get_include_name(i))) {
					/* yes, it's included, is matching our word too? */
					if (!strncmp(opbx_get_context_name(c),
							word, strlen(word))) {
						/* check state for completion */
						if (++which > state) {
							char *res = strdup(opbx_get_context_name(c));
							free(dupline);
							opbx_unlock_context(c);
							opbx_unlock_contexts();
							return res;
						}
					}
					break;
				}
				i = opbx_walk_context_includes(c, i);
			}	
			opbx_unlock_context(c);
			c = opbx_walk_contexts(c);
		}

		free(dupline);
		opbx_unlock_contexts();
		return NULL;
	}

	return NULL;
}

/*
 * REMOVE EXTENSION command stuff
 */
static int handle_context_remove_extension(int fd, int argc, char *argv[])
{
	int removing_priority = 0;
	char *exten, *context;

	if (argc != 4 && argc != 3) return RESULT_SHOWUSAGE;

	/*
	 * Priority input checking ...
	 */
	if (argc == 4) {
		char *c = argv[3];

		/* check for digits in whole parameter for right priority ...
		 * why? because atoi (strtol) returns 0 if any characters in
		 * string and whole extension will be removed, it's not good
		 */
		if (strcmp("hint", c)) {
    		    while (*c != '\0') {
			if (!isdigit(*c++)) {
				opbx_cli(fd, "Invalid priority '%s'\n", argv[3]);
				return RESULT_FAILURE;
			}
		    }
		    removing_priority = atoi(argv[3]);
		} else
		    removing_priority = PRIORITY_HINT;

		if (removing_priority == 0) {
			opbx_cli(fd, "If you want to remove whole extension, please " \
				"omit priority argument\n");
			return RESULT_FAILURE;
		}
	}

	/*
	 * Format exten@context checking ...
	 */
	if (!(context = strchr(argv[2], (int)'@'))) {
		opbx_cli(fd, "First argument must be in exten@context format\n");
		return RESULT_FAILURE;
	}

	*context++ = '\0';
	exten = argv[2];
	if ((!strlen(exten)) || (!(strlen(context)))) {
		opbx_cli(fd, "Missing extension or context name in second argument '%s@%s'\n",
			exten == NULL ? "?" : exten, context == NULL ? "?" : context);
		return RESULT_FAILURE;
	}

	if (!opbx_context_remove_extension(context, exten, removing_priority, registrar)) {
		if (!removing_priority)
			opbx_cli(fd, "Whole extension %s@%s removed\n",
				exten, context);
		else
			opbx_cli(fd, "Extension %s@%s with priority %d removed\n",
				exten, context, removing_priority);
			
		return RESULT_SUCCESS;
	}

	opbx_cli(fd, "Failed to remove extension %s@%s\n", exten, context);

	return RESULT_FAILURE;
}

#define BROKEN_READLINE 1

#ifdef BROKEN_READLINE
/*
 * There is one funny thing, when you have word like 300@ and you hit
 * <tab>, you arguments will like as your word is '300 ', so it '@'
 * characters acts sometimes as word delimiter and sometimes as a part
 * of word
 *
 * This fix function, allocates new word variable and store here every
 * time xxx@yyy always as one word and correct pos is set too
 *
 * It's ugly, I know, but I'm waiting for Mark suggestion if upper is
 * bug or feature ...
 */
static int fix_complete_args(char *line, char **word, int *pos)
{
	char *_line, *_strsep_line, *_previous_word = NULL, *_word = NULL;
	int words = 0;

	_line = strdup(line);

	_strsep_line = _line;
	while (_strsep_line) {
		_previous_word = _word;
		_word = strsep(&_strsep_line, " ");

		if (_word && strlen(_word)) words++;
	}


	if (_word || _previous_word) {
		if (_word) {
			if (!strlen(_word)) words++;
			*word = strdup(_word);
		} else
			*word = strdup(_previous_word);
		*pos = words - 1;
		free(_line);
		return 0;
	}

	free(_line);
	return -1;
}
#endif /* BROKEN_READLINE */

static char *complete_context_remove_extension(char *line, char *word, int pos,
	int state)
{
	char *ret = NULL;
	int which = 0;

#ifdef BROKEN_READLINE
	/*
	 * Fix arguments, *word is a new allocated structure, REMEMBER to
	 * free *word when you want to return from this function ...
	 */
	if (fix_complete_args(line, &word, &pos)) {
		opbx_log(LOG_ERROR, "Out of free memory\n");
		return NULL;
	}
#endif

	/*
	 * exten@context completion ... 
	 */
	if (pos == 2) {
		struct opbx_context *c;
		struct opbx_exten *e;
		char *context = NULL, *exten = NULL, *delim = NULL;

		/* now, parse values from word = exten@context */
		if ((delim = strchr(word, (int)'@'))) {
			/* check for duplicity ... */
			if (delim != strrchr(word, (int)'@')) {
#ifdef BROKEN_READLINE
				free(word);
#endif
				return NULL;
			}

			*delim = '\0';
			exten = strdup(word);
			context = strdup(delim + 1);
			*delim = '@';
		} else {
			exten = strdup(word);
		}
#ifdef BROKEN_READLINE
		free(word);
#endif

		if (opbx_lock_contexts()) {
			opbx_log(LOG_ERROR, "Failed to lock context list\n");
			free(context); free(exten);
			return NULL;
		}

		/* find our context ... */
		c = opbx_walk_contexts(NULL); 
		while (c) {
			/* our context? */
			if ( (!context || !strlen(context)) ||                            /* if no input, all contexts ... */
				 (context && !strncmp(opbx_get_context_name(c),
				              context, strlen(context))) ) {                  /* if input, compare ... */
				/* try to complete extensions ... */
				e = opbx_walk_context_extensions(c, NULL);
				while (e) {
					/* our extension? */
					if ( (!exten || !strlen(exten)) ||                           /* if not input, all extensions ... */
						 (exten && !strncmp(opbx_get_extension_name(e), exten,
						                    strlen(exten))) ) { /* if input, compare ... */
						if (++which > state) {
							/* If there is an extension then return
							 * exten@context.
							 */
							if (exten) {
								ret = malloc(strlen(opbx_get_extension_name(e)) +
									strlen(opbx_get_context_name(c)) + 2);
								if (ret)
									sprintf(ret, "%s@%s", opbx_get_extension_name(e),
										opbx_get_context_name(c));
							}
							free(exten); free(context);

							opbx_unlock_contexts();
	
							return ret;
						}
					}
					e = opbx_walk_context_extensions(c, e);
				}
			}
			c = opbx_walk_contexts(c);
		}

		opbx_unlock_contexts();

		free(exten); free(context);

		return NULL;
	}

	/*
	 * Complete priority ...
	 */
	if (pos == 3) {
		char *delim, *exten, *context, *dupline, *duplinet, *ec;
		struct opbx_context *c;

		dupline = strdup(line);
		if (!dupline) {
#ifdef BROKEN_READLINE
			free(word);
#endif
			return NULL;
		}
		duplinet = dupline;

		strsep(&duplinet, " "); /* skip 'remove' */
		strsep(&duplinet, " "); /* skip 'extension */

		if (!(ec = strsep(&duplinet, " "))) {
			free(dupline);
#ifdef BROKEN_READLINE
			free(word);
#endif
			return NULL;
		}

		/* wrong exten@context format? */
		if (!(delim = strchr(ec, (int)'@')) ||
			(strchr(ec, (int)'@') != strrchr(ec, (int)'@'))) {
#ifdef BROKEN_READLINE
			free(word);
#endif
			free(dupline);
			return NULL;
		}

		/* check if there is exten and context too ... */
		*delim = '\0';
		if ((!strlen(ec)) || (!strlen(delim + 1))) {
#ifdef BROKEN_READLINE
			free(word);
#endif
			free(dupline);
			return NULL;
		}

		exten = strdup(ec);
		context = strdup(delim + 1);
		free(dupline);

		if (opbx_lock_contexts()) {
			opbx_log(LOG_ERROR, "Failed to lock context list\n");
#ifdef BROKEN_READLINE
			free(word);
#endif
			free(exten); free(context);
			return NULL;
		}

		/* walk contexts */
		c = opbx_walk_contexts(NULL); 
		while (c) {
			if (!strcmp(opbx_get_context_name(c), context)) {
				struct opbx_exten *e;

				/* walk extensions */
				free(context);
				e = opbx_walk_context_extensions(c, NULL); 
				while (e) {
					if (!strcmp(opbx_get_extension_name(e), exten)) {
						struct opbx_exten *priority;
						char buffer[10];
					
						free(exten);
						priority = opbx_walk_extension_priorities(e, NULL);
						/* serve priorities */
						do {
							snprintf(buffer, 10, "%u",
								opbx_get_extension_priority(priority));
							if (!strncmp(word, buffer, strlen(word))) {
								if (++which > state) {
#ifdef BROKEN_READLINE
									free(word);
#endif
									opbx_unlock_contexts();
									return strdup(buffer);
								}
							}
							priority = opbx_walk_extension_priorities(e,
								priority);
						} while (priority);

#ifdef BROKEN_READLINE
						free(word);
#endif
						opbx_unlock_contexts();
						return NULL;			
					}
					e = opbx_walk_context_extensions(c, e);
				}
#ifdef BROKEN_READLINE
				free(word);
#endif
				free(exten);
				opbx_unlock_contexts();
				return NULL;
			}
			c = opbx_walk_contexts(c);
		}

#ifdef BROKEN_READLINE
		free(word);
#endif
		free(exten); free(context);

		opbx_unlock_contexts();
		return NULL;
	}

#ifdef BROKEN_READLINE
	free(word);
#endif
	return NULL; 
}

/*
 * Include context ...
 */
static int handle_context_add_include(int fd, int argc, char *argv[])
{
	if (argc != 5) return RESULT_SHOWUSAGE;

	/* third arg must be 'in' ... */
	if (strcmp(argv[3], "in")) return RESULT_SHOWUSAGE;

	if (opbx_context_add_include(argv[4], argv[2], registrar)) {
		switch (errno) {
			case ENOMEM:
				opbx_cli(fd, "Out of memory for context addition\n"); break;

			case EBUSY:
				opbx_cli(fd, "Failed to lock context(s) list, please try again later\n"); break;

			case EEXIST:
				opbx_cli(fd, "Context '%s' already included in '%s' context\n",
					argv[1], argv[3]); break;

			case ENOENT:
			case EINVAL:
				opbx_cli(fd, "There is no existence of context '%s'\n",
					errno == ENOENT ? argv[4] : argv[2]); break;

			default:
				opbx_cli(fd, "Failed to include '%s' in '%s' context\n",
					argv[1], argv[3]); break;
		}
		return RESULT_FAILURE;
	}

	/* show some info ... */
	opbx_cli(fd, "Context '%s' included in '%s' context\n",
		argv[2], argv[3]);

	return RESULT_SUCCESS;
}

static char *complete_context_add_include(char *line, char *word, int pos,
    int state)
{
	struct opbx_context *c;
	int which = 0;

	/* server context for inclusion ... */
	if (pos == 1)
	{
		if (opbx_lock_contexts()) {
			opbx_log(LOG_ERROR, "Failed to lock context list\n");
			return NULL;
		}

		/* server all contexts */ 
		c = opbx_walk_contexts(NULL); 
		while (c) {
			if ((!strlen(word) || 
				 !strncmp(opbx_get_context_name(c), word, strlen(word))) &&
				++which > state)
			{
				char *context = strdup(opbx_get_context_name(c));
				opbx_unlock_contexts();
				return context;
			}
			c = opbx_walk_contexts(c);
		}

		opbx_unlock_contexts();
	}

	/* complete 'in' only if context exist ... */
	if (pos == 2)
	{
		char *context, *dupline, *duplinet;

		if (state != 0) return NULL;

		/* parse context from line ... */
		if (!(dupline = strdup(line))) {
			opbx_log(LOG_ERROR, "Out of free memory\n");
			if (state == 0) return strdup("in");
			return NULL;
		}

		duplinet = dupline;

		strsep(&duplinet, " ");
		context = strsep(&duplinet, " ");
		if (context) {
			struct opbx_context *c;
			int context_existence = 0;

			/* check for context existence ... */
			if (opbx_lock_contexts()) {
				opbx_log(LOG_ERROR, "Failed to lock context list\n");
				free(dupline);
				/* our fault, we can't check, so complete 'in' ... */
				return strdup("in");
			}

			c = opbx_walk_contexts(NULL);
			while (c && !context_existence) {
				if (!strcmp(context, opbx_get_context_name(c))) {
					context_existence = 1;
					continue;
				}
				c = opbx_walk_contexts(c);
			}

			/* if context exists, return 'into' ... */
			if (context_existence) {
				free(dupline);
				opbx_unlock_contexts();
				return strdup("into");
			}

			opbx_unlock_contexts();
		}	

		free(dupline);
		return NULL;
	}

	/* serve context into which we include another context */
	if (pos == 3)
	{
		char *context, *dupline, *duplinet, *in;
		int context_existence = 0;

		if (!(dupline = strdup(line))) {
			opbx_log(LOG_ERROR, "Out of free memory\n");
			return NULL;
		}

		duplinet = dupline;

		strsep(&duplinet, " "); /* skip 'include' */
		context = strsep(&duplinet, " ");
		in = strsep(&duplinet, " ");

		/* given some context and third word is in? */
		if (!strlen(context) || strcmp(in, "in")) {
			free(dupline);
			return NULL;
		}

		if (opbx_lock_contexts()) {
			opbx_log(LOG_ERROR, "Failed to lock context list\n");
			free(dupline);
			return NULL;
		}

		/* check for context existence ... */
		c = opbx_walk_contexts(NULL);
		while (c && !context_existence) {
			if (!strcmp(context, opbx_get_context_name(c))) {
				context_existence = 1;
				continue;
			}
			c = opbx_walk_contexts(c);
		}

		if (!context_existence) {
			free(dupline);
			opbx_unlock_contexts();
			return NULL;
		}

		/* go through all contexts ... */
		c = opbx_walk_contexts(NULL);
		while (c) {
			/* must be different contexts ... */
			if (strcmp(context, opbx_get_context_name(c))) {
				if (!opbx_lock_context(c)) {
					struct opbx_include *i;
					int included = 0;

					/* check for duplicity inclusion ... */
					i = opbx_walk_context_includes(c, NULL);
					while (i && !included) {
						if (!strcmp(opbx_get_include_name(i), context))
							included = 1;
						i = opbx_walk_context_includes(c, i);
					}
					opbx_unlock_context(c);

					/* not included yet, so show possibility ... */
					if (!included &&
						!strncmp(opbx_get_context_name(c), word, strlen(word))){
						
						if (++which > state) {
							char *res = strdup(opbx_get_context_name(c));
							free(dupline);
							opbx_unlock_contexts();
							return res;
						}
					}	
				}
			}
			c = opbx_walk_contexts(c);
		}

		opbx_unlock_contexts();
		free(dupline);
		return NULL;
	}

	return NULL;
}

/*
 * 'save dialplan' CLI command implementation functions ...
 */
static int handle_save_dialplan(int fd, int argc, char *argv[])
{
	char filename[256];
	struct opbx_context *c;
	struct opbx_config *cfg;
	struct opbx_variable *v;
	int context_header_written;
	int incomplete = 0; /* incomplete config write? */
	FILE *output;

	if (! (static_config && !write_protect_config)) {
		opbx_cli(fd,
			"I can't save dialplan now, see '%s' example file.\n",
			config);
		return RESULT_FAILURE;
	}

	if (argc != 2 && argc != 3) return RESULT_SHOWUSAGE;

	if (opbx_mutex_lock(&save_dialplan_lock)) {
		opbx_cli(fd,
			"Failed to lock dialplan saving (another proccess saving?)\n");
		return RESULT_FAILURE;
	}

	/* have config path? */
	if (argc == 3) {
		/* is there extension.conf too? */
		if (!strstr(argv[2], ".conf")) {
			/* no, only directory path, check for last '/' occurence */
			if (*(argv[2] + strlen(argv[2]) -1) == '/')
				snprintf(filename, sizeof(filename), "%s%s",
					argv[2], config);
			else
				/* without config extensions.conf, add it */
				snprintf(filename, sizeof(filename), "%s/%s",
					argv[2], config);
		} else
			/* there is an .conf */
			snprintf(filename, sizeof(filename), argv[2]);
	} else
		/* no config file, default one */
		snprintf(filename, sizeof(filename), "%s/%s",
			(char *)opbx_config_OPBX_CONFIG_DIR, config);

	cfg = opbx_config_load("extensions.conf");

	/* try to lock contexts list */
	if (opbx_lock_contexts()) {
		opbx_cli(fd, "Failed to lock contexts list\n");
		opbx_mutex_unlock(&save_dialplan_lock);
		opbx_config_destroy(cfg);
		return RESULT_FAILURE;
	}

	/* create new file ... */
	if (!(output = fopen(filename, "wt"))) {
		opbx_cli(fd, "Failed to create file '%s'\n",
			filename);
		opbx_unlock_contexts();
		opbx_mutex_unlock(&save_dialplan_lock);
		opbx_config_destroy(cfg);
		return RESULT_FAILURE;
	}

	/* fireout general info */
	fprintf(output, "[general]\nstatic=%s\nwriteprotect=%s\nautofallthrough=%s\nclearglobalvars=%s\npriorityjumping=%s\n\n",
		static_config ? "yes" : "no",
		write_protect_config ? "yes" : "no",
		autofallthrough_config ? "yes" : "no",
		clearglobalvars_config ? "yes" : "no",
		option_priority_jumping ? "yes" : "no");

	if ((v = opbx_variable_browse(cfg, "globals"))) {
		fprintf(output, "[globals]\n");
		while(v) {
			fprintf(output, "%s => %s\n", v->name, v->value);
			v = v->next;
		}
		fprintf(output, "\n");
	}

	opbx_config_destroy(cfg);
	
	/* walk all contexts */
	c = opbx_walk_contexts(NULL);
	while (c) {
		context_header_written = 0;
	
		/* try to lock context and fireout all info */	
		if (!opbx_lock_context(c)) {
			struct opbx_exten *e, *last_written_e = NULL;
			struct opbx_include *i;
			struct opbx_ignorepat *ip;
			struct opbx_sw *sw;

			/* registered by this module? */
			if (!strcmp(opbx_get_context_registrar(c), registrar)) {
				fprintf(output, "[%s]\n", opbx_get_context_name(c));
				context_header_written = 1;
			}

			/* walk extensions ... */
			e = opbx_walk_context_extensions(c, NULL);
			while (e) {
				struct opbx_exten *p;

				/* fireout priorities */
				p = opbx_walk_extension_priorities(e, NULL);
				while (p) {
					if (!strcmp(opbx_get_extension_registrar(p),
						registrar)) {
			
						/* make empty line between different extensions */	
						if (last_written_e != NULL &&
							strcmp(opbx_get_extension_name(last_written_e),
								opbx_get_extension_name(p)))
							fprintf(output, "\n");
						last_written_e = p;
				
						if (!context_header_written) {
							fprintf(output, "[%s]\n", opbx_get_context_name(c));
							context_header_written = 1;
						}

						if (opbx_get_extension_priority(p)!=PRIORITY_HINT) {
							char *tempdata;
							const char *el = opbx_get_extension_label(p);
							char label[128] = "";

							tempdata = opbx_get_extension_app_data(p);

							if (el && (snprintf(label, sizeof(label), "(%s)", el) != (strlen(el) + 2)))
								incomplete = 1; // error encountered or label is > 125 chars

							if (opbx_get_extension_matchcid(p)) {
								fprintf(output, "exten => %s/%s,%d%s,%s(%s)\n",
								    opbx_get_extension_name(p),
								    opbx_get_extension_cidmatch(p),
								    opbx_get_extension_priority(p),
								    label,
								    opbx_get_extension_app(p),
								    tempdata);
							} else {
								fprintf(output, "exten => %s,%d%s,%s(%s)\n",
								    opbx_get_extension_name(p),
								    opbx_get_extension_priority(p),
								    label,
								    opbx_get_extension_app(p),
								    tempdata);
							}
						} else {
							fprintf(output, "exten => %s,hint,%s\n",
							    opbx_get_extension_name(p),
							    opbx_get_extension_app(p));
						}
					}
					p = opbx_walk_extension_priorities(e, p);
				}

				e = opbx_walk_context_extensions(c, e);
			}

			/* written any extensions? ok, write space between exten & inc */
			if (last_written_e) fprintf(output, "\n");

			/* walk through includes */
			i = opbx_walk_context_includes(c, NULL);
			while (i) {
				if (!strcmp(opbx_get_include_registrar(i), registrar)) {
					if (!context_header_written) {
						fprintf(output, "[%s]\n", opbx_get_context_name(c));
						context_header_written = 1;
					}
					fprintf(output, "include => %s\n",
						opbx_get_include_name(i));
				}
				i = opbx_walk_context_includes(c, i);
			}

			if (opbx_walk_context_includes(c, NULL))
				fprintf(output, "\n");

			/* walk through switches */
			sw = opbx_walk_context_switches(c, NULL);
			while (sw) {
				if (!strcmp(opbx_get_switch_registrar(sw), registrar)) {
					if (!context_header_written) {
						fprintf(output, "[%s]\n", opbx_get_context_name(c));
						context_header_written = 1;
					}
					fprintf(output, "switch => %s/%s\n",
						opbx_get_switch_name(sw),
						opbx_get_switch_data(sw));
				}
				sw = opbx_walk_context_switches(c, sw);
			}

			if (opbx_walk_context_switches(c, NULL))
				fprintf(output, "\n");

			/* fireout ignorepats ... */
			ip = opbx_walk_context_ignorepats(c, NULL);
			while (ip) {
				if (!strcmp(opbx_get_ignorepat_registrar(ip), registrar)) {
					if (!context_header_written) {
						fprintf(output, "[%s]\n", opbx_get_context_name(c));
						context_header_written = 1;
					}

					fprintf(output, "ignorepat => %s\n",
						opbx_get_ignorepat_name(ip));
				}
				ip = opbx_walk_context_ignorepats(c, ip);
			}

			opbx_unlock_context(c);
		} else
			incomplete = 1;

		c = opbx_walk_contexts(c);
	}	

	opbx_unlock_contexts();
	opbx_mutex_unlock(&save_dialplan_lock);
	fclose(output);

	if (incomplete) {
		opbx_cli(fd, "Saved dialplan is incomplete\n");
		return RESULT_FAILURE;
	}

	opbx_cli(fd, "Dialplan successfully saved into '%s'\n",
		filename);
	return RESULT_SUCCESS;
}

/*
 * ADD EXTENSION command stuff
 */
static int handle_context_add_extension(int fd, int argc, char *argv[])
{
	char *whole_exten;
	char *exten, *prior;
	int iprior = -2;
	char *cidmatch, *app, *app_data;
	char *start, *end;

	/* check for arguments at first */
	if (argc != 5 && argc != 6) return RESULT_SHOWUSAGE;
	if (strcmp(argv[3], "into")) return RESULT_SHOWUSAGE;
	if (argc == 6) if (strcmp(argv[5], "replace")) return RESULT_SHOWUSAGE;

	whole_exten = argv[2];
	exten 		= strsep(&whole_exten,",");
	if (strchr(exten, '/')) {
		cidmatch = exten;
		strsep(&cidmatch,"/");
	} else {
		cidmatch = NULL;
	}
	prior       = strsep(&whole_exten,",");
	if (prior) {
    	if (!strcmp(prior, "hint")) {
			iprior = PRIORITY_HINT;
		} else {
			if (sscanf(prior, "%d", &iprior) != 1) {
				opbx_cli(fd, "'%s' is not a valid priority\n", prior);
				prior = NULL;
			}
		}
	}
	app = whole_exten;
	if (app && (start = strchr(app, '(')) && (end = strrchr(app, ')'))) {
		*start = *end = '\0';
		app_data = start + 1;
	} else {
		if (app) {
			app_data = strchr(app, ',');
			if (app_data) {
				*app_data = '\0';
				app_data++;
			}
		} else	
			app_data = NULL;
	}

	if (!exten || !prior || !app || (!app_data && iprior != PRIORITY_HINT)) return RESULT_SHOWUSAGE;

	if (!app_data)
		app_data="";
	if (opbx_add_extension(argv[4], argc == 6 ? 1 : 0, exten, iprior, NULL, cidmatch, app,
		(void *)strdup(app_data), free, registrar)) {
		switch (errno) {
			case ENOMEM:
				opbx_cli(fd, "Out of free memory\n"); break;

			case EBUSY:
				opbx_cli(fd, "Failed to lock context(s) list, please try again later\n"); break;

			case ENOENT:
				opbx_cli(fd, "No existence of '%s' context\n", argv[4]); break;

			case EEXIST:
				opbx_cli(fd, "Extension %s@%s with priority %s already exists\n",
					exten, argv[4], prior); break;

			default:
				opbx_cli(fd, "Failed to add '%s,%s,%s,%s' extension into '%s' context\n",
					exten, prior, app, app_data, argv[4]); break;
		}
		return RESULT_FAILURE;
	}

	if (argc == 6) 
		opbx_cli(fd, "Extension %s@%s (%s) replace by '%s,%s,%s,%s'\n",
			exten, argv[4], prior, exten, prior, app, app_data);
	else
		opbx_cli(fd, "Extension '%s,%s,%s,%s' added into '%s' context\n",
			exten, prior, app, app_data, argv[4]);

	return RESULT_SUCCESS;
}

/* add extension 6123,1,Dial,IAX/212.71.138.13/6123 into local */
static char *complete_context_add_extension(char *line, char *word,
	int pos, int state)
{
	int which = 0;

	/* complete 'into' word ... */
	if (pos == 3) {
		if (state == 0) return strdup("into");
		return NULL;
	}

	/* complete context */
	if (pos == 4) {
		struct opbx_context *c;

		/* try to lock contexts list ... */
		if (opbx_lock_contexts()) {
			opbx_log(LOG_WARNING, "Failed to lock contexts list\n");
			return NULL;
		}

		/* walk through all contexts */
		c = opbx_walk_contexts(NULL);
		while (c) {
			/* matching context? */
			if (!strncmp(opbx_get_context_name(c), word, strlen(word))) {
				if (++which > state) {
					char *res = strdup(opbx_get_context_name(c));
					opbx_unlock_contexts();
					return res;
				}
			}
			c = opbx_walk_contexts(c);
		}

		opbx_unlock_contexts();
		return NULL;
	}

	if (pos == 5) return state == 0 ? strdup("replace") : NULL;

	return NULL;
}

/*
 * IGNOREPAT CLI stuff
 */
static int handle_context_add_ignorepat(int fd, int argc, char *argv[])
{
	if (argc != 5) return RESULT_SHOWUSAGE;
	if (strcmp(argv[3], "into")) return RESULT_SHOWUSAGE;

	if (opbx_context_add_ignorepat(argv[4], argv[2], registrar)) {
		switch (errno) {
			case ENOMEM:
				opbx_cli(fd, "Out of free memory\n"); break;

			case ENOENT:
				opbx_cli(fd, "There is no existence of '%s' context\n", argv[4]);
				break;

			case EEXIST:
				opbx_cli(fd, "Ignore pattern '%s' already included in '%s' context\n",
					argv[2], argv[4]);
				break;

			case EBUSY:
				opbx_cli(fd, "Failed to lock context(s) list, please, try again later\n");
				break;

			default:
				opbx_cli(fd, "Failed to add ingore pattern '%s' into '%s' context\n",
					argv[2], argv[4]);
				break;
		}
		return RESULT_FAILURE;
	}

	opbx_cli(fd, "Ignore pattern '%s' added into '%s' context\n",
		argv[2], argv[4]);
	return RESULT_SUCCESS;
}

static char *complete_context_add_ignorepat(char *line, char *word,
	int pos, int state)
{
	if (pos == 3) return state == 0 ? strdup("into") : NULL;

	if (pos == 4) {
		struct opbx_context *c;
		int which = 0;
		char *dupline, *duplinet, *ignorepat = NULL;

		dupline = strdup(line);
		duplinet = dupline;

		if (duplinet) {
			strsep(&duplinet, " "); /* skip 'add' */
			strsep(&duplinet, " "); /* skip 'ignorepat' */
			ignorepat = strsep(&duplinet, " ");
		}

		if (opbx_lock_contexts()) {
			opbx_log(LOG_ERROR, "Failed to lock contexts list\n");
			return NULL;
		}

		c = opbx_walk_contexts(NULL);
		while (c) {
			if (!strncmp(opbx_get_context_name(c), word, strlen(word))) {
				int serve_context = 1;
				if (ignorepat) {
					if (!opbx_lock_context(c)) {
						struct opbx_ignorepat *ip;
						ip = opbx_walk_context_ignorepats(c, NULL);
						while (ip && serve_context) {
							if (!strcmp(opbx_get_ignorepat_name(ip), ignorepat))
								serve_context = 0;
							ip = opbx_walk_context_ignorepats(c, ip);
						}
						opbx_unlock_context(c);
					}
				}
				if (serve_context) {
					if (++which > state) {
						char *context = strdup(opbx_get_context_name(c));
						if (dupline) free(dupline);
						opbx_unlock_contexts();
						return context;
					}
				}
			}
			c = opbx_walk_contexts(c);
		}

		if (dupline) free(dupline);
		opbx_unlock_contexts();
		return NULL;
	}

	return NULL;
}

static int handle_context_remove_ignorepat(int fd, int argc, char *argv[])
{
	if (argc != 5) return RESULT_SHOWUSAGE;
	if (strcmp(argv[3], "from")) return RESULT_SHOWUSAGE;

	if (opbx_context_remove_ignorepat(argv[4], argv[2], registrar)) {
		switch (errno) {
			case EBUSY:
				opbx_cli(fd, "Failed to lock context(s) list, please try again later\n");
				break;

			case ENOENT:
				opbx_cli(fd, "There is no existence of '%s' context\n", argv[4]);
				break;

			case EINVAL:
				opbx_cli(fd, "There is no existence of '%s' ignore pattern in '%s' context\n",
					argv[2], argv[4]);
				break;

			default:
				opbx_cli(fd, "Failed to remove ignore pattern '%s' from '%s' context\n", argv[2], argv[4]);
				break;
		}
		return RESULT_FAILURE;
	}

	opbx_cli(fd, "Ignore pattern '%s' removed from '%s' context\n",
		argv[2], argv[4]);
	return RESULT_SUCCESS;
}

static int pbx_load_module(void);

static int handle_reload_extensions(int fd, int argc, char *argv[])
{
	if (argc!=2) return RESULT_SHOWUSAGE;
	pbx_load_module();
	return RESULT_SUCCESS;
}

static char *complete_context_remove_ignorepat(char *line, char *word,
	int pos, int state)
{
	struct opbx_context *c;
	int which = 0;

	if (pos == 2) {
		if (opbx_lock_contexts()) {
			opbx_log(LOG_WARNING, "Failed to lock contexts list\n");
			return NULL;
		}

		c = opbx_walk_contexts(NULL);
		while (c) {
			if (!opbx_lock_context(c)) {
				struct opbx_ignorepat *ip;
			
				ip = opbx_walk_context_ignorepats(c, NULL);
				while (ip) {
					if (!strncmp(opbx_get_ignorepat_name(ip), word, strlen(word))) {
						if (which + 1 > state) {
							struct opbx_context *cw;
							int already_served = 0;
							cw = opbx_walk_contexts(NULL);
							while (cw && cw != c && !already_served) {
								if (!opbx_lock_context(cw)) {
									struct opbx_ignorepat *ipw;
									ipw = opbx_walk_context_ignorepats(cw, NULL);
									while (ipw) {
										if (!strcmp(opbx_get_ignorepat_name(ipw),
											opbx_get_ignorepat_name(ip))) already_served = 1;
										ipw = opbx_walk_context_ignorepats(cw, ipw);
									}
									opbx_unlock_context(cw);
								}
								cw = opbx_walk_contexts(cw);
							}
							if (!already_served) {
								char *ret = strdup(opbx_get_ignorepat_name(ip));
								opbx_unlock_context(c);
								opbx_unlock_contexts();
								return ret;
							}
						} else
							which++;
					}
					ip = opbx_walk_context_ignorepats(c, ip);
				}

				opbx_unlock_context(c);
			}
			c = opbx_walk_contexts(c);
		}

		opbx_unlock_contexts();
		return NULL;
	}
 
	if (pos == 3) return state == 0 ? strdup("from") : NULL;

	if (pos == 4) {
		char *dupline, *duplinet, *ignorepat;

		dupline = strdup(line);
		if (!dupline) {
			opbx_log(LOG_WARNING, "Out of free memory\n");
			return NULL;
		}

		duplinet = dupline;
		strsep(&duplinet, " ");
		strsep(&duplinet, " ");
		ignorepat = strsep(&duplinet, " ");

		if (!ignorepat) {
			free(dupline);
			return NULL;
		}

		if (opbx_lock_contexts()) {
			opbx_log(LOG_WARNING, "Failed to lock contexts list\n");
			free(dupline);
			return NULL;
		}

		c = opbx_walk_contexts(NULL);
		while (c) {
			if (!opbx_lock_context(c)) {
				struct opbx_ignorepat *ip;
				ip = opbx_walk_context_ignorepats(c, NULL);
				while (ip) {
					if (!strcmp(opbx_get_ignorepat_name(ip), ignorepat)) {
						if (!strncmp(opbx_get_context_name(c), word, strlen(word))) {
							if (++which > state) {
								char *ret = strdup(opbx_get_context_name(c));
								free(dupline);
								opbx_unlock_context(c);
								opbx_unlock_contexts();
								return ret;
							}
						}
					}
					ip = opbx_walk_context_ignorepats(c, ip);
				}

				opbx_unlock_context(c);
			}
			c = opbx_walk_contexts(c);
		}

		free(dupline);
		opbx_unlock_contexts();
		return NULL;
	}

	return NULL;
}

/*
 * CLI entries for commands provided by this module
 */
static struct opbx_clicmd context_dont_include_cli = {
	.cmda = { "dont", "include", NULL },
	.handler = handle_context_dont_include,
	.generator = complete_context_dont_include,
	.summary = "Remove a specified include from context",
	.usage = context_dont_include_help,
};

static struct opbx_clicmd context_remove_extension_cli = {
	.cmda = { "remove", "extension", NULL },
	.handler = handle_context_remove_extension,
	.generator = complete_context_remove_extension,
	.summary = "Remove a specified extension",
	.usage = context_remove_extension_help,
};

static struct opbx_clicmd context_add_include_cli = {
	.cmda = { "include", "context", NULL },
	.handler = handle_context_add_include,
	.generator = complete_context_add_include,
	.summary = "Include context in other context",
	.usage = context_add_include_help,
};

static struct opbx_clicmd save_dialplan_cli = {
	.cmda = { "save", "dialplan", NULL },
	.handler = handle_save_dialplan,
	.summary = "Save dialplan",
	.usage = save_dialplan_help,
};

static struct opbx_clicmd context_add_extension_cli = {
	.cmda = { "add", "extension", NULL },
	.handler = handle_context_add_extension,
	.generator = complete_context_add_extension,
	.summary = "Add new extension into context",
	.usage = context_add_extension_help,
};

static struct opbx_clicmd context_add_ignorepat_cli = {
	.cmda = { "add", "ignorepat", NULL },
	.handler = handle_context_add_ignorepat,
	.generator = complete_context_add_ignorepat,
	.summary = "Add new ignore pattern",
	.usage = context_add_ignorepat_help,
};

static struct opbx_clicmd context_remove_ignorepat_cli = {
	.cmda = { "remove", "ignorepat", NULL },
	.handler = handle_context_remove_ignorepat,
	.generator = complete_context_remove_ignorepat,
	.summary = "Remove ignore pattern from context",
	.usage = context_remove_ignorepat_help,
};

static struct opbx_clicmd reload_extensions_cli = {
	.cmda = { "extensions", "reload", NULL},
	.handler = handle_reload_extensions,
	.summary = "Reload extensions and *only* extensions",
	.usage = reload_extensions_help,
};

/*
 * Standard module functions ...
 */
static int unload_module(void)
{
	opbx_cli_unregister(&context_add_extension_cli);
	if (static_config && !write_protect_config)
		opbx_cli_unregister(&save_dialplan_cli);
	opbx_cli_unregister(&context_add_include_cli);
	opbx_cli_unregister(&context_dont_include_cli);
	opbx_cli_unregister(&context_remove_extension_cli);
	opbx_cli_unregister(&context_remove_ignorepat_cli);
	opbx_cli_unregister(&context_add_ignorepat_cli);
	opbx_cli_unregister(&reload_extensions_cli);
	opbx_context_destroy(NULL, registrar);
	return 0;
}

static int pbx_load_module(void)
{
	struct opbx_config *cfg;
	struct opbx_variable *v;
	char *cxt, *ext, *pri, *appl, *data, *tc, *cidmatch;
	struct opbx_context *con;
	char *end;
	char *label;
	char realvalue[256];
	int lastpri = -2;

	cfg = opbx_config_load(config);
	if (cfg) {
		/* Use existing config to populate the PBX table */
		static_config = opbx_true(opbx_variable_retrieve(cfg, "general",
							       "static"));
		write_protect_config = opbx_true(opbx_variable_retrieve(cfg, "general",
								      "writeprotect"));
		autofallthrough_config = opbx_true(opbx_variable_retrieve(cfg, "general",
									"autofallthrough"));
		clearglobalvars_config = opbx_true(opbx_variable_retrieve(cfg, "general", 
									"clearglobalvars"));
		option_priority_jumping = !opbx_false(opbx_variable_retrieve(cfg, "general",
									   "priorityjumping"));

		v = opbx_variable_browse(cfg, "globals");
		while(v) {
			pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue));
			pbx_builtin_setvar_helper(NULL, v->name, realvalue);
			v = v->next;
		}
		cxt = opbx_category_browse(cfg, NULL);
		while(cxt) {
			/* All categories but "general" or "globals" are considered contexts */
			if (!strcasecmp(cxt, "general") || !strcasecmp(cxt, "globals")) {
				cxt = opbx_category_browse(cfg, cxt);
				continue;
			}
			if ((con=opbx_context_create(&local_contexts,cxt, registrar))) {
				v = opbx_variable_browse(cfg, cxt);
				while(v) {
					if (!strcasecmp(v->name, "exten")) {
						char *stringp=NULL;
						int ipri = -2;
						char realext[256]="";
						char *plus, *firstp, *firstc;
						tc = strdup(v->value);
						if(tc!=NULL){
							stringp=tc;
							ext = strsep(&stringp, ",");
							if (!ext)
								ext="";
							pbx_substitute_variables_helper(NULL, ext, realext, sizeof(realext));
							cidmatch = strchr(realext, '/');
							if (cidmatch) {
								*cidmatch = '\0';
								cidmatch++;
								opbx_shrink_phone_number(cidmatch);
							}
							pri = strsep(&stringp, ",");
							if (!pri)
								pri="";
							label = strchr(pri, '(');
							if (label) {
								*label = '\0';
								label++;
								end = strchr(label, ')');
								if (end)
									*end = '\0';
								else
									opbx_log(LOG_WARNING, "Label missing trailing ')' at line %d\n", v->lineno);
							}
							plus = strchr(pri, '+');
							if (plus) {
								*plus = '\0';
								plus++;
							}
							if (!strcmp(pri,"hint"))
								ipri=PRIORITY_HINT;
							else if (!strcmp(pri, "next") || !strcmp(pri, "n")) {
								if (lastpri > -2)
									ipri = lastpri + 1;
								else
									opbx_log(LOG_WARNING, "Can't use 'next' priority on the first entry!\n");
							} else if (!strcmp(pri, "same") || !strcmp(pri, "s")) {
								if (lastpri > -2)
									ipri = lastpri;
								else
									opbx_log(LOG_WARNING, "Can't use 'same' priority on the first entry!\n");
							} else  {
								if (sscanf(pri, "%d", &ipri) != 1) {
									if ((ipri = opbx_findlabel_extension2(NULL, con, realext, pri, cidmatch)) < 1) {
										opbx_log(LOG_WARNING, "Invalid priority/label '%s' at line %d\n", pri, v->lineno);
										ipri = 0;
									}
								}
							}
							appl = stringp;
							if (!appl)
								appl="";
							/* Find the first occurrence of either '(' or ',' */
							firstc = strchr(appl, ',');
							firstp = strchr(appl, '(');
							if (firstc && ((!firstp) || (firstc < firstp))) {
								/* comma found, no parenthesis */
								/* or both found, but comma found first */
								appl = strsep(&stringp, ",");
								data = stringp;
							} else if ((!firstc) && (!firstp)) {
								/* Neither found */
								data = "";
							} else {
								/* Final remaining case is parenthesis found first */
								appl = strsep(&stringp, "(");
								data = stringp;
								end = strrchr(data, ')');
								if ((end = strrchr(data, ')'))) {
									*end = '\0';
								} else {
									opbx_log(LOG_WARNING, "No closing parenthesis found? '%s(%s'\n", appl, data);
								}
							}

							if (!data)
								data="";
							while(*appl && (*appl < 33)) appl++;
							if (ipri) {
								if (plus)
									ipri += atoi(plus);
								lastpri = ipri;
								if(!option_dontwarn) {
									if (!strcmp(realext, "_."))
										opbx_log(LOG_WARNING, "The use of '_.' for an extension is strongly discouraged and can have unexpected behavior.  Please use '_X.' instead at line %d\n", v->lineno);
								}
								if (opbx_add_extension2(con, 0, realext, ipri, label, cidmatch, appl, strdup(data), FREE, registrar)) {
									opbx_log(LOG_WARNING, "Unable to register extension at line %d\n", v->lineno);
								}
							}
							free(tc);
						} else 
						    opbx_log(LOG_ERROR,"Error strdup returned NULL in %s\n",__PRETTY_FUNCTION__);
					} else if(!strcasecmp(v->name, "include")) {
						pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue));
						if (opbx_context_add_include2(con, realvalue, registrar))
							opbx_log(LOG_WARNING, "Unable to include context '%s' in context '%s'\n", v->value, cxt);
					} else if(!strcasecmp(v->name, "ignorepat")) {
						pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue));
						if (opbx_context_add_ignorepat2(con, realvalue, registrar))
							opbx_log(LOG_WARNING, "Unable to include ignorepat '%s' in context '%s'\n", v->value, cxt);
					} else if (!strcasecmp(v->name, "switch") || !strcasecmp(v->name, "lswitch") || !strcasecmp(v->name, "eswitch")) {
						char *stringp=NULL;
						if (!strcasecmp(v->name, "switch"))
							pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue));
						else
							strncpy(realvalue, v->value, sizeof(realvalue) - 1);
						tc = realvalue;
						stringp=tc;
						appl = strsep(&stringp, "/");
						data = strsep(&stringp, "");
						if (!data)
							data = "";
						if (opbx_context_add_switch2(con, appl, data, !strcasecmp(v->name, "eswitch"), registrar))
							opbx_log(LOG_WARNING, "Unable to include switch '%s' in context '%s'\n", v->value, cxt);
					}
					v = v->next;
				}
			}
			cxt = opbx_category_browse(cfg, cxt);
		}
		opbx_config_destroy(cfg);
	}
	opbx_merge_contexts_and_delete(&local_contexts,registrar);

	for (con = opbx_walk_contexts(NULL); con; con = opbx_walk_contexts(con))
		opbx_context_verify_includes(con);

	pbx_set_autofallthrough(autofallthrough_config);

	return 0;
}

static int load_module(void)
{
	if (pbx_load_module()) return -1;
 
	opbx_cli_register(&context_remove_extension_cli);
	opbx_cli_register(&context_dont_include_cli);
	opbx_cli_register(&context_add_include_cli);
	if (static_config && !write_protect_config)
		opbx_cli_register(&save_dialplan_cli);
	opbx_cli_register(&context_add_extension_cli);
	opbx_cli_register(&context_add_ignorepat_cli);
	opbx_cli_register(&context_remove_ignorepat_cli);
	opbx_cli_register(&reload_extensions_cli);

	return 0;
}

static int reload_module(void)
{
	opbx_context_destroy(NULL, registrar);
	if (clearglobalvars_config)
		pbx_builtin_clear_globals();
	pbx_load_module();
	return 0;
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, dtext)
