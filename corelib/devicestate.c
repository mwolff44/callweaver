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
 * \brief Device state management
 * 
 */
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/utils.h"
#include "callweaver/lock.h"
#include "callweaver/linkedlists.h"
#include "callweaver/logger.h"
#include "callweaver/devicestate.h"
#include "callweaver/pbx.h"
#include "callweaver/options.h"

static const char *devstatestring[] = {
	/*-1 CW_DEVICE_FAILURE */	"FAILURE",	/* Valid, but unknown state */
	/* 0 CW_DEVICE_UNKNOWN */	"Unknown",	/* Valid, but unknown state */
	/* 1 CW_DEVICE_NOT_INUSE */	"Not in use",	/* Not used */
	/* 2 CW_DEVICE IN USE */	"In use",	/* In use */
	/* 3 CW_DEVICE_BUSY */	"Busy",		/* Busy */
	/* 4 CW_DEVICE_INVALID */	"Invalid",	/* Invalid - not known to CallWeaver */
	/* 5 CW_DEVICE_UNAVAILABLE */	"Unavailable",	/* Unavailable (not registred) */
	/* 6 CW_DEVICE_RINGING */	"Ringing"	/* Ring, ring, ring */
};

/* cw_devstate_cb: A device state watcher (callback) */
struct devstate_cb {
	void *data;
	cw_devstate_cb_type callback;
	CW_LIST_ENTRY(devstate_cb) list;
};

static CW_LIST_HEAD_STATIC(devstate_cbs, devstate_cb);

struct state_change {
	CW_LIST_ENTRY(state_change) list;
	char device[1];
};

static CW_LIST_HEAD_STATIC(state_changes, state_change);

static pthread_t change_thread = CW_PTHREADT_NULL;
static cw_cond_t change_pending;

/*--- devstate2str: Find devicestate as text message for output */
const char *devstate2str(int devstate) 
{
	return devstatestring[devstate];
}

/*--- cw_parse_device_state: Find out if device is active in a call or not */
static cw_devicestate_t cw_parse_device_state(const char *device)
{
	struct cw_channel *chan;
	cw_devicestate_t res;

	res = CW_DEVICE_UNKNOWN;
	if ((chan = cw_get_device_by_name_locked(device))) {
		res = (chan->_state == CW_STATE_RINGING ? CW_DEVICE_RINGING : CW_DEVICE_INUSE);
		cw_channel_unlock(chan);
		cw_object_put(chan);
	}
	return res;
}

/*--- cw_device_state: Check device state through channel specific function or generic function */
cw_devicestate_t cw_device_state(const char *device)
{
	char *buf;
	char *tech;
	char *number;
	const struct cw_channel_tech *chan_tech;
	cw_devicestate_t res = CW_DEVICE_UNKNOWN;

	buf = cw_strdupa(device);
	tech = strsep(&buf, "/");
	number = buf;

	chan_tech = cw_get_channel_tech(tech);
	if (!chan_tech)
		return CW_DEVICE_INVALID;

	if (!chan_tech->devicestate) 	/* Does the channel driver support device state notification? */
		return cw_parse_device_state(device);	/* No, try the generic function */
	else {
		res = chan_tech->devicestate(number);	/* Ask the channel driver for device state */
		if (res == CW_DEVICE_UNKNOWN) {
			res = cw_parse_device_state(device);
			/* at this point we know the device exists, but the channel driver
			   could not give us a state; if there is no channel state available,
			   it must be 'not in use'
			*/
			if (res == CW_DEVICE_UNKNOWN)
				res = CW_DEVICE_NOT_INUSE;
			return res;
		} else
			return res;
	}
        
}

/*--- cw_devstate_add: Add device state watcher */
int cw_devstate_add(cw_devstate_cb_type callback, void *data)
{
	struct devstate_cb *devcb;

	if (!callback)
		return -1;

	devcb = calloc(1, sizeof(*devcb));
	if (!devcb)
		return -1;

	devcb->data = data;
	devcb->callback = callback;

	CW_LIST_LOCK(&devstate_cbs);
	CW_LIST_INSERT_HEAD(&devstate_cbs, devcb, list);
	CW_LIST_UNLOCK(&devstate_cbs);

	return 0;
}

