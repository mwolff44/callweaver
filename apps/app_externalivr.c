/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
 *
 * Portions taken from the file-based music-on-hold work
 * created by Anthony Minessale II in res_musiconhold.c
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
 * External IVR application interface
 * 
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision$")

#include "openpbx/lock.h"
#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/module.h"
#include "openpbx/linkedlists.h"

static const char *tdesc = "External IVR Interface Application";

static const char *app = "ExternalIVR";

static const char *synopsis = "Interfaces with an external IVR application";

static const char *descrip = 
"  ExternalIVR(command[|arg[|arg...]]): Forks an process to run the supplied command,\n"
"and starts a generator on the channel. The generator's play list is\n"
"controlled by the external application, which can add and clear entries\n"
"via simple commands issued over its stdout. The external application\n"
"will receive all DTMF events received on the channel, and notification\n"
"if the channel is hung up. The application will not be forcibly terminated\n"
"when the channel is hung up.\n"
"See doc/README.externalivr for a protocol specification.\n";

#define opbx_chan_log(level, channel, format, ...) opbx_log(level, "%s: " format, channel->name, ## __VA_ARGS__)

struct playlist_entry {
	OPBX_LIST_ENTRY(playlist_entry) list;
	char filename[1];
};

struct localuser {
	struct opbx_channel *chan;
	struct localuser *next;
	OPBX_LIST_HEAD(playlist, playlist_entry) playlist;
	OPBX_LIST_HEAD(finishlist, playlist_entry) finishlist;
	int abort_current_sound;
	int playing_silence;
	int option_autoclear;
};

LOCAL_USER_DECL;

struct gen_state {
	struct localuser *u;
	struct opbx_filestream *stream;
	struct playlist_entry *current;
	int sample_queue;
};

static void send_child_event(FILE *handle, const char event, const char *data,
			     const struct opbx_channel *chan)
{
	char tmp[256];

	if (!data) {
		snprintf(tmp, sizeof(tmp), "%c,%10ld", event, time(NULL));
	} else {
		snprintf(tmp, sizeof(tmp), "%c,%10ld,%s", event, time(NULL), data);
	}

	fprintf(handle, "%s\n", tmp);
	opbx_chan_log(LOG_DEBUG, chan, "sent '%s'\n", tmp);
}

static void *gen_alloc(struct opbx_channel *chan, void *params)
{
	struct localuser *u = params;
	struct gen_state *state;

	state = calloc(1, sizeof(*state));

	if (!state)
		return NULL;

	state->u = u;

	return state;
}

static void gen_closestream(struct gen_state *state)
{
	if (!state->stream)
		return;

	opbx_closestream(state->stream);
	state->u->chan->stream = NULL;
	state->stream = NULL;
}

static void gen_release(struct opbx_channel *chan, void *data)
{
	struct gen_state *state = data;

	gen_closestream(state);
	free(data);
}

/* caller has the playlist locked */
static int gen_nextfile(struct gen_state *state)
{
	struct localuser *u = state->u;
	char *file_to_stream;
	
	u->abort_current_sound = 0;
	u->playing_silence = 0;
	gen_closestream(state);

	while (!state->stream) {
		state->current = OPBX_LIST_REMOVE_HEAD(&u->playlist, list);
		if (state->current) {
			file_to_stream = state->current->filename;
		} else {
			file_to_stream = "silence-10";
			u->playing_silence = 1;
		}

		if (!(state->stream = opbx_openstream_full(u->chan, file_to_stream, u->chan->language, 1))) {
			opbx_chan_log(LOG_WARNING, u->chan, "File '%s' could not be opened: %s\n", file_to_stream, strerror(errno));
			if (!u->playing_silence) {
				continue;
			} else { 
				break;
			}
		}
	}

	return (!state->stream);
}

static struct opbx_frame *gen_readframe(struct gen_state *state)
{
	struct opbx_frame *f = NULL;
	struct localuser *u = state->u;
	
	if (u->abort_current_sound ||
	    (u->playing_silence && OPBX_LIST_FIRST(&u->playlist))) {
		gen_closestream(state);
		OPBX_LIST_LOCK(&u->playlist);
		gen_nextfile(state);
		OPBX_LIST_UNLOCK(&u->playlist);
	}

