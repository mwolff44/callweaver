/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (c) 2004 - 2005, inAccess Networks
 *
 * Michael Manousos <manousos@inaccessnetworks.com>
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
 * Headerless G.726 (16/24/32/40kbps) data format for OpenPBX.
 * 
 */
 
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION("$HeadURL$", "$Revision$")

#include "openpbx/lock.h"
#include "openpbx/options.h"
#include "openpbx/channel.h"
#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/sched.h"
#include "openpbx/module.h"
#include "openpbx/confdefs.h"

#define	RATE_32		1

/* We can only read/write chunks of FRAME_TIME ms G.726 data */
#define	FRAME_TIME	10	/* 10 ms size */

/* Frame sizes in bytes */
static int frame_size[4] = { 
		FRAME_TIME * 4,
};

struct opbx_filestream {
	/* Do not place anything before "reserved" */
	void *reserved[OPBX_RESERVED_POINTERS];
	/* This is what a filestream means to us */
	int fd; 							/* Open file descriptor */
	int rate;							/* RATE_* defines */
	struct opbx_frame fr;				/* Frame information */
	char waste[OPBX_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	char empty;							/* Empty character */
	unsigned char g726[FRAME_TIME * 5];	/* G.726 encoded voice */
};

OPBX_MUTEX_DEFINE_STATIC(g726_lock);
static int glistcnt = 0;

static char *desc = "Raw G.726 (32kbps) data";
static char *name32 = "g726-32";
static char *exts32 = "g726-32";


/*
 * Rate dependant format functions (open, rewrite)
 */
static struct opbx_filestream *g726_32_open(int fd)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct opbx_filestream *tmp;
	if ((tmp = malloc(sizeof(struct opbx_filestream)))) {
		memset(tmp, 0, sizeof(struct opbx_filestream));
		if (opbx_mutex_lock(&g726_lock)) {
			opbx_log(LOG_WARNING, "Unable to lock g726 list.\n");
			free(tmp);
			return NULL;
		}
		tmp->fd = fd;
		tmp->rate = RATE_32;
		tmp->fr.data = tmp->g726;
		tmp->fr.frametype = OPBX_FRAME_VOICE;
		tmp->fr.subclass = OPBX_FORMAT_G726;
		/* datalen will vary for each frame */
		tmp->fr.src = name32;
		tmp->fr.mallocd = 0;
		glistcnt++;
		if (option_debug)
			opbx_log(LOG_DEBUG, "Created filestream G.726-%dk.\n", 
									40 - tmp->rate * 8);
		opbx_mutex_unlock(&g726_lock);
		opbx_update_use_count();
	}
	return tmp;
}

static struct opbx_filestream *g726_32_rewrite(int fd, const char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct opbx_filestream *tmp;
	if ((tmp = malloc(sizeof(struct opbx_filestream)))) {
		memset(tmp, 0, sizeof(struct opbx_filestream));
		if (opbx_mutex_lock(&g726_lock)) {
			opbx_log(LOG_WARNING, "Unable to lock g726 list.\n");
			free(tmp);
			return NULL;
		}
		tmp->fd = fd;
		tmp->rate = RATE_32;
		glistcnt++;
		if (option_debug)
			opbx_log(LOG_DEBUG, "Created filestream G.726-%dk.\n", 
									40 - tmp->rate * 8);
		opbx_mutex_unlock(&g726_lock);
		opbx_update_use_count();
	} else
		opbx_log(LOG_WARNING, "Out of memory\n");
	return tmp;
}

/*
 * Rate independent format functions (close, read, write)
 */
static void g726_close(struct opbx_filestream *s)
{
	if (opbx_mutex_lock(&g726_lock)) {
		opbx_log(LOG_WARNING, "Unable to lock g726 list.\n");
		return;
	}
	glistcnt--;
	if (option_debug)
		opbx_log(LOG_DEBUG, "Closed filestream G.726-%dk.\n", 40 - s->rate * 8);
	opbx_mutex_unlock(&g726_lock);
	opbx_update_use_count();
	close(s->fd);
	free(s);
	s = NULL;
}

static struct opbx_frame *g726_read(struct opbx_filestream *s, int *whennext)
{
	int res;
	/* Send a frame from the file to the appropriate channel */
	s->fr.frametype = OPBX_FRAME_VOICE;
	s->fr.subclass = OPBX_FORMAT_G726;
	s->fr.offset = OPBX_FRIENDLY_OFFSET;
	s->fr.samples = 8 * FRAME_TIME;
	s->fr.datalen = frame_size[s->rate];
	s->fr.mallocd = 0;
	s->fr.data = s->g726;
	if ((res = read(s->fd, s->g726, s->fr.datalen)) != s->fr.datalen) {
		if (res)
			opbx_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	*whennext = s->fr.samples;
	return &s->fr;
}

static int g726_write(struct opbx_filestream *fs, struct opbx_frame *f)
{
	int res;
	if (f->frametype != OPBX_FRAME_VOICE) {
		opbx_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != OPBX_FORMAT_G726) {
		opbx_log(LOG_WARNING, "Asked to write non-G726 frame (%d)!\n", 
						f->subclass);
		return -1;
	}
	if (f->datalen % frame_size[fs->rate]) {
		opbx_log(LOG_WARNING, "Invalid data length %d, should be multiple of %d\n", 
						f->datalen, frame_size[fs->rate]);
		return -1;
	}
	if ((res = write(fs->fd, f->data, f->datalen)) != f->datalen) {
			opbx_log(LOG_WARNING, "Bad write (%d/%d): %s\n", 
							res, frame_size[fs->rate], strerror(errno));
			return -1;
	}
	return 0;
}

static char *g726_getcomment(struct opbx_filestream *s)
{
	return NULL;
}

static int g726_seek(struct opbx_filestream *fs, long sample_offset, int whence)
{
	return -1;
}

static int g726_trunc(struct opbx_filestream *fs)
{
	return -1;
}

static long g726_tell(struct opbx_filestream *fs)
{
	return -1;
}

/*
 * Module interface (load_module, unload_module, usecount, description, key)
 */
int load_module()
{
	int res;

	res = opbx_format_register(name32, exts32, OPBX_FORMAT_G726,
								g726_32_open,
								g726_32_rewrite,
								g726_write,
								g726_seek,
								g726_trunc,
								g726_tell,
								g726_read,
								g726_close,
								g726_getcomment);
	if (res) {
		opbx_log(LOG_WARNING, "Failed to register format %s.\n", name32);
		return(-1);
	}
	return(0);
}

int unload_module()
{
	int res;

	res = opbx_format_unregister(name32);
	if (res) {
		opbx_log(LOG_WARNING, "Failed to unregister format %s.\n", name32);
		return(-1);
	}
	return(0);
}	

int usecount()
{
	return glistcnt;
}

char *description()
{
	return desc;
}