/*--- cw_devstate_del: Remove device state watcher */
void cw_devstate_del(cw_devstate_cb_type callback, void *data)
{
	struct devstate_cb *devcb;

	CW_LIST_LOCK(&devstate_cbs);
	CW_LIST_TRAVERSE_SAFE_BEGIN(&devstate_cbs, devcb, list) {
		if ((devcb->callback == callback) && (devcb->data == data)) {
			CW_LIST_REMOVE_CURRENT(&devstate_cbs, list);
			free(devcb);
			break;
		}
	}
	CW_LIST_TRAVERSE_SAFE_END;
	CW_LIST_UNLOCK(&devstate_cbs);
}

/*--- do_state_change: Notify callback watchers of change, and notify PBX core for hint updates */
static inline void do_state_change(const char *device)
{
	int state;
	struct devstate_cb *devcb;

	state = cw_device_state(device);
	if (option_debug > 2)
		cw_log(CW_LOG_DEBUG, "Changing state for %s - state %d (%s)\n", device, state, devstate2str(state));

	CW_LIST_LOCK(&devstate_cbs);
	CW_LIST_TRAVERSE(&devstate_cbs, devcb, list)
		devcb->callback(device, state, devcb->data);
	CW_LIST_UNLOCK(&devstate_cbs);

	cw_hint_state_changed(device);
}

static int __cw_device_state_changed_literal(char *buf)
{
	char *device, *tmp;
	struct state_change *change = NULL;

	device = buf;
	tmp = strrchr(device, '-');
	if (tmp)
		*tmp = '\0';
	if (!pthread_equal(change_thread, CW_PTHREADT_NULL))
		change = calloc(1, sizeof(*change) + strlen(device));

	if (!change) {
		/* we could not allocate a change struct, or */
		/* there is no background thread, so process the change now */
		do_state_change(device);
	} else {
		/* queue the change */
		strcpy(change->device, device);
		CW_LIST_LOCK(&state_changes);
		CW_LIST_INSERT_TAIL(&state_changes, change, list);
		if (CW_LIST_FIRST(&state_changes) == change)
			/* the list was empty, signal the thread */
			cw_cond_signal(&change_pending);
		CW_LIST_UNLOCK(&state_changes);
	}

	return 1;
}

int cw_device_state_changed_literal(const char *dev)
{
	char *buf;
	buf = cw_strdupa(dev);
	return __cw_device_state_changed_literal(buf);
}

/*--- cw_device_state_changed: Accept change notification, add it to change queue */
int cw_device_state_changed(const char *fmt, ...) 
{
	char buf[CW_MAX_EXTENSION];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return __cw_device_state_changed_literal(buf);
}

/*--- do_devstate_changes: Go through the dev state change queue and update changes in the dev state thread */
static __attribute__((__noreturn__)) void *do_devstate_changes(void *data)
{
	struct state_change *sc = NULL;

	CW_UNUSED(data);

	CW_LIST_LOCK(&state_changes);
	for (;;) {
		/* the list lock will _always_ be held at this point in the loop */
		sc = CW_LIST_REMOVE_HEAD(&state_changes, list);
		if (sc) {
			/* we got an entry, so unlock the list while we process it */
			CW_LIST_UNLOCK(&state_changes);
			do_state_change(sc->device);
			free(sc);
			CW_LIST_LOCK(&state_changes);
		} else {
			/* there was no entry, so atomically unlock the list and wait for
			   the condition to be signalled (returns with the lock held) */
			cw_cond_wait(&change_pending, &state_changes.lock);
		}
	}
}

/*--- cw_device_state_engine_init: Initialize the device state engine in separate thread */
int cw_device_state_engine_init(void)
{
	cw_cond_init(&change_pending, NULL);
	if (cw_pthread_create(&change_thread, &global_attr_detached, do_devstate_changes, NULL) < 0) {
		cw_log(CW_LOG_ERROR, "Unable to start device state change thread.\n");
		return -1;
	}

	return 0;
}