	if (!(state->stream && (f = opbx_readframe(state->stream)))) {
		if (state->current) {
			OPBX_LIST_LOCK(&u->finishlist);
			OPBX_LIST_INSERT_TAIL(&u->finishlist, state->current, list);
			OPBX_LIST_UNLOCK(&u->finishlist);
			state->current = NULL;
		}
		if (!gen_nextfile(state))
			f = opbx_readframe(state->stream);
	}

	return f;
}

static int gen_generate(struct opbx_channel *chan, void *data, int len, int samples)
{
	struct gen_state *state = data;
	struct opbx_frame *f = NULL;
	int res = 0;

	state->sample_queue += samples;

	while (state->sample_queue > 0) {
		if (!(f = gen_readframe(state)))
			return -1;

		res = opbx_write(chan, f);
		opbx_frfree(f);
		if (res < 0) {
			opbx_chan_log(LOG_WARNING, chan, "Failed to write frame: %s\n", strerror(errno));
			return -1;
		}
		state->sample_queue -= f->samples;
	}

	return res;
}

static struct opbx_generator gen =
{
	alloc: gen_alloc,
	release: gen_release,
	generate: gen_generate,
};

static struct playlist_entry *make_entry(const char *filename)
{
	struct playlist_entry *entry;

	entry = calloc(1, sizeof(*entry) + strlen(filename) + 10);

	if (!entry)
		return NULL;

	strcpy(entry->filename, filename);

	return entry;
}

static int app_exec(struct opbx_channel *chan, void *data)
{
	struct localuser *u = NULL;
	struct playlist_entry *entry;
	const char *args = data;
	int child_stdin[2] = { 0,0 };
	int child_stdout[2] = { 0,0 };
	int child_stderr[2] = { 0,0 };
	int res = -1;
	int gen_active = 0;
	int pid;
	char *command;
	char *argv[32];
	int argc = 1;
	char *buf;
	FILE *child_commands = NULL;
	FILE *child_errors = NULL;
	FILE *child_events = NULL;

	if (!args || opbx_strlen_zero(args)) {
		opbx_log(LOG_WARNING, "ExternalIVR requires a command to execute\n");
		goto exit;
	}

	buf = opbx_strdupa(data);
	command = strsep(&buf, "|");
	memset(argv, 0, sizeof(argv) / sizeof(argv[0]));
	argv[0] = command;
	while ((argc < 31) && (argv[argc++] = strsep(&buf, "|")));
	argv[argc] = NULL;

	LOCAL_USER_ADD(u);

	if (pipe(child_stdin)) {
		opbx_chan_log(LOG_WARNING, chan, "Could not create pipe for child input: %s\n", strerror(errno));
		goto exit;
	}

	if (pipe(child_stdout)) {
		opbx_chan_log(LOG_WARNING, chan, "Could not create pipe for child output: %s\n", strerror(errno));
		goto exit;
	}

	if (pipe(child_stderr)) {
		opbx_chan_log(LOG_WARNING, chan, "Could not create pipe for child errors: %s\n", strerror(errno));
		goto exit;
	}

	u->abort_current_sound = 0;
	OPBX_LIST_HEAD_INIT(&u->playlist);
	OPBX_LIST_HEAD_INIT(&u->finishlist);

	if (chan->_state != OPBX_STATE_UP) {
		opbx_answer(chan);
	}

	if (opbx_activate_generator(chan, &gen, u) < 0) {
		opbx_chan_log(LOG_WARNING, chan, "Failed to activate generator\n");
		goto exit;
	} else
		gen_active = 1;

	pid = fork();
	if (pid < 0) {
		opbx_log(LOG_WARNING, "Failed to fork(): %s\n", strerror(errno));
		goto exit;
	}

	if (!pid) {
		/* child process */
		int i;

		dup2(child_stdin[0], STDIN_FILENO);
		dup2(child_stdout[1], STDOUT_FILENO);
		dup2(child_stderr[1], STDERR_FILENO);
		for (i = STDERR_FILENO + 1; i < 1024; i++)
			close(i);
		execv(command, argv);
		fprintf(stderr, "Failed to execute '%s': %s\n", command, strerror(errno));
		exit(1);
	} else {
		/* parent process */
		int child_events_fd = child_stdin[1];
		int child_commands_fd = child_stdout[0];
		int child_errors_fd = child_stderr[0];
		struct opbx_frame *f;
		int ms;
		int exception;
		int ready_fd;
		int waitfds[2] = { child_errors_fd, child_commands_fd };
		struct opbx_channel *rchan;

		close(child_stdin[0]);
		child_stdin[0] = 0;
		close(child_stdout[1]);
		child_stdout[1] = 0;
		close(child_stderr[1]);
		child_stderr[1] = 0;

		if (!(child_events = fdopen(child_events_fd, "w"))) {
			opbx_chan_log(LOG_WARNING, chan, "Could not open stream for child events\n");
			goto exit;
		}

		setvbuf(child_events, NULL, _IONBF, 0);

		if (!(child_commands = fdopen(child_commands_fd, "r"))) {
			opbx_chan_log(LOG_WARNING, chan, "Could not open stream for child commands\n");
			goto exit;
		}

		if (!(child_errors = fdopen(child_errors_fd, "r"))) {
			opbx_chan_log(LOG_WARNING, chan, "Could not open stream for child errors\n");
			goto exit;
		}

		res = 0;

		while (1) {
			if (opbx_test_flag(chan, OPBX_FLAG_ZOMBIE)) {
				opbx_chan_log(LOG_NOTICE, chan, "Is a zombie\n");
				res = -1;
				break;
			}

			if (opbx_check_hangup(chan)) {
				opbx_chan_log(LOG_NOTICE, chan, "Got check_hangup\n");
				send_child_event(child_events, 'H', NULL, chan);
				res = -1;
				break;
			}

			ready_fd = 0;
			ms = 100;
			errno = 0;
			exception = 0;

			rchan = opbx_waitfor_nandfds(&chan, 1, waitfds, 2, &exception, &ready_fd, &ms);

			if (!OPBX_LIST_EMPTY(&u->finishlist)) {
				OPBX_LIST_LOCK(&u->finishlist);
				while ((entry = OPBX_LIST_REMOVE_HEAD(&u->finishlist, list))) {
					send_child_event(child_events, 'F', entry->filename, chan);
					free(entry);
				}
				OPBX_LIST_UNLOCK(&u->finishlist);
			}

			if (rchan) {
				/* the channel has something */
				f = opbx_read(chan);
				if (!f) {
					opbx_chan_log(LOG_NOTICE, chan, "Returned no frame\n");
					send_child_event(child_events, 'H', NULL, chan);
					res = -1;
					break;
				}

				if (f->frametype == OPBX_FRAME_DTMF) {
					send_child_event(child_events, f->subclass, NULL, chan);
					if (u->option_autoclear) {
						if (!u->abort_current_sound && !u->playing_silence)
							send_child_event(child_events, 'T', NULL, chan);
						OPBX_LIST_LOCK(&u->playlist);
						while ((entry = OPBX_LIST_REMOVE_HEAD(&u->playlist, list))) {
							send_child_event(child_events, 'D', entry->filename, chan);
							free(entry);
						}
						if (!u->playing_silence)
							u->abort_current_sound = 1;
						OPBX_LIST_UNLOCK(&u->playlist);
					}
				} else if ((f->frametype == OPBX_FRAME_CONTROL) && (f->subclass == OPBX_CONTROL_HANGUP)) {
					opbx_chan_log(LOG_NOTICE, chan, "Got OPBX_CONTROL_HANGUP\n");
					send_child_event(child_events, 'H', NULL, chan);
					opbx_frfree(f);
					res = -1;
					break;
				}
				opbx_frfree(f);
			} else if (ready_fd == child_commands_fd) {
				char input[1024];

				if (exception || feof(child_commands)) {
					opbx_chan_log(LOG_WARNING, chan, "Child process went away\n");
					res = -1;
					break;
				}

				if (!fgets(input, sizeof(input), child_commands))
					continue;

				command = opbx_strip(input);

				opbx_chan_log(LOG_DEBUG, chan, "got command '%s'\n", input);

				if (strlen(input) < 4)
					continue;

				if (input[0] == 'S') {
					if (opbx_fileexists(&input[2], NULL, NULL) == -1) {
						opbx_chan_log(LOG_WARNING, chan, "Unknown file requested '%s'\n", &input[2]);
						send_child_event(child_events, 'Z', NULL, chan);
						strcpy(&input[2], "exception");
					}
					if (!u->abort_current_sound && !u->playing_silence)
						send_child_event(child_events, 'T', NULL, chan);
					OPBX_LIST_LOCK(&u->playlist);
					while ((entry = OPBX_LIST_REMOVE_HEAD(&u->playlist, list))) {
						send_child_event(child_events, 'D', entry->filename, chan);
						free(entry);
					}
					if (!u->playing_silence)
						u->abort_current_sound = 1;
					entry = make_entry(&input[2]);
					if (entry)
						OPBX_LIST_INSERT_TAIL(&u->playlist, entry, list);
					OPBX_LIST_UNLOCK(&u->playlist);
				} else if (input[0] == 'A') {
					if (opbx_fileexists(&input[2], NULL, NULL) == -1) {
						opbx_chan_log(LOG_WARNING, chan, "Unknown file requested '%s'\n", &input[2]);
						send_child_event(child_events, 'Z', NULL, chan);
						strcpy(&input[2], "exception");
					}
					entry = make_entry(&input[2]);
					if (entry) {
						OPBX_LIST_LOCK(&u->playlist);
						OPBX_LIST_INSERT_TAIL(&u->playlist, entry, list);
						OPBX_LIST_UNLOCK(&u->playlist);
					}
				} else if (input[0] == 'H') {
					opbx_chan_log(LOG_NOTICE, chan, "Hanging up: %s\n", &input[2]);
					send_child_event(child_events, 'H', NULL, chan);
					break;
				} else if (input[0] == 'O') {
					if (!strcasecmp(&input[2], "autoclear"))
						u->option_autoclear = 1;
					else if (!strcasecmp(&input[2], "noautoclear"))
						u->option_autoclear = 0;
					else
						opbx_chan_log(LOG_WARNING, chan, "Unknown option requested '%s'\n", &input[2]);
				}
			} else if (ready_fd == child_errors_fd) {
				char input[1024];

				if (exception || feof(child_errors)) {
					opbx_chan_log(LOG_WARNING, chan, "Child process went away\n");
					res = -1;
					break;
				}

				if (fgets(input, sizeof(input), child_errors)) {
					command = opbx_strip(input);
					opbx_chan_log(LOG_NOTICE, chan, "stderr: %s\n", command);
				}
			} else if ((ready_fd < 0) && ms) { 
				if (errno == 0 || errno == EINTR)
					continue;

				opbx_chan_log(LOG_WARNING, chan, "Wait failed (%s)\n", strerror(errno));
				break;
			}
		}
	}

 exit:
	if (gen_active)
		opbx_deactivate_generator(chan);

	if (child_events)
		fclose(child_events);

	if (child_commands)
		fclose(child_commands);

	if (child_errors)
		fclose(child_errors);

	if (child_stdin[0])
		close(child_stdin[0]);

	if (child_stdin[1])
		close(child_stdin[1]);

	if (child_stdout[0])
		close(child_stdout[0]);

	if (child_stdout[1])
		close(child_stdout[1]);

	if (child_stderr[0])
		close(child_stderr[0]);

	if (child_stderr[1])
		close(child_stderr[1]);

	if (u) {
		while ((entry = OPBX_LIST_REMOVE_HEAD(&u->playlist, list)))
			free(entry);

		LOCAL_USER_REMOVE(u);
	}

	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;

	return opbx_unregister_application(app);
}

int load_module(void)
{
	return opbx_register_application(app, app_exec, synopsis, descrip);
}

char *description(void)
{
	return (char *) tdesc;
}

int usecount(void)
{
	int res;

	STANDARD_USECOUNT(res);

	return res;
}

char *key()
{
	return OPENPBX_GPL_KEY;
}
