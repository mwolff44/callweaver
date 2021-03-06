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
 * \brief Channel Management
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES
#include <spandsp.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/object.h"
#include "callweaver/registry.h"
#include "callweaver/pbx.h"
#include "callweaver/frame.h"
#include "callweaver/options.h"
#include "callweaver/channel.h"
#include "callweaver/musiconhold.h"
#include "callweaver/logger.h"
#include "callweaver/say.h"
#include "callweaver/file.h"
#include "callweaver/cli.h"
#include "callweaver/translate.h"
#include "callweaver/manager.h"
#include "callweaver/chanvars.h"
#include "callweaver/linkedlists.h"
#include "callweaver/indications.h"
#include "callweaver/monitor.h"
#include "callweaver/causes.h"
#include "callweaver/phone_no_utils.h"
#include "callweaver/utils.h"
#include "callweaver/lock.h"
#include "callweaver/app.h"
#include "callweaver/transcap.h"
#include "callweaver/devicestate.h"

/* uncomment if you have problems with 'monitoring' synchronized files */
#if 0
#define MONITOR_CONSTANT_DELAY
#define MONITOR_DELAY	150 * 8		/* 150 ms of MONITORING DELAY */
#endif

/*
 * Prevent new channel allocation if shutting down.
 */
static int shutting_down = 0;

static int uniqueint = 0;

unsigned int cw_debugchan_flags;

/* XXX Lock appropriately in more functions XXX */

struct chanlist {
	const struct cw_channel_tech *tech;
	struct chanlist *next;
};

static struct chanlist *backends = NULL;

/* Protect the backend list */
CW_MUTEX_DEFINE_STATIC(chlock);


const struct cw_cause
{
	int cause;
	const char *desc;
} causes[] =
{
	{ CW_CAUSE_UNALLOCATED, "Unallocated (unassigned) number" },
	{ CW_CAUSE_NO_ROUTE_TRANSIT_NET, "No route to specified transmit network" },
	{ CW_CAUSE_NO_ROUTE_DESTINATION, "No route to destination" },
	{ CW_CAUSE_CHANNEL_UNACCEPTABLE, "Channel unacceptable" },
	{ CW_CAUSE_CALL_AWARDED_DELIVERED, "Call awarded and being delivered in an established channel" },
	{ CW_CAUSE_NORMAL_CLEARING, "Normal Clearing" },
	{ CW_CAUSE_USER_BUSY, "User busy" },
	{ CW_CAUSE_NO_USER_RESPONSE, "No user responding" },
	{ CW_CAUSE_NO_ANSWER, "User alerting, no answer" },
	{ CW_CAUSE_CALL_REJECTED, "Call Rejected" },
	{ CW_CAUSE_NUMBER_CHANGED, "Number changed" },
	{ CW_CAUSE_DESTINATION_OUT_OF_ORDER, "Destination out of order" },
	{ CW_CAUSE_INVALID_NUMBER_FORMAT, "Invalid number format" },
	{ CW_CAUSE_FACILITY_REJECTED, "Facility rejected" },
	{ CW_CAUSE_RESPONSE_TO_STATUS_ENQUIRY, "Response to STATus ENQuiry" },
	{ CW_CAUSE_NORMAL_UNSPECIFIED, "Normal, unspecified" },
	{ CW_CAUSE_NORMAL_CIRCUIT_CONGESTION, "Circuit/channel congestion" },
	{ CW_CAUSE_NETWORK_OUT_OF_ORDER, "Network out of order" },
	{ CW_CAUSE_NORMAL_TEMPORARY_FAILURE, "Temporary failure" },
	{ CW_CAUSE_SWITCH_CONGESTION, "Switching equipment congestion" },
	{ CW_CAUSE_ACCESS_INFO_DISCARDED, "Access information discarded" },
	{ CW_CAUSE_REQUESTED_CHAN_UNAVAIL, "Requested channel not available" },
	{ CW_CAUSE_PRE_EMPTED, "Pre-empted" },
	{ CW_CAUSE_FACILITY_NOT_SUBSCRIBED, "Facility not subscribed" },
	{ CW_CAUSE_OUTGOING_CALL_BARRED, "Outgoing call barred" },
	{ CW_CAUSE_INCOMING_CALL_BARRED, "Incoming call barred" },
	{ CW_CAUSE_BEARERCAPABILITY_NOTAUTH, "Bearer capability not authorized" },
	{ CW_CAUSE_BEARERCAPABILITY_NOTAVAIL, "Bearer capability not available" },
	{ CW_CAUSE_BEARERCAPABILITY_NOTIMPL, "Bearer capability not implemented" },
	{ CW_CAUSE_CHAN_NOT_IMPLEMENTED, "Channel not implemented" },
	{ CW_CAUSE_FACILITY_NOT_IMPLEMENTED, "Facility not implemented" },
	{ CW_CAUSE_INVALID_CALL_REFERENCE, "Invalid call reference value" },
	{ CW_CAUSE_INCOMPATIBLE_DESTINATION, "Incompatible destination" },
	{ CW_CAUSE_INVALID_MSG_UNSPECIFIED, "Invalid message unspecified" },
	{ CW_CAUSE_MANDATORY_IE_MISSING, "Mandatory information element is missing" },
	{ CW_CAUSE_MESSAGE_TYPE_NONEXIST, "Message type nonexist." },
	{ CW_CAUSE_WRONG_MESSAGE, "Wrong message" },
	{ CW_CAUSE_IE_NONEXIST, "Info. element nonexist or not implemented" },
	{ CW_CAUSE_INVALID_IE_CONTENTS, "Invalid information element contents" },
	{ CW_CAUSE_WRONG_CALL_STATE, "Message not compatible with call state" },
	{ CW_CAUSE_RECOVERY_ON_TIMER_EXPIRE, "Recover on timer expiry" },
	{ CW_CAUSE_MANDATORY_IE_LENGTH_ERROR, "Mandatory IE length error" },
	{ CW_CAUSE_PROTOCOL_ERROR, "Protocol error, unspecified" },
	{ CW_CAUSE_INTERWORKING, "Interworking, unspecified" },
};

/* Control frame types */
const struct cw_control
{
	int control;
	const char *desc;
} controles[] =
{
	{CW_CONTROL_HANGUP, "Other end has hungup"},
	{CW_CONTROL_RING, "Local ring"},
	{CW_CONTROL_RINGING, "Remote end is ringing"},
	{CW_CONTROL_ANSWER,"Remote end has answered"},
	{CW_CONTROL_BUSY, "Remote end is busy"},
	{CW_CONTROL_TAKEOFFHOOK, "Make it go off hook"},
	{CW_CONTROL_OFFHOOK, "Line is off hook"},
	{CW_CONTROL_CONGESTION, "Congestion (circuits busy"},
	{CW_CONTROL_FLASH, "Flash hook"},
	{CW_CONTROL_WINK, "Wink"},
	{CW_CONTROL_OPTION, "Set a low-level option"},
	{CW_CONTROL_RADIO_KEY, "Key Radio"},
	{CW_CONTROL_RADIO_UNKEY, "Un-Key Radio"},
	{CW_CONTROL_PROGRESS, "Indicate PROGRESS"},
	{CW_CONTROL_PROCEEDING, "Indicate CALL PROCEEDING"},
	{CW_CONTROL_HOLD, "Indicate call is placed on hold"},
	{CW_CONTROL_UNHOLD, "Indicate call is left from hold"},
	{CW_CONTROL_VIDUPDATE, "Indicate video frame update"},
};


static int cw_channel_qsort_compare_by_name(const void *a, const void *b)
{
	const struct cw_object * const *objp_a = a;
	const struct cw_object * const *objp_b = b;
	const struct cw_channel *channel_a = container_of(*objp_a, struct cw_channel, obj);
	const struct cw_channel *channel_b = container_of(*objp_b, struct cw_channel, obj);

	return strcasecmp(channel_a->name, channel_b->name);
}

static int channel_object_match(struct cw_object *obj, const void *pattern)
{
	struct cw_channel *chan = container_of(obj, struct cw_channel, obj);

	return !strcasecmp(chan->name, pattern);
}

struct cw_registry channel_registry = {
	.name = "Channels",
	.qsort_compare = cw_channel_qsort_compare_by_name,
	.match = channel_object_match,
};


static int device_object_match(struct cw_object *obj, const void *pattern)
{
	struct cw_channel *chan = container_of(obj, struct cw_channel, obj);
	int l = strlen(pattern);

	return (!strncasecmp(chan->name, pattern, l) && chan->name[l] == '-');
}

struct cw_registry device_registry = {
	.name = "Devices",
	.qsort_compare = cw_channel_qsort_compare_by_name,
	.match = device_object_match,
};


/* this code is broken - if someone knows how to rewrite the list traversal, please tell */
struct cw_variable *cw_channeltype_list(void)
{
#if 0
	struct chanlist *cl;
	struct cw_variable *var = NULL;
	struct cw_variable *prev = NULL;
#endif

	cw_log(CW_LOG_WARNING, "cw_channeltype_list() called (probably by res_snmp.so). This is not implemented yet.\n");
	return NULL;

/*	CW_LIST_TRAVERSE(&backends, cl, list) {  <-- original line from asterisk */

#if 0
	CW_LIST_TRAVERSE(&backends, cl, next)
    {
		if (prev)
        {
			if ((prev->next = cw_variable_new(cl->tech->type, cl->tech->description)))
				prev = prev->next;
		}
        else
        {
			var = cw_variable_new(cl->tech->type, cl->tech->description);
			prev = var;
		}
	}
	return var;
#endif
}

static int show_channeltypes(struct cw_dynstr *ds_p, int argc, char *argv[])
{
#define FORMAT  "%-10.10s  %-30.30s %-12.12s %-12.12s %-12.12s\n"
	struct chanlist *cl;

	CW_UNUSED(argc);
	CW_UNUSED(argv);

	cw_dynstr_tprintf(ds_p, 2,
		cw_fmtval(FORMAT, "Type", "Description",       "Devicestate", "Indications", "Transfer"),
		cw_fmtval(FORMAT, "----------", "-----------", "-----------", "-----------", "--------")
	);
	if (cw_mutex_lock(&chlock)) {
		cw_log(CW_LOG_WARNING, "Unable to lock channel list\n");
		return -1;
	}
	cl = backends;
	while (cl) {
		cw_dynstr_printf(ds_p, FORMAT, cl->tech->type, cl->tech->description,
			(cl->tech->devicestate) ? "yes" : "no", 
			(cl->tech->indicate) ? "yes" : "no",
			(cl->tech->transfer) ? "yes" : "no");
		cl = cl->next;
	}
	cw_mutex_unlock(&chlock);
	return RESULT_SUCCESS;

#undef FORMAT

}

static const char show_channeltypes_usage[] = 
"Usage: show channeltypes\n"
"       Shows available channel types registered in your CallWeaver server.\n";

static struct cw_clicmd cli_show_channeltypes = {
	.cmda = { "show", "channeltypes", NULL },
	.handler = show_channeltypes,
	.summary = "Show available channel types",
	.usage = show_channeltypes_usage,
};

/*--- cw_check_hangup: Checks to see if a channel is needing hang up */
int cw_check_hangup(struct cw_channel *chan)
{
	time_t	myt;

	/* if soft hangup flag, return true */
	if (chan->_softhangup) 
		return 1;
	/* if no technology private data, return true */
	if (!chan->tech_pvt) 
		return 1;
	/* if no hangup scheduled, just return here */
	if (!chan->whentohangup) 
		return 0;
	time(&myt); /* get current time */
	/* return, if not yet */
	if (chan->whentohangup > myt) 
		return 0;
	chan->_softhangup |= CW_SOFTHANGUP_TIMEOUT;
	return 1;
}


/*--- cw_begin_shutdown: Initiate system shutdown */
static int channel_hangup_one(struct cw_object *obj, void *data)
{
	struct cw_channel *chan = container_of(obj, struct cw_channel, obj);

	CW_UNUSED(data);

	cw_softhangup(chan, CW_SOFTHANGUP_SHUTDOWN);
	return 0;
}

void cw_begin_shutdown(int hangup)
{
	shutting_down = 1;
	if (hangup)
		cw_registry_iterate(&channel_registry, channel_hangup_one, NULL);
}


/*--- cw_cancel_shutdown: Cancel a shutdown in progress */
void cw_cancel_shutdown(void)
{
	shutting_down = 0;
}

/*--- cw_shutting_down: Returns non-zero if CallWeaver is being shut down */
int cw_shutting_down(void)
{
	return shutting_down;
}

/*--- cw_channel_setwhentohangup: Set when to hangup channel */
void cw_channel_setwhentohangup(struct cw_channel *chan, time_t offset)
{
	time_t	myt;

	time(&myt);
	if (offset)
		chan->whentohangup = myt + offset;
	else
		chan->whentohangup = 0;
	cw_queue_frame(chan, &cw_null_frame);
	return;
}
/*--- cw_channel_cmpwhentohangup: Compare a offset with when to hangup channel */
int cw_channel_cmpwhentohangup(struct cw_channel *chan, time_t offset)
{
	time_t whentohangup;

	if (chan->whentohangup == 0) {
		if (offset == 0)
			return (0);
		else
			return (-1);
	} else { 
		if (offset == 0)
			return (1);
		else {
			whentohangup = offset + time (NULL);
			if (chan->whentohangup < whentohangup)
				return (1);
			else if (chan->whentohangup == whentohangup)
				return (0);
			else
				return (-1);
		}
	}
}

/*--- cw_channel_register: Register a new telephony channel in CallWeaver */
int cw_channel_register(const struct cw_channel_tech *tech)
{
	struct chanlist *chan;

	cw_mutex_lock(&chlock);

	chan = backends;
	while (chan) {
		if (!strcasecmp(tech->type, chan->tech->type)) {
			cw_log(CW_LOG_WARNING, "Already have a handler for type '%s'\n", tech->type);
			cw_mutex_unlock(&chlock);
			return -1;
		}
		chan = chan->next;
	}

	chan = malloc(sizeof(*chan));
	if (!chan) {
		cw_log(CW_LOG_WARNING, "Out of memory\n");
		cw_mutex_unlock(&chlock);
		return -1;
	}
	chan->tech = tech;
	chan->next = backends;
	backends = chan;

	if (option_debug)
		cw_log(CW_LOG_DEBUG, "Registered handler for '%s' (%s)\n", chan->tech->type, chan->tech->description);

	if (option_verbose > 1)
		cw_verbose(VERBOSE_PREFIX_2 "Registered channel type '%s' (%s)\n", chan->tech->type,
				chan->tech->description);

	cw_mutex_unlock(&chlock);
	return 0;
}

void cw_channel_unregister(const struct cw_channel_tech *tech)
{
	struct chanlist *chan, *last=NULL;

	if (option_debug)
		cw_log(CW_LOG_DEBUG, "Unregistering channel type '%s'\n", tech->type);

	cw_mutex_lock(&chlock);

	chan = backends;
	while (chan) {
		if (chan->tech == tech) {
			if (last)
				last->next = chan->next;
			else
				backends = backends->next;
			free(chan);
			cw_mutex_unlock(&chlock);

			if (option_verbose > 1)
				cw_verbose( VERBOSE_PREFIX_2 "Unregistered channel type '%s'\n", tech->type);

			return;
		}
		last = chan;
		chan = chan->next;
	}

	cw_mutex_unlock(&chlock);
}

const struct cw_channel_tech *cw_get_channel_tech(const char *name)
{
	struct chanlist *chanls;

	if (cw_mutex_lock(&chlock)) {
		cw_log(CW_LOG_WARNING, "Unable to lock channel tech list\n");
		return NULL;
	}

	for (chanls = backends; chanls; chanls = chanls->next) {
		if (strcasecmp(name, chanls->tech->type))
			continue;

		cw_mutex_unlock(&chlock);
		return chanls->tech;
	}

	cw_mutex_unlock(&chlock);
	return NULL;
}

/*--- cw_cause2str: Gives the string form of a given hangup cause */
const char *cw_cause2str(int cause)
{
	int x;

	for (x=0; x < sizeof(causes) / sizeof(causes[0]); x++) 
		if (causes[x].cause == cause)
			return causes[x].desc;

	return "Unknown";
}

/*--- cw_control2str: Gives the string form of a given control frame */
const char *cw_control2str(int control)
{
	int x;

	for (x=0; x < sizeof(controles) / sizeof(controles[0]); x++) 
		if (controles[x].control == control)
			return controles[x].desc;

	return "Unknown";
}

/*--- cw_state2str: Gives the string form of a given channel state */
const char *cw_state2str(int state)
{
	/* XXX Not reentrant XXX */
	static char localtmp[256];
	switch(state) {
	case CW_STATE_DOWN:
		return "Down";
	case CW_STATE_RESERVED:
		return "Rsrvd";
	case CW_STATE_OFFHOOK:
		return "OffHook";
	case CW_STATE_DIALING:
		return "Dialing";
	case CW_STATE_RING:
		return "Ring";
	case CW_STATE_RINGING:
		return "Ringing";
	case CW_STATE_UP:
		return "Up";
	case CW_STATE_BUSY:
		return "Busy";
	default:
		snprintf(localtmp, sizeof(localtmp), "Unknown (%d)\n", state);
		return localtmp;
	}
}

/*--- cw_transfercapability2str: Gives the string form of a given transfer capability */
const char *cw_transfercapability2str(int transfercapability)
{
	switch(transfercapability) {
	case CW_TRANS_CAP_SPEECH:
		return "SPEECH";
	case CW_TRANS_CAP_DIGITAL:
		return "DIGITAL";
	case CW_TRANS_CAP_RESTRICTED_DIGITAL:
		return "RESTRICTED_DIGITAL";
	case CW_TRANS_CAP_3_1K_AUDIO:
		return "3K1AUDIO";
	case CW_TRANS_CAP_DIGITAL_W_TONES:
		return "DIGITAL_W_TONES";
	case CW_TRANS_CAP_VIDEO:
		return "VIDEO";
	default:
		return "UNKNOWN";
	}
}

/*--- cw_best_codec: Pick the best codec */
int cw_best_codec(int fmts)
{
	/* This just our opinion, expressed in code.  We are asked to choose
	   the best codec to use, given no information */
	int x;
	static const int prefs[] = 
	{
		/* Okay, ulaw is used by all telephony equipment, so start with it */
		CW_FORMAT_ULAW,
		/* Unless of course, you're a European, so then prefer ALAW */
		CW_FORMAT_ALAW,
		/* Okay, well, signed linear is easy to translate into other stuff */
		CW_FORMAT_SLINEAR,
		/* G.726 is standard ADPCM */
		CW_FORMAT_G726,
		/* ADPCM has great sound quality and is still pretty easy to translate */
		CW_FORMAT_DVI_ADPCM,
		/* Okay, we're down to vocoders now, so pick GSM because it's small and easier to
		   translate and sounds pretty good */
		CW_FORMAT_GSM,
		/* iLBC is not too bad */
		CW_FORMAT_ILBC,
		/* Speex is free, but computationally more expensive than GSM */
		CW_FORMAT_SPEEX,
		/* Ick, LPC10 sounds terrible, but at least we have code for it, if you're tacky enough
		   to use it */
		CW_FORMAT_LPC10,
		/* G.729a is faster than 723 and slightly less expensive */
		CW_FORMAT_G729A,
		/* Down to G.723.1 which is proprietary but at least designed for voice */
		CW_FORMAT_G723_1,
        CW_FORMAT_OKI_ADPCM,
	};

	/* Find the first prefered codec in the format given */
	for (x = 0;  x < (sizeof(prefs)/sizeof(prefs[0]));  x++)
	{
		if (fmts & prefs[x])
			return prefs[x];
	}
	cw_log(CW_LOG_WARNING, "Don't know any of 0x%x formats\n", fmts);
	return 0;
}

static const struct cw_channel_tech null_tech =
{
	.type = "NULL",
	.description = "Null channel (should not see this)",
};


static void free_cid(struct cw_callerid *cid)
{
	free(cid->cid_dnid);
	free(cid->cid_num);
	free(cid->cid_name);
	free(cid->cid_ani);
	free(cid->cid_rdnis);
}


static void cw_channel_release(struct cw_object *obj)
{
	struct cw_channel *chan = container_of(obj, struct cw_channel, obj);
	struct cw_frame *f;

	if (option_debug) cw_log(CW_LOG_DEBUG, "%p: %s", chan, chan->name);

	if (chan->tech_pvt)
		cw_log(CW_LOG_WARNING, "Channel '%s' may not have been hung up properly\n", chan->name);

	free_cid(&chan->cid);

	/* Close pipes if appropriate */
	if (chan->alertpipe[0] > -1)
		close(chan->alertpipe[0]);
	if (chan->alertpipe[1] > -1)
		close(chan->alertpipe[1]);

	while ((f = chan->readq)) {
		chan->readq = chan->readq->next;
		cw_fr_free(f);
	}

	cw_mutex_destroy(&chan->lock);
	cw_registry_destroy(&chan->vars);

	/* Drop out of the group counting radar */
	cw_app_group_discard(chan);

	/* Destroy the jitterbuffer */
	cw_jb_destroy(chan);

	cw_object_destroy(chan);
	free(chan);
}


/*--- cw_channel_alloc: Create a new channel structure */
struct cw_channel *cw_channel_alloc(int needqueue, const char *fmt, ...)
{
	va_list ap;
	struct cw_channel *chan;
	int x;

	if (!shutting_down) {
		if ((chan = calloc(1, sizeof(*chan)))) {
			chan->alertpipe[0] = chan->alertpipe[1] = -1;
			for (x = 0;  x < CW_MAX_FDS;  x++)
				chan->fds[x] = -1;

			if (needqueue) {
				if (!pipe(chan->alertpipe)) {
					fcntl(chan->alertpipe[0], F_SETFL, fcntl(chan->alertpipe[0], F_GETFL) | O_NONBLOCK);
					fcntl(chan->alertpipe[1], F_SETFL, fcntl(chan->alertpipe[1], F_GETFL) | O_NONBLOCK);

					/* Always watch the alertpipe */
					chan->fds[CW_MAX_FDS-1] = chan->alertpipe[0];
				} else {
					cw_log(CW_LOG_WARNING, "Channel allocation failed: Can't create alert pipe!\n");
					free(chan);
					return NULL;
				}
			}

			chan->tech = &null_tech;

			/* Initial state */
			chan->_state = CW_STATE_DOWN;
			chan->appl = NULL;
			chan->flags = cw_debugchan_flags;
			chan->generator.tid = CW_PTHREADT_NULL;
			chan->t38_status = T38_STATUS_UNKNOWN;

			strcpy(chan->context, "default");
			strcpy(chan->exten, "s");
			chan->priority = 1;

//			snprintf(chan->uniqueid, sizeof(chan->uniqueid), "%li.%d", (long) time(NULL), uniqueint++);
			if (cw_strlen_zero(cw_config[CW_SYSTEM_NAME]))
				snprintf(chan->uniqueid, sizeof(chan->uniqueid), "%li.%d", (long) time(NULL), uniqueint++);
			else
				snprintf(chan->uniqueid, sizeof(chan->uniqueid), "%s-%li.%d", cw_config[CW_SYSTEM_NAME], (long) time(NULL), uniqueint++);

			cw_mutex_init(&chan->lock);
			cw_var_registry_init(&chan->vars, 1024);

			chan->amaflags = cw_default_amaflags;
			cw_copy_string(chan->accountcode, cw_default_accountcode, sizeof(chan->accountcode));
			cw_copy_string(chan->language, defaultlanguage, sizeof(chan->language));

			cw_object_init(chan, NULL, 1);
			chan->obj.release = cw_channel_release;

			if (fmt) {
				const char *p;

				va_start(ap, fmt);
				vsnprintf((char *)chan->name, sizeof(chan->name), fmt, ap);
				va_end(ap);

				chan->reg_entry = cw_registry_add(&channel_registry, cw_hash_string(0, chan->name), &chan->obj);
				if ((p = strrchr(chan->name, '-'))) {
					const char *q;
					unsigned int hash;

					for (q = chan->name, hash = 0; q != p; hash = cw_hash_add(hash, *(q++)));
					chan->dev_reg_entry = cw_registry_add(&device_registry, hash, &chan->obj);
				}
			}

			if (option_debug) cw_log(CW_LOG_DEBUG, "%p: %s", chan, chan->name);

			/* N.B. cw_channel_alloc returns an uncounted reference. The reference
			 * we have here is expected to be released by cw_channel_free(). The expectation
			 * is that the alloc/free sequence is owned by the calling technology and
			 * that guarantees not to access the channel after a hangup. All other
			 * channel access come via look ups of the channel in registries and
			 * thus get counted references that need to be put.
			 * Logically you can think of this as a channel driver getting a reference
			 * to the channel with cw_channel_alloc() and putting it with cw_hangup
			 * or cw_channel_free(). This means the channel driver owns a reference
			 * to the channel throughout its natural life.
			 */
			return chan;
		}

		cw_log(CW_LOG_ERROR, "Out of memory\n");
		return NULL;
	}

	cw_log(CW_LOG_NOTICE, "Refusing channel allocation due to active shutdown\n");
	return NULL;
}

/* Sets the channel t38 status */
void cw_channel_perform_set_t38_status( struct cw_channel *tmp, t38_status_t status, const char *file, int line )
{
    if ( !tmp ) {
	cw_log(CW_LOG_NOTICE,"cw_channel_set_t38_status called with NULL channel at %s:%d\n", file, line);
	return;
    }
    cw_log(CW_LOG_NOTICE,"Setting t38 status to %d for %s at %s:%d\n", status, tmp->name, file, line);
    tmp->t38_status = status;
}

/* Gets the channel t38 status */
t38_status_t cw_channel_get_t38_status( struct cw_channel *tmp ) 
{
    if ( !tmp )
	return T38_STATUS_UNKNOWN;
    else
	return tmp->t38_status;
}



/*--- cw_queue_frame: Queue an outgoing media frame */
int cw_queue_frame(struct cw_channel *chan, struct cw_frame *fin)
{
	struct cw_frame *f;
	struct cw_frame *prev, *cur;
	int qlen = 0;

	/* Build us a copy and free the original one */
	if ((f = cw_frdup(fin)) == NULL)
	{
		cw_log(CW_LOG_WARNING, "Unable to duplicate frame\n");
		return -1;
	}
	cw_channel_lock(chan);
	prev = NULL;
	for (cur = chan->readq;  cur;  cur = cur->next)
	{
		if ((cur->frametype == CW_FRAME_CONTROL) && (cur->subclass == CW_CONTROL_HANGUP))
			{
			/* Don't bother actually queueing anything after a hangup */
			cw_fr_free(f);
			cw_channel_unlock(chan);
			return 0;
		}
		prev = cur;
		qlen++;
	}
	/* Allow up to 96 voice frames outstanding, and up to 128 total frames */
	if (((fin->frametype == CW_FRAME_VOICE) && (qlen > 96)) || (qlen  > 128))
	{
		if (fin->frametype != CW_FRAME_VOICE)
			{
			cw_log(CW_LOG_ERROR, "Dropping non-voice (type %d) frame for %s due to long queue length\n", fin->frametype, chan->name);
		}
			else
			{	
			cw_log(CW_LOG_WARNING, "Dropping voice frame for %s due to exceptionally long queue\n", chan->name);
		}
		cw_fr_free(f);
		cw_channel_unlock(chan);
		return 0;
	}
	if (prev)
		prev->next = f;
	else
		chan->readq = f;

	if (chan->alertpipe[1] > -1)
	{
		if (write(chan->alertpipe[1], &chan, 1) != 1)
			cw_log(CW_LOG_WARNING, 
				"Unable to write to alert pipe on %s, frametype/subclass %d/%d (qlen = %d): %s!\n",
				chan->name,
				f->frametype,
				f->subclass,
				qlen,
				strerror(errno)
			);
	} else if (cw_test_flag(chan, CW_FLAG_BLOCKING)) {
		pthread_kill(chan->blocker, SIGURG);
	}
	cw_channel_unlock(chan);
	return 0;
}

/*--- cw_queue_hangup: Queue a hangup frame for channel */
int cw_queue_hangup(struct cw_channel *chan)
{
	struct cw_frame f = { CW_FRAME_CONTROL, CW_CONTROL_HANGUP };
	/* Yeah, let's not change a lock-critical value without locking */
	if (!cw_channel_trylock(chan)) {
		chan->_softhangup |= CW_SOFTHANGUP_DEV;
		cw_channel_unlock(chan);
	}
	return cw_queue_frame(chan, &f);
}

/*--- cw_queue_control: Queue a control frame */
int cw_queue_control(struct cw_channel *chan, int control)
{
	struct cw_frame f = { CW_FRAME_CONTROL, };
	f.subclass = control;
	return cw_queue_frame(chan, &f);
}

/*--- cw_channel_defer_dtmf: Set defer DTMF flag on channel */
int cw_channel_defer_dtmf(struct cw_channel *chan)
{
	int pre = 0;

	if (chan) {
		pre = cw_test_flag(chan, CW_FLAG_DEFER_DTMF);
		cw_set_flag(chan, CW_FLAG_DEFER_DTMF);
	}
	return pre;
}

/*--- cw_channel_undefer_dtmf: Unset defer DTMF flag on channel */
void cw_channel_undefer_dtmf(struct cw_channel *chan)
{
	if (chan)
		cw_clear_flag(chan, CW_FLAG_DEFER_DTMF);
}


/*--- cw_get_channel_by_name_locked: Get channel by name and lock it */
#ifdef DEBUG_MUTEX
struct cw_channel *__cw_get_by_name_locked(struct cw_registry *registry, const char *name, const char *file, int lineno, const char *func)
#else
struct cw_channel *__cw_get_by_name_locked(struct cw_registry *registry, const char *name)
#endif
{
	static const struct timespec ts = { .tv_sec = 0, .tv_nsec = 3000000 };
	struct cw_object *obj;
	int tries = 10;

	/* We are most likely already holding a locked channel at this point.
	 * Since another thread may be waiting on that lock while holding the
	 * lock on the channel we want here we're going to try non-blocking
	 * locks up to 10 times with 3ms between tries and then give in and
	 * pretend the channel doesn't exist.
	 * Why 3ms? Because older Linux systems will busy wait delays up to
	 * 2ms rather than rescheduling.
	 */
	while (tries-- && (obj = cw_registry_find(registry, 1, cw_hash_string(0, name), name))) {
		struct cw_channel *chan = container_of(obj, struct cw_channel, obj);
#ifdef DEBUG_MUTEX
		if (!cw_mutex_trylock_debug(&chan->lock, 1, file, lineno, func, chan->name)) {
#else
		if (!cw_channel_trylock(chan)) {
#endif
			if (chan->reg_entry)
				return chan;
			cw_channel_unlock(chan);
		} else
			nanosleep(&ts, NULL);
		cw_object_put(chan);
	}
	return NULL;
}


struct complete_channel_args {
	struct cw_dynstr *ds_p;
	const char *prefix;
	size_t prefix_len;
};

static int complete_channel_one(struct cw_object *obj, void *data)
{
	struct cw_channel *chan = container_of(obj, struct cw_channel, obj);
	struct complete_channel_args *args = data;

	/* FIXME: we only need to lock because there are things that rename existing
	 * channels. Renaming channels is evil.
	 */
	cw_channel_lock(chan);

	if (!strncasecmp(chan->name, args->prefix, args->prefix_len))
		cw_dynstr_printf(args->ds_p, "%s\n", chan->name);

	cw_channel_unlock(chan);
	return 0;
}

void cw_complete_channel(struct cw_dynstr *ds_p, const char *prefix, size_t prefix_len)
{
	struct complete_channel_args args = {
		.ds_p = ds_p,
		.prefix = prefix,
		.prefix_len = prefix_len,
	};

	cw_registry_iterate(&channel_registry, complete_channel_one, &args);
}


/*--- cw_get_channel_by_name_prefix_locked: Get channel by name prefix and lock it */
/* FIXME: this is used by device state to find the device (name is <tech>/<device>-<call>)
 * and by ChanGrab
 */
struct channel_by_name_prefix_args {
	const char *prefix;
	size_t prefix_len;
	struct cw_channel *chan;
#ifdef DEBUG_MUTEX
	const char *file;
	int lineno;
	const char *func;
#endif
};

static int channel_by_name_prefix_one(struct cw_object *obj, void *data)
{
	static const struct timespec ts = { .tv_sec = 0, .tv_nsec = 3000000 };
	struct cw_channel *chan = container_of(obj, struct cw_channel, obj);
	struct channel_by_name_prefix_args *args = data;
	int tries = 10;

	if (strncasecmp(chan->name, args->prefix, args->prefix_len))
		return 0;

	/* We are most likely already holding a locked channel at this point.
	 * Since another thread may be waiting on that lock while holding the
	 * lock on the channel we want here we're going to try non-blocking
	 * locks up to 10 times with 3ms between tries and then give in and
	 * pretend the channel doesn't exist.
	 * Why 3ms? Because older Linux systems will busy wait delays up to
	 * 2ms rather than rescheduling.
	 */
	while (tries--) {
#ifdef DEBUG_MUTEX
		if (!cw_mutex_trylock_debug(&chan->lock, 1, args->file, args->lineno, args->func, chan->name)) {
#else
		if (!cw_channel_trylock(chan)) {
#endif
			if (chan->reg_entry) {
				args->chan = cw_object_dup(chan);
				return 1;
			}
			cw_channel_unlock(chan);
			break;
		} else
			nanosleep(&ts, NULL);
	}

	return 0;
}

#ifdef DEBUG_MUTEX
struct cw_channel *__cw_get_channel_by_name_prefix_locked(const char *prefix, size_t prefix_len, const char *file, int lineno, const char *func)
#else
struct cw_channel *cw_get_channel_by_name_prefix_locked(const char *prefix, size_t prefix_len)
#endif
{
	struct channel_by_name_prefix_args args = {
		.prefix = prefix,
		.prefix_len = prefix_len,
		.chan = NULL,
#ifdef DEBUG_MUTEX
		.file = file,
		.lineno = lineno,
		.func = func,
#endif
	};

	cw_registry_iterate(&channel_registry, channel_by_name_prefix_one, &args);
	return args.chan;
}


/*--- cw_get_channel_by_exten_locked: Get channel by exten (and optionally context) and lock it */
/* FIXME: this is used in call pickup - really we want a registry of just ringing channels keyed by context/exten */
struct channel_by_exten_prefix_args {
	const char *context;
	const char *exten;
	struct cw_channel *chan;
#ifdef DEBUG_MUTEX
	const char *file;
	int lineno;
	const char *func;
#endif
};

static int channel_by_exten_one(struct cw_object *obj, void *data)
{
	static const struct timespec ts = { .tv_sec = 0, .tv_nsec = 3000000 };
	struct cw_channel *chan = container_of(obj, struct cw_channel, obj);
	struct channel_by_exten_prefix_args *args = data;
	int tries = 10;

	if ((args->context && strcasecmp(chan->context, args->context))
	|| strcasecmp(chan->exten, args->exten))
		return 0;

	/* We are most likely already holding a locked channel at this point.
	 * Since another thread may be waiting on that lock while holding the
	 * lock on the channel we want here we're going to try non-blocking
	 * locks up to 10 times with 3ms between tries and then give in and
	 * pretend the channel doesn't exist.
	 * Why 3ms? Because older Linux systems will busy wait delays up to
	 * 2ms rather than rescheduling.
	 */
	while (tries--) {
#ifdef DEBUG_MUTEX
		if (!cw_mutex_trylock_debug(&chan->lock, 1, args->file, args->lineno, args->func, chan->name)) {
#else
		if (!cw_channel_trylock(chan)) {
#endif
			if (chan->reg_entry) {
				args->chan = cw_object_dup(chan);
				return 1;
			}
			cw_channel_unlock(chan);
			break;
		} else
			nanosleep(&ts, NULL);
	}

	return 0;
}

#ifdef DEBUG_MUTEX
struct cw_channel *__cw_get_channel_by_exten_locked(const char *exten, const char *context, const char *file, int lineno, const char *func)
#else
struct cw_channel *cw_get_channel_by_exten_locked(const char *exten, const char *context)
#endif
{
	struct channel_by_exten_prefix_args args = {
		.context = context,
		.exten = exten,
		.chan = NULL,
#ifdef DEBUG_MUTEX
		.file = file,
		.lineno = lineno,
		.func = func,
#endif
	};

	cw_registry_iterate(&channel_registry, channel_by_exten_one, &args);
	return args.chan;
}


/*--- cw_safe_sleep_conditional: Wait, look for hangups and condition arg */
int cw_safe_sleep_conditional(	struct cw_channel *chan, int ms,
	int (*cond)(void*), void *data )
{
	struct cw_frame *f;

	while (ms > 0)
	{
		if (cond  &&  ((*cond)(data) == 0))
			return 0;
		ms = cw_waitfor(chan, ms);
		if (ms <0)
			return -1;
		if (ms > 0)
			{
			f = cw_read(chan);
			if (!f)
				return -1;
			cw_fr_free(f);
		}
	}
	return 0;
}

/*--- cw_safe_sleep: Wait, look for hangups */
int cw_safe_sleep(struct cw_channel *chan, int ms)
{
	struct cw_frame *f;
	
	while (ms > 0)
	{
		ms = cw_waitfor(chan, ms);
		if (ms < 0)
			return -1;
		if (ms > 0)
			{
			f = cw_read(chan);
			if (!f)
				return -1;
			cw_fr_free(f);
		}
	}
	return 0;
}


/*--- cw_spy_detach_all: Detach all spies from a channel. */
/*! Detach all spies from a channel.
 * The caller must already hold the channel's lock.
 * \param chan The (already locked) channel.
 * \internal We don't have to deallocate the spy structures. It is sufficient to
 * mark the spies as 'done', and they will know what to do.
 */
void cw_spy_detach_all(struct cw_channel *chan) 
{
	struct cw_channel_spy *chanspy;

	for (chanspy = chan->spies;  chanspy;  chanspy = chanspy->next)
	{
		if (chanspy->status == CHANSPY_RUNNING)
			chanspy->status = CHANSPY_DONE;
	}
	chan->spies = NULL;
}

/*--- cw_spy_attach: Attach another spy in the given channel. */
/*! Attach a spy on a channel.
 * \param chan The (unlocked) channel we want to attach the spy on.
 * \param newspy The new and initialized spy we will attach.
 * \internal If the channel is in a native bridge, attaching a spy will
 * also break the bridge to force the traffic to pass through CW.
 */
void cw_spy_attach(struct cw_channel *chan, struct cw_channel_spy *newspy)
{
	struct cw_channel *peer;

	cw_channel_lock(chan);
	newspy->next = chan->spies;
	chan->spies = newspy;
	cw_channel_unlock(chan);
	if (cw_test_flag(chan, CW_FLAG_NBRIDGE) && (peer = cw_bridged_channel(chan))) {
		cw_softhangup(peer, CW_SOFTHANGUP_UNBRIDGE);
		cw_object_put(peer);
	}
}

/*--- cw_spy_detach: Detach a spy from the given channel. */
/*! Detach a spy from a channel.
 * The caller must already have the channel's lock.
 * \param chan The (already locked) channel.
 * \param newspy The spy we will detach.
 * \internal This only remove the spy from the channel's spy queue, it will not
 * deallocate any memory.
 */
void cw_spy_detach(struct cw_channel *chan, struct cw_channel_spy *oldspy)
{
	struct cw_channel_spy *cur, *prev = NULL;
	for (cur = chan->spies ; cur && cur != oldspy; cur = cur->next) {
		prev = cur;
	}
	if (cur) { /* We found the spy in our list. */
		if (chan->spies == cur)
			chan->spies = cur->next;
		else
			prev->next = cur->next;
	} else { /* Is this ever possible? */
		cw_log(CW_LOG_WARNING, "Unknown spy in cw_spy_detach().\n");
	}
}

/*--- cw_softhangup_nolock: Softly hangup a channel, don't lock */
int cw_softhangup_nolock(struct cw_channel *chan, int cause)
{
	int res = 0;

	if (option_debug)
		cw_log(CW_LOG_DEBUG, "Soft-Hanging up channel '%s', cause %d\n", chan->name, cause);
	/* Inform channel driver that we need to be hung up, if it cares */
	chan->_softhangup |= cause;
	cw_queue_frame(chan, &cw_null_frame);
	/* Interrupt any poll call or such */
	if (cw_test_flag(chan, CW_FLAG_BLOCKING))
		pthread_kill(chan->blocker, SIGURG);
	return res;
}

/*--- cw_softhangup_nolock: Softly hangup a channel, lock */
int cw_softhangup(struct cw_channel *chan, int cause)
{
	int res;

	cw_channel_lock(chan);
	res = cw_softhangup_nolock(chan, cause);
	cw_channel_unlock(chan);
	return res;
}

/*--- cw_spy_get_frames: Get as many frames as BOTH parts can give. */
/** Get as many frames as BOTH parts can give.
 * This will make \c f0 point to a list of frames from the first queue and \c f1
 * point to a list of frames from the second queue. Both list will have the same
 * number of elements.
 *
 * After calling this function the queues will have released the specified
 * frames and it's the caller's responsibility to deallocate them.
 *
 * @param spy The (unlocked) spy.
 * @param f0 The start of the frames from the first queue. This can become NULL
 * if no frames were available in either queue.
 * @param f1 The start of the frames from the second queue. This can become NULL
 * if no frames were available in either queue.
 */
void cw_spy_get_frames(struct cw_channel_spy *spy, struct cw_frame **f0, struct cw_frame **f1)
{
    unsigned left, right;
    unsigned same;
    cw_mutex_lock(&spy->lock);
    left = spy->queue[0].count;
    right = spy->queue[1].count;
    same = left < right ? left : right;
    if (same == 0) {
		*f0 = *f1 = NULL;
    } else {
		int ii;
		struct cw_frame *f = spy->queue[0].head;
		for (ii = 1; ii < same; ++ii)
			f = f->next;
		*f0 = spy->queue[0].head;
		spy->queue[0].head = f->next;
		spy->queue[0].count -= same;
		if (spy->queue[0].count == 0)
			spy->queue[0].tail = NULL;
		f->next = NULL;
		f = spy->queue[1].head;
		for (ii = 1; ii < same; ++ii)
			f = f->next;
		*f1 = spy->queue[1].head;
		spy->queue[1].head = f->next;
		spy->queue[1].count -= same;
		if (spy->queue[1].count == 0)
			spy->queue[1].tail = NULL;
		f->next = NULL;
    }
    cw_mutex_unlock(&spy->lock);
}

/*--- cw_spy_queue_frame: Add a frame to a spy. */
/** Copy a frame for a spy.
 * @param spy Pointer to the (unlocked) spy.
 * @param f Pointer to the frame to duplicate.
 * @param pos 0 for the first queue, 1 for the second.
 */
void cw_spy_queue_frame(struct cw_channel_spy *spy, struct cw_frame *f, int pos)
{
    cw_mutex_lock(&spy->lock);
    if (spy->queue[pos].count > 1000) {
        struct cw_frame *headf = spy->queue[pos].head;
        struct cw_frame *tmpf = headf;
        cw_log(CW_LOG_ERROR, "Too many frames queued at once, flushing cache.\n");
        /* Set the queue as empty and unlock.*/
        spy->queue[pos].head = spy->queue[pos].tail = NULL;
        spy->queue[pos].count = 0;
        cw_mutex_unlock(&spy->lock);
        /* Free the wasted frames */
        while (tmpf) {
            struct cw_frame *freef = tmpf;
            tmpf = tmpf->next;
            cw_fr_free(freef);
        }
        return;
    } else {
        struct cw_frame *tmpf = cw_frdup(f);
        if (!tmpf) {
            cw_log(CW_LOG_WARNING, "Unable to duplicate frame\n");
        } else {
            ++spy->queue[pos].count;
            if (spy->queue[pos].tail)
                spy->queue[pos].tail->next = tmpf;
            else
                spy->queue[pos].head = tmpf;
            spy->queue[pos].tail = tmpf;
        }
    }
    cw_mutex_unlock(&spy->lock);
}


static void free_translation(struct cw_channel *chan)
{
	if (chan->writetrans)
		cw_translator_free_path(chan->writetrans);
	if (chan->readtrans)
		cw_translator_free_path(chan->readtrans);
	chan->writetrans = NULL;
	chan->readtrans = NULL;
	chan->rawwriteformat = chan->nativeformats;
	chan->rawreadformat = chan->nativeformats;
}


/*--- cw_channel_free: Free a channel structure */
void cw_channel_free(struct cw_channel *chan)
{
	cw_channel_lock(chan);

	if (chan->reg_entry) {
		cw_registry_del(&channel_registry, chan->reg_entry);
		if (chan->dev_reg_entry)
			cw_registry_del(&device_registry, chan->dev_reg_entry);
		chan->reg_entry = chan->dev_reg_entry = NULL;
	}

	if (chan->pbx)
		cw_log(CW_LOG_WARNING, "PBX may not have been terminated properly on '%s'\n", chan->name);

	if (chan->stream)
		cw_stopstream(chan);

	if (chan->monitor)
		chan->monitor->stop(chan, 0);

	cw_generator_deactivate(&chan->generator);

	if (chan->music_state)
		cw_moh_cleanup(chan);

	free_translation(chan);

	if (chan->name[0])
		cw_device_state_changed_literal(chan->name);

	cw_channel_unlock(chan);

	/* This is the reference that was created by cw_channel_alloc that
	 * we are putting here.
	 */
	cw_object_put(chan);
}


/*--- cw_hangup: Hangup a channel */
int cw_hangup(struct cw_channel *chan)
{
	int res = 0;

	/* Don't actually hang up a channel that will masquerade as someone else, or
	   if someone is going to masquerade as us */
	cw_channel_lock(chan);

	cw_spy_detach_all(chan);	/* get rid of spies */

	if (chan->masq)
	{
		if (cw_do_masquerade(chan)) 
			cw_log(CW_LOG_WARNING, "Failed to perform masquerade\n");
	}

	if (chan->masq)
	{
		cw_log(CW_LOG_WARNING, "%s getting hung up, but someone is trying to masq into us?!?\n", chan->name);
		cw_channel_unlock(chan);
		return 0;
	}
	/* If this channel is one which will be masqueraded into something, 
	   mark it as a zombie already, so we know to free it later */
	if (chan->masqr)
	{
		cw_set_flag(chan, CW_FLAG_ZOMBIE);
		cw_channel_unlock(chan);
		return 0;
	}

	if (chan->cdr)
	{
        /* End the CDR if it hasn't already */ 
		cw_cdr_end(chan->cdr);
		cw_cdr_detach(chan->cdr);	/* Post and Free the CDR */ 
		chan->cdr = NULL;
	}
	if (cw_test_flag(chan, CW_FLAG_BLOCKING))
	{
		cw_log(CW_LOG_WARNING, "Hard hangup called by thread %ld on %s, while fd "
					"is blocked by thread %ld!  Expect a failure\n",
					(long)pthread_self(), chan->name, (long)chan->blocker);
		CRASH;
	}
	if (!cw_test_flag(chan, CW_FLAG_ZOMBIE))
	{
		if (option_debug)
			cw_log(CW_LOG_DEBUG, "Hanging up channel '%s'\n", chan->name);
		if (chan->tech->hangup)
			res = chan->tech->hangup(chan);
	}
	else
	{
		if (option_debug)
			cw_log(CW_LOG_DEBUG, "Hanging up zombie '%s'\n", chan->name);
	}

	cw_channel_unlock(chan);

	cw_manager_event(CW_EVENT_FLAG_CALL, "Hangup",
		4,
		cw_msg_tuple("Channel",   "%s", chan->name),
		cw_msg_tuple("Uniqueid",  "%s", chan->uniqueid),
		cw_msg_tuple("Cause",     "%d", chan->hangupcause),
		cw_msg_tuple("Cause-txt", "%s", cw_cause2str(chan->hangupcause))
	);

	cw_channel_free(chan);
	return res;
}

int cw_answer(struct cw_channel *chan)
{
	int res = 0;

	cw_channel_lock(chan);
	/* Stop if we're a zombie or need a soft hangup */
	if (cw_test_flag(chan, CW_FLAG_ZOMBIE) || cw_check_hangup(chan))
	{
		cw_channel_unlock(chan);
		return -1;
	}
	switch (chan->_state)
	{
		case CW_STATE_RINGING:
		case CW_STATE_RING:
		if (chan->tech->answer)
			res = chan->tech->answer(chan);
		cw_setstate(chan, CW_STATE_UP);
		if (chan->cdr)
			cw_cdr_answer(chan->cdr);
		cw_channel_unlock(chan);
		return res;
		break;
		case CW_STATE_UP:
		if (chan->cdr)
			cw_cdr_answer(chan->cdr);
		break;
	}
	cw_channel_unlock(chan);
	return 0;
}

/*--- cw_waitfor_n_fd: Wait for x amount of time on a file descriptor to have input.  
      Please note that this is used only by DUNDI, at current date (Tue Jul 24, 2007).
*/

int cw_waitfor_n_fd(int *fds, int n, int *ms, int *exception)
{
	struct timeval start = { 0 , 0 };
	int res;
	int x, y;
	int winner = -1;
	int spoint;
	struct pollfd *pfds;
	
	pfds = alloca(sizeof(struct pollfd) * n);
	if (*ms > 0)
		start = cw_tvnow();
	y = 0;
	for (x = 0;  x < n;  x++)
	{
		if (fds[x] > -1)
        {
			pfds[y].fd = fds[x];
			pfds[y].events = POLLIN | POLLPRI;
			y++;
		}
	}
	res = poll(pfds, y, *ms);
	if (res < 0)
	{
		/* Simulate a timeout if we were interrupted */
		*ms = (errno != EINTR)  ?  -1  :  0;
		return -1;
	}
	spoint = 0;
	for (x = 0;  x < n;  x++)
	{
			if (fds[x] > -1)
			{
			if ((res = cw_fdisset(pfds, fds[x], y, &spoint)))
				{
				winner = fds[x];
				if (exception)
					*exception = (res & POLLPRI)?  -1  :  0;
			}
		}
	}
	if (*ms > 0)
	{
		*ms -= cw_tvdiff_ms(cw_tvnow(), start);
		if (*ms < 0)
			*ms = 0;
	}
	return winner;
}

/*--- cw_waitfor_nanfds: Wait for x amount of time on a file descriptor to have input.  */
struct cw_channel *cw_waitfor_nandfds(struct cw_channel **c, int n, int *fds, int nfds, 
	int *exception, int *outfd, int *ms)
{
	struct timeval start = { 0 , 0 };
	struct pollfd *pfds;
	int res;
	long rms;
	int x, y, max;
	int spoint;
	time_t now = 0;
	long whentohangup = 0, havewhen = 0, diff;
	struct cw_channel *winner = NULL;

	pfds = alloca(sizeof(struct pollfd) * (n * CW_MAX_FDS + nfds));

	if (outfd)
		*outfd = -99999;
	if (exception)
		*exception = 0;
	
	/* Perform any pending masquerades */
	for (x = 0;  x < n;  x++) {
		cw_channel_lock(c[x]);
		if (c[x]->whentohangup) {
			if (!havewhen)
				time(&now);
			diff = c[x]->whentohangup - now;
			if (!havewhen || (diff < whentohangup)) {
				havewhen++;
				whentohangup = diff;
			}
		}
		if (c[x]->masq) {
			if (cw_do_masquerade(c[x])) {
				cw_log(CW_LOG_WARNING, "Masquerade failed\n");
				*ms = -1;
				cw_channel_unlock(c[x]);
				return NULL;
			}
		}
		cw_channel_unlock(c[x]);
	}

	rms = *ms;
	
	if (havewhen)
	{
		if ((*ms < 0) || (whentohangup * 1000 < *ms))
			rms = whentohangup * 1000;
	}
	max = 0;
	for (x = 0;  x < n;  x++)
	{
		for (y = 0;  y < CW_MAX_FDS;  y++)
			{
			if (c[x]->fds[y] > -1)
				{
				pfds[max].fd = c[x]->fds[y];
				pfds[max].events = POLLIN | POLLPRI;
				pfds[max].revents = 0;
				max++;
			}
		}
		CHECK_BLOCKING(c[x]);
	}
	for (x = 0;  x < nfds;  x++)
	{
		if (fds[x] > -1)
			{
			pfds[max].fd = fds[x];
			pfds[max].events = POLLIN | POLLPRI;
			pfds[max].revents = 0;
			max++;
		}
	}
	if (*ms > 0) 
		start = cw_tvnow();
	
	if (sizeof(int) == 4)
	{
		do
			{
			int kbrms = rms;

			if (kbrms > 600000)
				kbrms = 600000;
			res = poll(pfds, max, kbrms);
			if (!res)
				rms -= kbrms;
		}
			while (!res  &&  (rms > 0));
	}
	else
	{
		res = poll(pfds, max, rms);
	}
	
	if (res < 0)
	{
		for (x = 0;  x < n;  x++) 
			cw_clear_flag(c[x], CW_FLAG_BLOCKING);
		/* Simulate a timeout if we were interrupted */
		if (errno != EINTR)
			{
			*ms = -1;
		}
			else
			{
			/* Just an interrupt */
#if 0
			*ms = 0;
#endif			
		}
		return NULL;
	}
	else
	{
		/* If no fds signalled, then timeout. So set ms = 0
		   since we may not have an exact timeout. */
		if (res == 0)
			*ms = 0;
	}

	if (havewhen)
		time(&now);
		
	spoint = 0;
	for (x = 0;  x < n;  x++)
	{
		cw_clear_flag(c[x], CW_FLAG_BLOCKING);
		if (havewhen && c[x]->whentohangup && (now > c[x]->whentohangup))
			{
			c[x]->_softhangup |= CW_SOFTHANGUP_TIMEOUT;
			if (!winner)
				winner = c[x];
		}
		for (y = 0;  y < CW_MAX_FDS;  y++)
			{
			if (c[x]->fds[y] > -1)
				{
				if ((res = cw_fdisset(pfds, c[x]->fds[y], max, &spoint)))
						{
					if (res & POLLPRI)
						cw_set_flag(c[x], CW_FLAG_EXCEPTION);
					else
						cw_clear_flag(c[x], CW_FLAG_EXCEPTION);
					c[x]->fdno = y;
					winner = c[x];
				}
			}
		}
	}
	for (x = 0;  x < nfds;  x++)
	{
		if (fds[x] > -1)
			{
			if ((res = cw_fdisset(pfds, fds[x], max, &spoint)))
				{
				if (outfd)
					*outfd = fds[x];
				if (exception)
						{	
					if (res & POLLPRI) 
						*exception = -1;
					else
						*exception = 0;
				}
				winner = NULL;
			}
		}	
	}
	if (*ms > 0)
        {
		*ms -= cw_tvdiff_ms(cw_tvnow(), start);
		if (*ms < 0)
			*ms = 0;
	}
	return winner;
}

struct cw_channel *cw_waitfor_n(struct cw_channel **c, int n, int *ms)
{
	return cw_waitfor_nandfds(c, n, NULL, 0, NULL, NULL, ms);
}

int cw_waitfor(struct cw_channel *c, int ms)
{
	int oldms = ms;

	cw_waitfor_n(&c, 1, &ms);
	if (ms < 0)
	{
		if (oldms < 0)
			return 0;
		else
			return -1;
	}
	return ms;
}

/** Wait for a digit discarding.
 * @param c The channel to wait on.
 * @param ms Maximum time to wait, in milliseconds.
 * @return Whatever cw_waitfordigit_full() returns.
 *
 * This will simply call cw_waitfordigit_full() with the proper arguments to
 * get the first DTMF digit on @c c, discarding all sound and video frames.
 */
int cw_waitfordigit(struct cw_channel *c, int ms)
{
	return cw_waitfordigit_full(c, ms, -1, -1);
}

int cw_waitfordigit_full(struct cw_channel *c, int ms, int audiofd, int cmdfd)
{
	struct cw_frame *f;
	struct cw_channel *rchan;
	int outfd;
	int res;

	/* Stop if we're a zombie or need a soft hangup */
	if (cw_test_flag(c, CW_FLAG_ZOMBIE) || cw_check_hangup(c)) 
		return -1;

	/* Wait for a digit, no more than ms milliseconds total. */
	while (ms) {
		errno = 0;
		rchan = cw_waitfor_nandfds(&c, 1, &cmdfd, (cmdfd > -1) ? 1 : 0, NULL, &outfd, &ms);
		if ((!rchan) && (outfd < 0) && (ms)) {
			if (errno == 0 || errno == EINTR)
				continue;
			cw_log(CW_LOG_WARNING, "Wait failed (%s)\n", strerror(errno));
			return -1;
		} else if (outfd > -1)
			{
			/* The FD we were watching has something waiting */
			return 1;
		} else if (rchan) {
			if ((f = cw_read(c)) == 0)
				return -1;
			switch (f->frametype) {
				case CW_FRAME_DTMF:
					res = f->subclass;
					cw_fr_free(f);
					return res;
				case CW_FRAME_CONTROL:
					switch(f->subclass) {
						case CW_CONTROL_HANGUP:
							cw_fr_free(f);
							return -1;
						case CW_CONTROL_RINGING:
						case CW_CONTROL_ANSWER:
							/* Unimportant */
							break;
						default:
							cw_log(CW_LOG_WARNING, "Unexpected control subclass '%d'\n", f->subclass);
					}
				case CW_FRAME_VOICE:
					/* Write audio if appropriate */
					if (audiofd > -1)
						write(audiofd, f->data, f->datalen);
			}
			/* Ignore */
			cw_fr_free(f);
		}
	}
	return 0; /* Time is up */
}

struct cw_frame *cw_read(struct cw_channel *chan)
{
	struct cw_frame *f = NULL;
	int prestate;
	
	cw_channel_lock(chan);
	if (chan->masq) {
		if (cw_do_masquerade(chan)) {
			cw_log(CW_LOG_WARNING, "Failed to perform masquerade\n");
			f = NULL;
		} else {
			f =  &cw_null_frame;
		}
			cw_channel_unlock(chan);
		return f;
	}

	/* Stop if we're a zombie or need a soft hangup */
	if (cw_test_flag(chan, CW_FLAG_ZOMBIE)  ||  cw_check_hangup(chan)) {
		cw_channel_unlock(chan);
		cw_generator_deactivate(&chan->generator);
		return NULL;
	}
	prestate = chan->_state;

	if (!cw_test_flag(chan, CW_FLAG_DEFER_DTMF) && !cw_strlen_zero(chan->dtmfq)) {
		/* We have DTMF that has been deferred.  Return it now */
			cw_fr_init_ex(&chan->dtmff, CW_FRAME_DTMF, chan->dtmfq[0]);
		/* Drop first digit */
		memmove(chan->dtmfq, chan->dtmfq + 1, sizeof(chan->dtmfq) - 1);
		cw_channel_unlock(chan);
		return &chan->dtmff;
	}

	/* Read and ignore anything on the alertpipe, but read only
	   one sizeof(blah) per frame that we send from it */
	if (chan->alertpipe[0] > -1) {
		char blah;
		read(chan->alertpipe[0], &blah, sizeof(blah));
	}

	/* Check for pending read queue */
	if (chan->readq) {
		f = chan->readq;
		chan->readq = f->next;
	} else {
		chan->blocker = pthread_self();
		if (cw_test_flag(chan, CW_FLAG_EXCEPTION)) {
			if (chan->tech->exception) {
				f = chan->tech->exception(chan);
			} else {
				cw_log(CW_LOG_WARNING, "Exception flag set on '%s', but no exception handler\n", chan->name);
				f = &cw_null_frame;
			}
			/* Clear the exception flag */
			cw_clear_flag(chan, CW_FLAG_EXCEPTION);
		} else {
			if (chan->tech->read)
				f = chan->tech->read(chan);
			else
				cw_log(CW_LOG_WARNING, "No read routine on channel %s\n", chan->name);
		}
	}

	if (f) {
		/* If the channel driver returned more than one frame, stuff the excess
		   into the readq for the next cw_read call */
		if (f->next) {
			/* We can safely assume the read queue is empty, or we wouldn't be here */
			chan->readq = f->next;
			f->next = NULL;
		}

		if ((f->frametype == CW_FRAME_VOICE)) {
			if (!(f->subclass & chan->nativeformats)) {
				/* This frame can't be from the current native formats -- drop it on the floor */
				cw_log(CW_LOG_NOTICE, "Dropping incompatible voice frame on %s of format %s since our native format has changed to %s\n", chan->name, cw_getformatname(f->subclass), cw_getformatname(chan->nativeformats));
				cw_fr_free(f);
				f = &cw_null_frame;
			} else {
				if (chan->spies) {
					struct cw_channel_spy *spying;

					for (spying = chan->spies;  spying;  spying = spying->next)
						cw_spy_queue_frame(spying, f, 0);
				}
				if (chan->monitor && chan->monitor->read_stream) {
#ifndef MONITOR_CONSTANT_DELAY
					int jump = chan->outsmpl - chan->insmpl - 2 * f->samples;

					if (jump >= 0) {
						if (cw_seekstream(chan->monitor->read_stream, jump + f->samples, SEEK_FORCECUR) == -1)
							cw_log(CW_LOG_WARNING, "Failed to perform seek in monitoring read stream, synchronization between the files may be broken\n");
						chan->insmpl += jump + 2 * f->samples;
					} else {
						chan->insmpl+= f->samples;
					}
#else
					int jump = chan->outsmpl - chan->insmpl;

					if (jump - MONITOR_DELAY >= 0) {
						if (cw_seekstream(chan->monitor->read_stream, jump - f->samples, SEEK_FORCECUR) == -1)
							cw_log(CW_LOG_WARNING, "Failed to perform seek in monitoring read stream, synchronization between the files may be broken\n");
						chan->insmpl += jump;
					} else {
						chan->insmpl += f->samples;
					}
#endif
					if (cw_writestream(chan->monitor->read_stream, f) < 0)
						cw_log(CW_LOG_WARNING, "Failed to write data to channel monitor read stream\n");
				}
			
				/* FIXME: If we have an RX gain we should apply it now using cw_frame_adjust_volume(). */

				if (chan->readtrans) {
					if ((f = cw_translate(chan->readtrans, f, 1)) == NULL)
						f = &cw_null_frame;
				}
			}
		} else if (f->frametype == CW_FRAME_CONTROL && f->subclass == CW_CONTROL_HANGUP) {
			cw_fr_free(f);
			f = NULL;
		}
	}

	if (!f) {
		/* Make sure we always return NULL in the future */
		chan->_softhangup |= CW_SOFTHANGUP_DEV;
		/* End the CDR if appropriate */
		if (chan->cdr)
			cw_cdr_end(chan->cdr);
	} else if (cw_test_flag(chan, CW_FLAG_DEFER_DTMF) && f->frametype == CW_FRAME_DTMF) {
		if (strlen(chan->dtmfq) < sizeof(chan->dtmfq) - 2)
			chan->dtmfq[strlen(chan->dtmfq)] = (char)f->subclass;
		else
			cw_log(CW_LOG_WARNING, "Dropping deferred DTMF digits on %s\n", chan->name);
		f = &cw_null_frame;
	} else if ((f->frametype == CW_FRAME_CONTROL) && (f->subclass == CW_CONTROL_ANSWER)) {
		if (prestate == CW_STATE_UP) {
			cw_log(CW_LOG_DEBUG, "Dropping duplicate answer!\n");
			f = &cw_null_frame;
		}
		/* Answer the CDR */
		cw_setstate(chan, CW_STATE_UP);
		cw_cdr_answer(chan->cdr);
	}

	if (cw_test_flag(chan, CW_FLAG_DEBUG_IN))
		cw_frame_dump(chan->name, f, "<<");

	cw_channel_unlock(chan);

	if (f == NULL)
		cw_generator_deactivate(&chan->generator);

	return f;
}

int cw_indicate(struct cw_channel *chan, int condition)
{
	int res = -1;

	/* Stop if we're a zombie or need a soft hangup */
	if (cw_test_flag(chan, CW_FLAG_ZOMBIE) || cw_check_hangup(chan))
		return -1;

	cw_channel_lock(chan);

	if (chan->tech->indicate)
		res = chan->tech->indicate(chan, condition);

	cw_channel_unlock(chan);

	if (!chan->tech->indicate  ||  res) {
		/*
		 * Device does not support (that) indication, lets fake
		 * it by doing our own tone generation. (PM2002)
		 */
		if (condition >= 0) {
			const struct tone_zone_sound *ts = NULL;
			switch (condition) {
				case CW_CONTROL_RINGING:
					ts = cw_get_indication_tone(chan->zone, "ring");
					break;
				case CW_CONTROL_BUSY:
					ts = cw_get_indication_tone(chan->zone, "busy");
					break;
				case CW_CONTROL_CONGESTION:
					ts = cw_get_indication_tone(chan->zone, "congestion");
					break;
			}
			if (ts  &&  ts->data[0]) {
				cw_log(CW_LOG_DEBUG, "Driver for channel '%s' does not support indication %d, emulating it\n", chan->name, condition);
				cw_playtones_start(chan,0,ts->data, 1);
				res = 0;
			} else if (condition == CW_CONTROL_PROGRESS) {
				/* cw_playtones_stop(chan); */
			} else if (condition == CW_CONTROL_PROCEEDING) {
				/* Do nothing, really */
			} else if (condition == CW_CONTROL_HOLD) {
				/* Do nothing.... */
			} else if (condition == CW_CONTROL_UNHOLD) {
				/* Do nothing.... */
			} else if (condition == CW_CONTROL_VIDUPDATE) {
				/* Do nothing.... */
			} else {
				/* not handled */
				cw_log(CW_LOG_WARNING, "Unable to handle indication %d for '%s'\n", condition, chan->name);
				res = -1;
			}
		} else {
			cw_playtones_stop(chan);
		}
	}

	return res;
}

int cw_recvchar(struct cw_channel *chan, int timeout)
{
	int c;
	char *buf = cw_recvtext(chan, timeout);

	if (buf == NULL)
		return -1;	/* error or timeout */
	c = *(unsigned char *) buf;
	free(buf);
	return c;
}

char *cw_recvtext(struct cw_channel *chan, int timeout)
{
	int res, done = 0;
	char *buf = NULL;
	
	while (!done) {
		struct cw_frame *f;

		if (cw_check_hangup(chan))
			break;
		if ((res = cw_waitfor(chan, timeout)) <= 0) /* timeout or error */
			break;
		timeout = res;	/* update timeout */
		if ((f = cw_read(chan)) == NULL)
			break; /* no frame */
		if (f->frametype == CW_FRAME_CONTROL  &&  f->subclass == CW_CONTROL_HANGUP) {
			done = 1;	/* force a break */
		} else if (f->frametype == CW_FRAME_TEXT) {
			/* what we want */
			buf = strndup((char *) f->data, f->datalen);	/* dup and break */
			done = 1;
		}
		cw_fr_free(f);
	}
	return buf;
}

int cw_sendtext(struct cw_channel *chan, const char *text)
{
	struct cw_frame f = {
		.frametype = CW_FRAME_TEXT,
		.data = (char *)text,
		.datalen = strlen(text),
	};

	return cw_queue_frame(chan, &f);
}

static int do_senddigit(struct cw_channel *chan, char digit)
{
	int res = -1;

	if (chan->tech->send_digit)
		res = chan->tech->send_digit(chan, digit);

	if (!chan->tech->send_digit || res < 0) {
		/*
		 * Device does not support DTMF tones, lets fake
		 * it by doing our own generation. (PM2002)
		 */
		static const char* dtmf_tones[] = {
			"!0/100,!0/100",	/* silence */
			"!941+1336/100,!0/100",	/* 0 */
			"!697+1209/100,!0/100",	/* 1 */
			"!697+1336/100,!0/100",	/* 2 */
			"!697+1477/100,!0/100",	/* 3 */
			"!770+1209/100,!0/100",	/* 4 */
			"!770+1336/100,!0/100",	/* 5 */
			"!770+1477/100,!0/100",	/* 6 */
			"!852+1209/100,!0/100",	/* 7 */
			"!852+1336/100,!0/100",	/* 8 */
			"!852+1477/100,!0/100",	/* 9 */
			"!697+1633/100,!0/100",	/* A */
			"!770+1633/100,!0/100",	/* B */
			"!852+1633/100,!0/100",	/* C */
			"!941+1633/100,!0/100",	/* D */
			"!941+1209/100,!0/100",	/* * */
			"!941+1477/100,!0/100"	/* # */
		};
		if (res == -2)
			cw_playtones_start(chan, 0, dtmf_tones[0], 0);
		else if (digit >= '0'  &&  digit <='9')
			cw_playtones_start(chan, 0, dtmf_tones[1 + digit - '0'], 0);
		else if (digit >= 'A' && digit <= 'D')
			cw_playtones_start(chan, 0, dtmf_tones[1 + digit - 'A' + 10], 0);
		else if (digit == '*')
			cw_playtones_start(chan, 0, dtmf_tones[15], 0);
		else if (digit == '#')
			cw_playtones_start(chan, 0, dtmf_tones[16], 0);
		else {
			/* not handled */
			cw_log(CW_LOG_DEBUG, "Unable to generate DTMF tone '%c' for '%s'\n", digit, chan->name);
		}
	}
	return 0;
}

int cw_senddigit(struct cw_channel *chan, char digit)
{
	return do_senddigit(chan, digit);
}

int cw_prod(struct cw_channel *chan)
{
	struct cw_frame a = { CW_FRAME_VOICE };
	struct cw_frame *f;
	uint8_t nothing[128];

	/* Send an empty audio frame to get things moving */
	if (chan->_state != CW_STATE_UP) {
		cw_log(CW_LOG_DEBUG, "Prodding channel '%s'\n", chan->name);
		a.subclass = chan->rawwriteformat;
		a.data = nothing + CW_FRIENDLY_OFFSET;
		f = &a;
		if (cw_write(chan, &f))
			cw_log(CW_LOG_WARNING, "Prodding channel '%s' failed\n", chan->name);
	}
	return 0;
}

int cw_write(struct cw_channel *chan, struct cw_frame **fr_p)
{
	struct cw_frame *fr;
	struct cw_frame *f = NULL;
	int res = -1;

	/* Stop if we're a zombie or need a soft hangup */
	cw_channel_lock(chan);
	if (cw_test_flag(chan, CW_FLAG_ZOMBIE) || cw_check_hangup(chan)) {
		cw_channel_unlock(chan);
		return -1;
	}
	/* Handle any pending masquerades */
	if (chan->masq) {
		*fr_p = cw_frisolate(*fr_p);
		if (cw_do_masquerade(chan)) {
			cw_log(CW_LOG_WARNING, "Failed to perform masquerade\n");
			cw_channel_unlock(chan);
			return -1;
		}
	}

	if (chan->masqr) {
		cw_channel_unlock(chan);
		return 0;
	}

	/* A write by a non channel generator thread may or may not
	 * deactivate a running channel generator depending on
	 * whether the CW_FLAG_WRITE_INT is set or not for the
	 * channel. If CW_FLAG_WRITE_INT is set, channel generator
	 * is deactivated. Otherwise, the write is simply ignored. */
	if (!cw_generator_is_self(chan)  &&  cw_generator_is_active(chan)) {
		/* We weren't called by the generator
		 * thread and channel generator is active */
		if (cw_test_flag(chan, CW_FLAG_WRITE_INT)) {
			/* Deactivate generator */

			/** unlock & lock added - testing if this caused crashes*/
			cw_channel_unlock(chan);
			if (option_debug)
				cw_log(CW_LOG_DEBUG, "trying deactivate generator with unlock/lock channel (cw_write function)\n");
			cw_generator_deactivate(&chan->generator);
			cw_channel_lock(chan);
		} else {
			/* Write doesn't interrupt generator.
			 * Write gets ignored instead */
			cw_channel_unlock(chan);
			return 0;
		}

	}

	fr = *fr_p;

	if (cw_test_flag(chan, CW_FLAG_DEBUG_OUT))
		cw_frame_dump(chan->name, fr, ">>");
#if 0
	/* CMANTUNES: Do we really need this CHECK_BLOCKING thing in here?
	 * I no longer think we do because we can now be reading and writing
	 * at the same time. Writing is no longer tied to reading as before */
	CHECK_BLOCKING(chan);
#endif
	switch (fr->frametype) {
		/* FIXME: for CW_FRAME_CONTROL, CW_CONTROL_OPTION, CW_OPTION_{TX,RX}GAIN we should
		 * record the requested gain in the struct cw_channel, switch to slinear in the
		 * appropriate direction updating any translators.
		 */

		case CW_FRAME_DTMF:
			cw_clear_flag(chan, CW_FLAG_BLOCKING);
			cw_channel_unlock(chan);
			cw_log(CW_LOG_DTMF, "%s : %c\n", chan->name, fr->subclass);
			res = do_senddigit(chan, (char)fr->subclass);
			cw_channel_lock(chan);
			CHECK_BLOCKING(chan);
			break;

		default:
			if (!chan->tech->write)
				break;

			res = 0;
			f = fr;

			/* FIXME: If we have a TX gain we should apply it here using cw_frame_adjust_volume(). But that
			 * requires slinear. We may or may not have that and the translation path we have may or may
			 * not be usable given the rescaled slinear.
			 */

			if (chan->writetrans && fr->frametype == CW_FRAME_VOICE && !(f = cw_translate(chan->writetrans, fr, 0)))
				break;

			/* CMANTUNES: Instead of writing directly here,
			 * we could insert frame into output queue and
			 * let the channel driver use a writer thread
			 * to actually write the stuff, for example. */

			if (f->frametype == CW_FRAME_VOICE) {
				if (chan->spies) {
					struct cw_channel_spy *spying;

					for (spying = chan->spies;  spying;  spying = spying->next)
						cw_spy_queue_frame(spying, f, 1);
				}

				if (chan->monitor && chan->monitor->write_stream) {
#ifndef MONITOR_CONSTANT_DELAY
					int jump = chan->insmpl - chan->outsmpl - 2 * f->samples;
					if (jump >= 0) {
						if (cw_seekstream(chan->monitor->write_stream, jump + f->samples, SEEK_FORCECUR) == -1)
							cw_log(CW_LOG_WARNING, "Failed to perform seek in monitoring write stream, synchronization between the files may be broken\n");
						chan->outsmpl += jump + 2 * f->samples;
					} else
						chan->outsmpl += f->samples;
#else
					int jump = chan->insmpl - chan->outsmpl;
					if (jump - MONITOR_DELAY >= 0) {
						if (cw_seekstream(chan->monitor->write_stream, jump - f->samples, SEEK_FORCECUR) == -1)
							cw_log(CW_LOG_WARNING, "Failed to perform seek in monitoring write stream, synchronization between the files may be broken\n");
						chan->outsmpl += jump;
					} else
						chan->outsmpl += f->samples;
#endif
					if (cw_writestream(chan->monitor->write_stream, f) < 0)
						cw_log(CW_LOG_WARNING, "Failed to write data to channel monitor write stream\n");
				}
			}

			res = chan->tech->write(chan, f);
			if (f != fr)
				cw_fr_free(f);
			break;
	}

	cw_clear_flag(chan, CW_FLAG_BLOCKING);

	/* Consider a write failure to force a soft hangup */
	if (res < 0)
		chan->_softhangup |= CW_SOFTHANGUP_DEV;

	cw_channel_unlock(chan);
	return res;
}

static int set_format(
                        struct cw_channel *chan, 
                        int fmt, 
                        int *rawformat, 
                        int *format,
				struct cw_trans_pvt **trans, 
                        const int direction
                     )
{
	int native;
	int res;
	
	native = chan->nativeformats;
	/* Find a translation path from the native format to one of the desired formats */
	if (!direction)
		/* reading */
		res = cw_translator_best_choice(&fmt, &native);
	else
		/* writing */
		res = cw_translator_best_choice(&native, &fmt);

	if (res < 0) {
		cw_log(CW_LOG_WARNING, "Unable to find a codec translation path from %s to %s\n",
			cw_getformatname(native), cw_getformatname(fmt));
		return -1;
	}
	
	/* Now we have a good choice for both. */
	cw_channel_lock(chan);

	if ((*rawformat == native) && (*format == fmt) && ((*rawformat == *format) || (*trans))) {
		/* the channel is already in these formats, so nothing to do */
		cw_channel_unlock(chan);
		return 0;
	}

	*rawformat = native;
	/* User perspective is fmt */
	*format = fmt;
	/* Free any read translation we have right now */
	if (*trans)
		cw_translator_free_path(*trans);
	/* Build a translation path from the raw format to the desired format */
	if (!direction)
		/* reading */
		*trans = cw_translator_build_path(*format, *rawformat);
	else
		/* writing */
		*trans = cw_translator_build_path(*rawformat, *format);

	cw_channel_unlock(chan);

	if (option_debug) {
		cw_log(CW_LOG_DEBUG, "Set channel %s to %s format %s\n", chan->name,
			direction ? "write" : "read", cw_getformatname(fmt));
	}

	return 0;
}

int cw_set_read_format(struct cw_channel *chan, int fmt)
{
	return set_format(chan, fmt, &chan->rawreadformat, &chan->readformat,
			  &chan->readtrans, 0);
}

int cw_set_write_format(struct cw_channel *chan, int fmt)
{
	return set_format(chan, fmt, &chan->rawwriteformat, &chan->writeformat,
			  &chan->writetrans, 1);
}

struct cw_channel *__cw_request_and_dial(const char *type, int format, void *data, int timeout, int *outstate, const char *cid_num, const char *cid_name, struct outgoing_helper *oh)
{
	int state = 0;
	int cause = 0;
	struct cw_channel *chan;
	struct cw_frame *f;
	int res = 0;
	
	if ((chan = cw_request(type, format, data, &cause))) {
		if (oh && oh->vars)
			cw_var_copy(&chan->vars, oh->vars);

		cw_set_callerid(chan, cid_num, cid_name, cid_num);

		if (!cw_call(chan, data)) {
			while (timeout  &&  (chan->_state != CW_STATE_UP)) {
				if ((res = cw_waitfor(chan, timeout)) < 0) {
					/* Something not cool, or timed out */
					break;
				}
				/* If done, break out */
				if (!res)
					break;
				if (timeout > -1)
					timeout = res;
				if ((f = cw_read(chan)) == 0) {
					state = CW_CONTROL_HANGUP;
					res = 0;
					break;
				}
				if (f->frametype == CW_FRAME_CONTROL) {
					if (f->subclass == CW_CONTROL_RINGING) {
						state = CW_CONTROL_RINGING;
					} else if ((f->subclass == CW_CONTROL_BUSY)  ||  (f->subclass == CW_CONTROL_CONGESTION)) {
						state = f->subclass;
						cw_fr_free(f);
						break;
					} else if (f->subclass == CW_CONTROL_ANSWER) {
						state = f->subclass;
						cw_fr_free(f);
						break;
					} else if (f->subclass == CW_CONTROL_PROGRESS) {
						/* Ignore */
					} else if (f->subclass == -1) {
						/* Ignore -- just stopping indications */
					} else {
						cw_log(CW_LOG_NOTICE, "Don't know what to do with control frame %d\n", f->subclass);
					}
				}
				cw_fr_free(f);
			}
		} else
			cw_log(CW_LOG_NOTICE, "Unable to call channel %s/%s\n", type, (char *)data);
	} else {
		cw_log(CW_LOG_NOTICE, "Unable to request channel %s/%s\n", type, (char *)data);
		switch(cause) {
			case CW_CAUSE_BUSY:
				state = CW_CONTROL_BUSY;
				break;
			case CW_CAUSE_CONGESTION:
				state = CW_CONTROL_CONGESTION;
				break;
		}
	}
	if (chan) {
		/* Final fixups */
		if (oh) {
			if (oh->context && *oh->context)
				cw_copy_string(chan->context, oh->context, sizeof(chan->context));
			if (oh->exten && *oh->exten)
				cw_copy_string(chan->exten, oh->exten, sizeof(chan->exten));
			if (oh->priority)	
				chan->priority = oh->priority;
		}
		if (chan->_state == CW_STATE_UP) 
			state = CW_CONTROL_ANSWER;
	}
	if (outstate)
		*outstate = state;
	if (chan  &&  res <= 0) {
		if (!chan->cdr)
			cw_cdr_alloc(chan);

		if (chan->cdr) {
			char tmp[256];
			snprintf(tmp, 256, "%s/%s", type, (char *)data);
			cw_cdr_setapp(chan->cdr,"Dial",tmp);
			cw_cdr_update(chan);
			cw_cdr_start(chan->cdr);
			cw_cdr_end(chan->cdr);
			/* If the cause wasn't handled properly */
			if (cw_cdr_disposition(chan->cdr,chan->hangupcause))
				cw_cdr_failed(chan->cdr);
		} else {
			cw_log(CW_LOG_WARNING, "Unable to create Call Detail Record\n");
		}
		cw_hangup(chan);
		chan = NULL;
	}

	return chan;
}

struct cw_channel *cw_request_and_dial(const char *type, int format, void *data, int timeout, int *outstate, const char *cidnum, const char *cidname)
{
	return __cw_request_and_dial(type, format, data, timeout, outstate, cidnum, cidname, NULL);
}

struct cw_channel *cw_request(const char *type, int format, void *data, int *cause)
{
	struct chanlist *chan;
	struct cw_channel *c = NULL;
	int capabilities;
	int fmt;
	int res;
	int foo;

	if (!cause)
		cause = &foo;
	*cause = CW_CAUSE_NOTDEFINED;

	cw_mutex_lock(&chlock);

	chan = backends;
	while (chan) {
		if (!strcasecmp(type, chan->tech->type)) {
			capabilities = chan->tech->capabilities;
			fmt = format;
			res = cw_translator_best_choice(&fmt, &capabilities);
			if (res < 0) {
				cw_log(CW_LOG_WARNING, "No translator path exists for channel type %s (native %d) to %d\n", type, chan->tech->capabilities, format);
				cw_mutex_unlock(&chlock);
				return NULL;
			}
			cw_mutex_unlock(&chlock);
			if (chan->tech->requester)
				c = chan->tech->requester(type, capabilities, data, cause);
			if (c) {
				if (c->_state == CW_STATE_DOWN) {
					cw_manager_event(CW_EVENT_FLAG_CALL, "Newchannel",
						7,
						cw_msg_tuple("Channel",      "%s", c->name),
						cw_msg_tuple("State",        "%s", cw_state2str(c->_state)),
						cw_msg_tuple("CallerID",     "%s", (c->cid.cid_num ? c->cid.cid_num : "<unknown>")),
						cw_msg_tuple("CallerIDName", "%s", (c->cid.cid_name ? c->cid.cid_name : "<unknown>")),
						cw_msg_tuple("Uniqueid",     "%s", c->uniqueid),
						cw_msg_tuple("Type",         "%s", type),
						cw_msg_tuple("Dialstring",   "%s", (char *)data)
					);
				}
			}
			return c;
		}
		chan = chan->next;
	}

	if (!chan) {
		cw_log(CW_LOG_WARNING, "No channel type registered for '%s'\n", type);
		*cause = CW_CAUSE_NOSUCHDRIVER;
	}

	cw_mutex_unlock(&chlock);

	return c;
}

int cw_call(struct cw_channel *chan, const char *addr)
{
	int res = -1;

	/* Stop if we're a zombie or need a soft hangup */
	cw_channel_lock(chan);
	if (!cw_test_flag(chan, CW_FLAG_ZOMBIE)  &&  !cw_check_hangup(chan)) {
		if (chan->tech->call)
			res = chan->tech->call(chan, addr);
	}
	cw_channel_unlock(chan);
	return res;
}

/*--- cw_transfer: Transfer a call to dest, if the channel supports transfer */
/*	called by app_transfer or the manager interface */
int cw_transfer(struct cw_channel *chan, const char *dest)
{
	int res = -1;

	/* Stop if we're a zombie or need a soft hangup */
	cw_channel_lock(chan);
	if (!cw_test_flag(chan, CW_FLAG_ZOMBIE) && !cw_check_hangup(chan)) {
		if (chan->tech->transfer) {
			res = chan->tech->transfer(chan, dest);
			if (!res)
				res = 1;
		} else {
			res = 0;
		}
	}
	cw_channel_unlock(chan);
	return res;
}

int cw_readstring(struct cw_channel *c, char *s, int len, int timeout, int ftimeout, const char *enders)
{
	int pos = 0;
	int to = ftimeout;
	int d;
	int ret = -1;

	/* XXX Merge with full version? XXX */
	/* Stop if we're a zombie or need a soft hangup */
	if (len && !cw_test_flag(c, CW_FLAG_ZOMBIE) && !cw_check_hangup(c)) {
		for (;;) {
			if (c->stream) {
				d = cw_waitstream(c, CW_DIGIT_ANY);
				cw_stopstream(c);
				usleep(1000);
				if (!d)
					d = cw_waitfordigit(c, to);
			} else {
				d = cw_waitfordigit(c, to);
			}
			if (d < 0)
				break;
			if (d == 0) {
				s[pos]='\0';
				ret = 1;
				break;
			}
			if (!strchr(enders, d))
				s[pos++] = (char)d;
			if (strchr(enders, d)  ||  (pos >= len)) {
				s[pos]='\0';
				ret = 0;
				break;
			}
			to = timeout;
		}
	}

	return ret;
}

int cw_readstring_full(struct cw_channel *c, char *s, int len, int timeout, int ftimeout, const char *enders, int audiofd, int ctrlfd)
{
	int pos = 0;
	int to = ftimeout;
	int d;
	int ret = -1;

	/* Stop if we're a zombie or need a soft hangup */
	if (!len && !cw_test_flag(c, CW_FLAG_ZOMBIE) && !cw_check_hangup(c)) {
		for (;;) {
			if (c->stream) {
				d = cw_waitstream_full(c, CW_DIGIT_ANY, audiofd, ctrlfd);
				cw_stopstream(c);
				usleep(1000);
				if (!d)
					d = cw_waitfordigit_full(c, to, audiofd, ctrlfd);
			} else {
				d = cw_waitfordigit_full(c, to, audiofd, ctrlfd);
			}
			if (d < 0)
				break;
			if (d == 0) {
				s[pos]='\0';
				ret = 1;
				break;
			}
			if (d == 1) {
				s[pos]='\0';
				ret = 2;
				break;
			}
			if (!strchr(enders, d))
				s[pos++] = (char)d;
			if (strchr(enders, d)  ||  (pos >= len)) {
				s[pos]='\0';
				ret = 0;
				break;
			}
			to = timeout;
		}
	}

	return ret;
}

int cw_channel_sendhtml(struct cw_channel *chan, int subclass, const char *data, int datalen)
{
	struct cw_frame f = {
		.frametype = CW_FRAME_HTML,
		.subclass = subclass,
		.data = (char *)data,
		.datalen = datalen,
	};

	return cw_queue_frame(chan, &f);
}

int cw_channel_sendurl(struct cw_channel *chan, const char *url)
{
	return cw_channel_sendhtml(chan, CW_HTML_URL, url, strlen(url) + 1);
}

int cw_channel_make_compatible(struct cw_channel *chan, struct cw_channel *peer)
{
	int src;
	int dst;

	/* Set up translation from the chan to the peer */
	src = chan->nativeformats;
	dst = peer->nativeformats;
	if (cw_translator_best_choice(&dst, &src) < 0) {
		cw_log(CW_LOG_WARNING, "No path to translate from %s(%d) to %s(%d)\n", chan->name, src, peer->name, dst);
		return -1;
	}

	/* if the best path is not 'pass through', then
	   transcoding is needed; if desired, force transcode path
	   to use SLINEAR between channels */
	if ((src != dst) && option_transcode_slin)
		dst = CW_FORMAT_SLINEAR;
	if (cw_set_read_format(chan, src) < 0) {
		cw_log(CW_LOG_WARNING, "Unable to set read format on channel %s to %d\n", chan->name, dst);
		return -1;
	}
	if (cw_set_write_format(peer, src) < 0) {
		cw_log(CW_LOG_WARNING, "Unable to set write format on channel %s to %d\n", peer->name, dst);
		return -1;
	}

	/* Set up translation from the peer to the chan */
	src = peer->nativeformats;
	dst = chan->nativeformats;
	if (cw_translator_best_choice(&dst, &src) < 0) {
		cw_log(CW_LOG_WARNING, "No path to translate from %s(%d) to %s(%d)\n", peer->name, src, chan->name, dst);
		return -1;
	}
	/* if the best path is not 'pass through', then
	   transcoding is needed; if desired, force transcode path
	   to use SLINEAR between channels */
	if ((src != dst) && option_transcode_slin)
		dst = CW_FORMAT_SLINEAR;
	if (cw_set_read_format(peer, dst) < 0) {
		cw_log(CW_LOG_WARNING, "Unable to set read format on channel %s to %d\n", peer->name, dst);
		return -1;
	}
	if (cw_set_write_format(chan, dst) < 0) {
		cw_log(CW_LOG_WARNING, "Unable to set write format on channel %s to %d\n", chan->name, dst);
		return -1;
	}
	return 0;
}

int cw_channel_masquerade(struct cw_channel *original, struct cw_channel *oldchan)
{
    int res = -1;

    if (original == oldchan) {
		cw_log(CW_LOG_WARNING, "Can't masquerade channel '%s' into itself!\n", original->name);
		return -1;
    }

    cw_channel_lock(original);
    while (cw_channel_trylock(oldchan)) {
		cw_channel_unlock(original);
		usleep(1);
		cw_channel_lock(original);
    }
    cw_log(CW_LOG_DEBUG, "Planning to masquerade channel %s into the structure of %s\n",
		oldchan->name, original->name);

    if (original->masq) {
	cw_log(CW_LOG_WARNING, "%s is already going to masquerade as %s\n", 
		original->masq->name, original->name);
    } else if (oldchan->masqr) {
		cw_log(CW_LOG_WARNING, "%s is already going to masquerade as %s\n", 
			oldchan->name, oldchan->masqr->name);
    } else {
		original->masq = oldchan;
		oldchan->masqr = original;
		cw_queue_frame(original, &cw_null_frame);
		cw_queue_frame(oldchan, &cw_null_frame);
		cw_log(CW_LOG_DEBUG, "Done planning to masquerade channel %s into the structure of %s\n", oldchan->name, original->name);
		res = 0;
    }
    cw_channel_unlock(oldchan);
    cw_channel_unlock(original);
    return res;
}


void cw_change_name(struct cw_channel *chan, const char *fmt, ...)
{
	char oldname[sizeof(chan->name)];
	va_list ap;
	int is_registered;

	cw_channel_lock(chan);

	if ((is_registered = (chan->reg_entry != NULL)))
		cw_registry_del(&channel_registry, chan->reg_entry);

	cw_copy_string(oldname, chan->name, sizeof(chan->name));
	va_start(ap, fmt);
	vsnprintf((char *)chan->name, sizeof(chan->name), fmt, ap);
	va_end(ap);
	if (is_registered)
		chan->reg_entry = cw_registry_add(&channel_registry, cw_hash_string(0, chan->name), &chan->obj);

	cw_channel_unlock(chan);

	cw_manager_event(CW_EVENT_FLAG_CALL, "Rename",
		3,
		cw_msg_tuple("Oldname",  "%s", oldname),
		cw_msg_tuple("Newname",  "%s", chan->name),
		cw_msg_tuple("Uniqueid", "%s", chan->uniqueid)
	);
}


/* Clone channel variables from 'clone' channel into 'original' channel
   All variables except those related to app_groupcount are cloned
   Assumes locks will be in place on both channels when called.
*/
   
static int clone_variables_one(struct cw_object *obj, void *data)
{
	struct cw_var_t *var = container_of(obj, struct cw_var_t, obj);
	struct cw_registry *reg = data;

	cw_registry_add(reg, var->hash, &var->obj);

	return 0;
}

static void clone_variables(struct cw_channel *original, struct cw_channel *oldchan)
{
	struct cw_registry tmp;

	/* Append variables from oldchan channel into original channel by pushing
	 * the variables from the original channel onto the oldchan and flipping
	 * the registries. This ensures that the ordering of variables remains
	 * correct. This is needed because gosub, proc etc assume a push down
	 * stack :-(
	 */
	cw_registry_iterate_rev(&original->vars, clone_variables_one, &oldchan->vars);

	/* This is highly dangerous if anything else could touch either
	 * registry in parallel!
	 */
	tmp = oldchan->vars;
	oldchan->vars = original->vars;
	original->vars = tmp;
}

/*--- cw_do_masquerade: Masquerade a channel */
/* Assumes channel will be locked when called */
int cw_do_masquerade(struct cw_channel *original)
{
	int x,i;
	int res=0;
	int origstate;
	struct cw_frame *cur, *prev;
	const struct cw_channel_tech *t;
	void *t_pvt;
	struct cw_callerid tmpcid;
	struct cw_channel *oldchan = original->masq;
	int rformat = original->readformat;
	int wformat = original->writeformat;

	if (option_debug > 3)
		cw_log(CW_LOG_DEBUG, "Actually Masquerading %s(%d) into the structure of %s(%d)\n",
			oldchan->name, oldchan->_state, original->name, original->_state);

	/* XXX This is a seriously wacked out operation.  We're essentially putting the guts of
	   the oldchan channel into the original channel.  Start by killing off the original
	   channel's backend.   I'm not sure we're going to keep this function, because 
	   while the features are nice, the cost is very high in terms of pure nastiness. XXX */

	/* We need the oldchan's lock, too */
	cw_channel_lock(oldchan);

	/* Having remembered the original read/write formats, we turn off any translation on either
	   one */
	free_translation(oldchan);
	free_translation(original);

	/* Unlink the masquerade */
	original->masq = NULL;
	oldchan->masqr = NULL;

	/* Copy the name from the oldchan channel */
	cw_change_name(original, "%s", oldchan->name);

	/* Mangle the name of the oldchan channel */
	cw_change_name(oldchan, "%s<MASQ>", oldchan->name);

	/* Swap the technlogies */	
	t = original->tech;
	original->tech = oldchan->tech;
	oldchan->tech = t;

	t_pvt = original->tech_pvt;
	original->tech_pvt = oldchan->tech_pvt;
	oldchan->tech_pvt = t_pvt;

	/* Swap the readq's */
	cur = original->readq;
	original->readq = oldchan->readq;
	oldchan->readq = cur;

	/* Swap the alertpipes */
	for (i = 0;  i < 2;  i++) {
		x = original->alertpipe[i];
		original->alertpipe[i] = oldchan->alertpipe[i];
		oldchan->alertpipe[i] = x;
	}

	/* Swap the raw formats */
	x = original->rawreadformat;
	original->rawreadformat = oldchan->rawreadformat;
	oldchan->rawreadformat = x;
	x = original->rawwriteformat;
	original->rawwriteformat = oldchan->rawwriteformat;
	oldchan->rawwriteformat = x;

	/* Save any pending frames on both sides.  Start by counting
	 * how many we're going to need... */
	prev = NULL;
	cur = oldchan->readq;
	x = 0;
	while (cur) {
		x++;
		prev = cur;
		cur = cur->next;
	}
	/* If we had any, prepend them to the ones already in the queue, and 
	 * load up the alertpipe */
	if (prev) {
		prev->next = original->readq;
		original->readq = oldchan->readq;
		oldchan->readq = NULL;
		if (original->alertpipe[1] > -1) {
			for (i = 0;  i < x;  i++) {
				char c;
				write(original->alertpipe[1], &c, sizeof(c));
			}
		}
	}
	oldchan->_softhangup = CW_SOFTHANGUP_DEV;


	/* And of course, so does our current state.  Note we need not
	   call cw_setstate since the event manager doesn't really consider
	   these separate.  We do this early so that the oldchan has the proper
	   state of the original channel. */
	origstate = original->_state;
	original->_state = oldchan->_state;
	oldchan->_state = origstate;

	if (oldchan->tech->fixup) {
		if ((res = oldchan->tech->fixup(original, oldchan)))
			cw_log(CW_LOG_WARNING, "Fixup failed on channel %s, strange things may happen.\n", oldchan->name);
	}

	/* Start by disconnecting the original's physical side */
	if (oldchan->tech->hangup)
		res = oldchan->tech->hangup(oldchan);
	if (res) {
		cw_log(CW_LOG_WARNING, "Hangup failed!  Strange things may happen!\n");
		cw_channel_unlock(oldchan);
		return -1;
	}
	
	/* Mangle the name of the oldchan channel */
	x = strrchr(oldchan->name, '<') - oldchan->name;
	cw_change_name(oldchan, "%*.*s<ZOMBIE>", x, x, oldchan->name);

	/* Update the type. */
	original->type = oldchan->type;
	t_pvt = original->monitor;
	original->monitor = oldchan->monitor;
	oldchan->monitor = t_pvt;
	
	/* Keep the same language.  */
	cw_copy_string(original->language, oldchan->language, sizeof(original->language));

	/* Copy the FD's */
	for (x = 0;  x < CW_MAX_FDS;  x++)
		original->fds[x] = oldchan->fds[x];

	/* Drop group from original */
	cw_app_group_discard(original);

	clone_variables(original, oldchan);

	/* Presense of ADSI capable CPE follows oldchan */
	original->adsicpe = oldchan->adsicpe;

	/* Bridge remains the same */
	/* CDR fields remain the same */
	/* XXX What about blocking, softhangup, blocker, and lock? XXX */
	/* Application and data remain the same */

	/* oldchan exception  becomes real one, as with fdno */
	cw_copy_flags(original, oldchan, CW_FLAG_EXCEPTION);
	original->fdno = oldchan->fdno;

	/* Stream stuff stays the same */
	/* Keep the original state.  The fixup code will need to work with it most likely */

	/* Just swap the whole structures, nevermind the allocations, they'll work themselves
	   out. */
	tmpcid = original->cid;
	original->cid = oldchan->cid;
	oldchan->cid = tmpcid;
	
	/* Our native formats are different now */
	original->nativeformats = oldchan->nativeformats;
	
	/* Context, extension, priority, app data, jump table,  remain the same */
	/* pvt switches.  pbx stays the same, as does next */
	
	/* Set the write format */
	cw_set_write_format(original, wformat);

	/* Set the read format */
	cw_set_read_format(original, rformat);

	/* Copy the music class */
	cw_copy_string(original->musicclass, oldchan->musicclass, sizeof(original->musicclass));

	cw_log(CW_LOG_DEBUG, "Putting channel %s in %d/%d formats\n", original->name, wformat, rformat);

	/* Okay.  Last thing is to let the channel driver know about all this mess, so he
	   can fix up everything as best as possible */
	if (original->tech->fixup) {
		res = original->tech->fixup(oldchan, original);
		if (res) {
			cw_log(CW_LOG_WARNING, "Channel for type '%s' could not fixup channel %s\n",
				original->type, original->name);
			cw_channel_unlock(oldchan);
			return -1;
		}
	} else {
		cw_log(CW_LOG_WARNING, "Channel type '%s' does not have a fixup routine (for %s)!  Bad things may happen.\n",
                 original->type, original->name);
	}

	/* Now, at this point, the "oldchan" channel is totally F'd up.  We mark it as
	   a zombie so nothing tries to touch it.  If it's already been marked as a
	   zombie, then free it now (since it already is considered invalid). */
	if (cw_test_flag(oldchan, CW_FLAG_ZOMBIE)) {
		cw_log(CW_LOG_DEBUG, "Destroying channel oldchan '%s'\n", oldchan->name);
		cw_channel_unlock(oldchan);
		cw_channel_free(oldchan);
		cw_manager_event(CW_EVENT_FLAG_CALL, "Hangup",
			4,
			cw_msg_tuple("Channel",   "%s", oldchan->name),
			cw_msg_tuple("Uniqueid",  "%s", oldchan->uniqueid),
			cw_msg_tuple("Cause",     "%d", oldchan->hangupcause),
			cw_msg_tuple("Cause-txt", "%s", cw_cause2str(oldchan->hangupcause))
		);
	} else {
		cw_log(CW_LOG_DEBUG, "Released oldchan lock on '%s'\n", oldchan->name);
		cw_set_flag(oldchan, CW_FLAG_ZOMBIE);
		cw_queue_frame(oldchan, &cw_null_frame);
		cw_channel_unlock(oldchan);
	}
	
	/* Signal any blocker */
	if (cw_test_flag(original, CW_FLAG_BLOCKING))
		pthread_kill(original->blocker, SIGURG);
	cw_log(CW_LOG_DEBUG, "Done Masquerading %s (%d)\n", original->name, original->_state);
	return 0;
}

void cw_set_callerid(struct cw_channel *chan, const char *callerid, const char *calleridname, const char *ani)
{
	if (callerid) {
		free(chan->cid.cid_num);
		if (cw_strlen_zero(callerid))
			chan->cid.cid_num = NULL;
		else
			chan->cid.cid_num = strdup(callerid);
	}
	if (calleridname) {
		free(chan->cid.cid_name);
		if (cw_strlen_zero(calleridname))
			chan->cid.cid_name = NULL;
		else
			chan->cid.cid_name = strdup(calleridname);
	}
	if (ani) {
		free(chan->cid.cid_ani);
		if (cw_strlen_zero(ani))
			chan->cid.cid_ani = NULL;
		else
			chan->cid.cid_ani = strdup(ani);
	}
	if (chan->cdr)
		cw_cdr_setcid(chan->cdr, chan);
	cw_manager_event(CW_EVENT_FLAG_CALL, "Newcallerid",
		5,
		cw_msg_tuple("Channel",         "%s",      chan->name),
		cw_msg_tuple("CallerID",        "%s",      (chan->cid.cid_num ? chan->cid.cid_num : "<Unknown>")),
		cw_msg_tuple("CallerIDName",    "%s",      (chan->cid.cid_name ? chan->cid.cid_name : "<Unknown>")),
		cw_msg_tuple("Uniqueid",        "%s",      chan->uniqueid),
		cw_msg_tuple("CID-CallingPres", "%d (%s)", chan->cid.cid_pres, cw_describe_caller_presentation(chan->cid.cid_pres))
	);
}

int cw_setstate(struct cw_channel *chan, int state)
{
	int oldstate = chan->_state;

	if (oldstate == state)
		return 0;

	chan->_state = state;
	cw_device_state_changed_literal(chan->name);
	cw_manager_event(CW_EVENT_FLAG_CALL, (oldstate == CW_STATE_DOWN ? "Newchannel" : "Newstate"),
		5,
		cw_msg_tuple("Channel",      "%s", chan->name),
		cw_msg_tuple("State",        "%s", cw_state2str(chan->_state)),
		cw_msg_tuple("CallerID",     "%s", (chan->cid.cid_num ? chan->cid.cid_num : "<unknown>")),
		cw_msg_tuple("CallerIDName", "%s", (chan->cid.cid_name ? chan->cid.cid_name : "<unknown>")),
		cw_msg_tuple("Uniqueid",     "%s", chan->uniqueid)
	);

	return 0;
}

/*--- Find bridged channel */
struct cw_channel *cw_bridged_channel(struct cw_channel *chan)
{
	struct cw_channel *bridged;

        if ( !chan ) return NULL;

	cw_channel_lock(chan);

	bridged = chan->_bridge;
	if (bridged && bridged->tech->bridged_channel)
		bridged = bridged->tech->bridged_channel(chan, bridged);

	cw_object_dup(bridged);

	cw_channel_unlock(chan);

	return bridged;
}

static void bridge_playfile(struct cw_channel *chan, struct cw_channel *peer, char *sound, int remain) 
{
	int min = 0, sec = 0, check = 0;

	check = cw_autoservice_start(peer);
	if (check)
		return;

	if (remain > 0) {
		if (remain / 60 > 1) {
			min = remain / 60;
			sec = remain % 60;
		} else {
			sec = remain;
		}
	}

	if (!strcmp(sound, "timeleft")) {
		/* Queue support */
		cw_streamfile(chan, "vm-youhave", chan->language);
		cw_waitstream(chan, "");
		if (min) {
			cw_say_number(chan, min, CW_DIGIT_ANY, chan->language, (char *) NULL);
			cw_streamfile(chan, "queue-minutes", chan->language);
			cw_waitstream(chan, "");
		}
		if (sec) {
			cw_say_number(chan, sec, CW_DIGIT_ANY, chan->language, (char *) NULL);
			cw_streamfile(chan, "queue-seconds", chan->language);
			cw_waitstream(chan, "");
		}
	} else {
		cw_streamfile(chan, sound, chan->language);
		cw_waitstream(chan, "");
	}

	check = cw_autoservice_stop(peer);
}

static enum cw_bridge_result cw_generic_bridge(struct cw_channel *c0,
                                                   struct cw_channel *c1,
                                                   struct cw_bridge_config *config,
                                                   struct cw_frame **fo,
                                                   struct cw_channel **rc,
                                                   struct timeval bridge_end)
{
	/* Copy voice back and forth between the two channels. */
	struct cw_channel *cs[3];
	struct cw_frame *f;
	struct cw_channel *who = NULL;
	enum cw_bridge_result res = CW_BRIDGE_COMPLETE;
	int o0nativeformats;
	int o1nativeformats;
	int watch_c0_dtmf;
	int watch_c1_dtmf;
	void *pvt0, *pvt1;
	int to;
	
	/* Indicates whether a frame was queued into a jitterbuffer */
	int frame_put_in_jb;

	cs[0] = c0;
	cs[1] = c1;
	pvt0 = c0->tech_pvt;
	pvt1 = c1->tech_pvt;
	o0nativeformats = c0->nativeformats;
	o1nativeformats = c1->nativeformats;
	watch_c0_dtmf = config->flags & CW_BRIDGE_DTMF_CHANNEL_0;
	watch_c1_dtmf = config->flags & CW_BRIDGE_DTMF_CHANNEL_1;

	/* Check the need of a jitterbuffer for each channel */
	cw_jb_do_usecheck(c0, c1);

	for (;;) {

        /* We get the T38 status of the 2 channels. 
           If at least one is not in a NON t38 state, then retry 
           This will force another native bridge loop
            */
        int res1,res2;
        res1 = cw_channel_get_t38_status(c0);
        res2 = cw_channel_get_t38_status(c1);
        //cw_log(CW_LOG_DEBUG,"genbridge res t38 = %d:%d [%d %d]\n",res1, res2, T38_STATUS_UNKNOWN, T38_OFFER_REJECTED);

        if ( res1!=res2 ) {
            //cw_log(CW_LOG_DEBUG,"Stopping generic bridge because channels have different modes\n");
            usleep(100);
            return CW_BRIDGE_RETRY;
        }

/*
        if ( res1==T38_NEGOTIATED ) {
            cw_log(CW_LOG_DEBUG,"Stopping generic bridge because channel 0 is t38 enabled ( %d != [%d,%d])\n", res1, T38_STATUS_UNKNOWN, T38_OFFER_REJECTED);
            return CW_BRIDGE_RETRY;
        }
        if ( res2==T38_NEGOTIATED ) {
            cw_log(CW_LOG_DEBUG,"Stopping generic bridge because channel 1 is t38 enabled\n");
            return CW_BRIDGE_RETRY;
        }
*/

		if ((c0->tech_pvt != pvt0) || (c1->tech_pvt != pvt1) ||
			(o0nativeformats != c0->nativeformats) ||
			(o1nativeformats != c1->nativeformats)) {
			/* Check for Masquerade, codec changes, etc */
			res = CW_BRIDGE_RETRY;
			break;
		}

		if (bridge_end.tv_sec) {
			to = cw_tvdiff_ms(bridge_end, cw_tvnow());
			if (to <= 0) {
				if (config->timelimit)
					res = CW_BRIDGE_RETRY;
				else
					res = CW_BRIDGE_COMPLETE;
				break;
			}
		} else {
			to = -1;
		}

		/* Calculate the appropriate max sleep interval - 
		 * in general, this is the time,
		 * left to the closest jb delivery moment */
		to = cw_jb_get_when_to_wakeup(c0, c1, to);

		who = cw_waitfor_n(cs, 2, &to);
		if (!who) {
			if (!to) {
				res = CW_BRIDGE_RETRY;
				break;
			}

			/* No frame received within the specified timeout - 
			 * check if we have to deliver now */
			cw_jb_get_and_deliver(c0, c1);

			cw_log(CW_LOG_DEBUG, "Nobody there, continuing...\n"); 
			if (c0->_softhangup == CW_SOFTHANGUP_UNBRIDGE || c1->_softhangup == CW_SOFTHANGUP_UNBRIDGE) {
				if (c0->_softhangup == CW_SOFTHANGUP_UNBRIDGE)
					c0->_softhangup = 0;
				if (c1->_softhangup == CW_SOFTHANGUP_UNBRIDGE)
					c1->_softhangup = 0;
				c0->_bridge = c1;
				c1->_bridge = c0;
			}
			continue;
		}
		f = cw_read(who);
		if (!f) {
			*fo = NULL;
			*rc = who;
			res = CW_BRIDGE_COMPLETE;
			cw_log(CW_LOG_DEBUG, "Didn't get a frame from channel: %s\n",who->name);
			break;
		}

		/* Try add the frame info the who's bridged channel jitterbuff */
		frame_put_in_jb = !cw_jb_put((who == c0) ? c1 : c0, f, f->subclass);

		if ((f->frametype == CW_FRAME_CONTROL) && !(config->flags & CW_BRIDGE_IGNORE_SIGS)) {
			if ((f->subclass == CW_CONTROL_HOLD) || (f->subclass == CW_CONTROL_UNHOLD) ||
				(f->subclass == CW_CONTROL_VIDUPDATE)) {
				cw_indicate(who == c0 ? c1 : c0, f->subclass);
			} else {
				*fo = f;
				*rc = who;
				res =  CW_BRIDGE_COMPLETE;
				cw_log(CW_LOG_DEBUG, "Got a FRAME_CONTROL (%d) frame on channel %s\n", f->subclass, who->name);
				break;
			}
		}
		if ((f->frametype == CW_FRAME_VOICE) ||
			(f->frametype == CW_FRAME_DTMF) ||
			(f->frametype == CW_FRAME_VIDEO) || 
			(f->frametype == CW_FRAME_IMAGE) ||
			(f->frametype == CW_FRAME_HTML) ||
			(f->frametype == CW_FRAME_MODEM) ||
			(f->frametype == CW_FRAME_TEXT)) {

			if (f->frametype == CW_FRAME_DTMF) {
				if (((who == c0) && watch_c0_dtmf) ||
					((who == c1) && watch_c1_dtmf)) {
					*rc = who;
					*fo = f;
					res = CW_BRIDGE_COMPLETE;
					cw_log(CW_LOG_DEBUG, "Got DTMF on channel (%s)\n", who->name);
					break;
                                } else if ( f->frametype == CW_FRAME_MODEM ) {
                                        cw_log(CW_LOG_DEBUG, "Got MODEM frame on channel (%s). Exiting generic bridge.\n", who->name);
                                    /* If we got a t38 frame... exit this generic bridge */
                                        return CW_BRIDGE_RETRY;
				} else {
					goto tackygoto;
				}
			} else {
#if 0
				cw_log(CW_LOG_DEBUG, "Read from %s\n", who->name);
				if (who == last) 
					cw_log(CW_LOG_DEBUG, "Servicing channel %s twice in a row?\n", last->name);
				last = who;
#endif
tackygoto:
				/* Write immediately frames, not passed through jb */
				if (!frame_put_in_jb)
					cw_write((who == c0)  ?  c1  :  c0, &f);
				
				/* Check if we have to deliver now */
				cw_jb_get_and_deliver(c0, c1);
			}
		}
		cw_fr_free(f);

		/* Swap who gets priority */
		cs[2] = cs[0];
		cs[0] = cs[1];
		cs[1] = cs[2];

	}
	return res;
}

/*--- cw_channel_bridge: Bridge two channels together */
enum cw_bridge_result cw_channel_bridge(struct cw_channel *c0, struct cw_channel *c1, struct cw_bridge_config *config, struct cw_frame **fo, struct cw_channel **rc) 
{
	struct cw_channel *who = NULL;
	enum cw_bridge_result res = CW_BRIDGE_COMPLETE;
	int nativefailed=0;
	int firstpass;
	int o0nativeformats;
	int o1nativeformats;
	long time_left_ms=0;
	struct timeval nexteventts = { 0, };
	int caller_warning = 0;
	int callee_warning = 0;
	int to;
	int x;

	/* Check neither channel is bridged, a zombie or in soft hangup and keep track
	 * of the bridge. _bridge is only set for the duration of the following loop.
	 * Since both c0 and c1 were passed as arguments we know they remain valid
	 * references until we return. Therefore we do not need to use cw_object_dup
	 * to count these.
	 */

	res = CW_BRIDGE_FAILED;
	cw_channel_lock(c0);
	if (c0->_bridge)
		cw_log(CW_LOG_WARNING, "%s is already in a bridge with %s\n", c0->name, c0->_bridge->name);
	else if (!cw_test_flag(c0, CW_FLAG_ZOMBIE) && !cw_check_hangup(c0)) {
		res = CW_BRIDGE_COMPLETE;
		c0->_bridge = c1;
	}
	cw_channel_unlock(c0);
	if (res != CW_BRIDGE_COMPLETE)
		return res;

	res = CW_BRIDGE_FAILED;
	cw_channel_lock(c1);
	if (c1->_bridge)
		cw_log(CW_LOG_WARNING, "%s is already in a bridge with %s\n", c1->name, c1->_bridge->name);
	else if (!cw_test_flag(c1, CW_FLAG_ZOMBIE) && !cw_check_hangup(c1)) {
		res = CW_BRIDGE_COMPLETE;
		c1->_bridge = c0;
	}
	cw_channel_unlock(c1);
	if (res != CW_BRIDGE_COMPLETE) {
		cw_channel_lock(c0);
		c0->_bridge = NULL;
		cw_channel_unlock(c0);
		return res;
	}

	*fo = NULL;
	firstpass = config->firstpass;
	config->firstpass = 0;

	if (cw_tvzero(config->start_time))
		config->start_time = cw_tvnow();
	time_left_ms = config->timelimit;

	caller_warning = cw_test_flag(&config->features_caller, CW_FEATURE_PLAY_WARNING);
	callee_warning = cw_test_flag(&config->features_callee, CW_FEATURE_PLAY_WARNING);

	if (config->start_sound && firstpass) {
		if (caller_warning)
			bridge_playfile(c0, c1, config->start_sound, time_left_ms / 1000);
		if (callee_warning)
			bridge_playfile(c1, c0, config->start_sound, time_left_ms / 1000);
	}

	cw_manager_event(CW_EVENT_FLAG_CALL, "Link",
		6,
		cw_msg_tuple("Channel1",  "%s", c0->name),
		cw_msg_tuple("Channel2",  "%s", c1->name),
		cw_msg_tuple("Uniqueid1", "%s", c0->uniqueid),
		cw_msg_tuple("Uniqueid2", "%s", c1->uniqueid),
		cw_msg_tuple("CallerID1", "%s", c0->cid.cid_num),
		cw_msg_tuple("CallerID2", "%s", c1->cid.cid_num)
	);

	o0nativeformats = c0->nativeformats;
	o1nativeformats = c1->nativeformats;

	if (config->timelimit) {
		nexteventts = cw_tvadd(config->start_time, cw_samp2tv(config->timelimit, 1000));
		if (caller_warning || callee_warning)
			nexteventts = cw_tvsub(nexteventts, cw_samp2tv(config->play_warning, 1000));
	}

	for (/* ever */;;) {
		to = -1;
		if (config->timelimit) {
			struct timeval now;
			now = cw_tvnow();
			to = cw_tvdiff_ms(nexteventts, now);
			if (to < 0)
				to = 0;
			time_left_ms = config->timelimit - cw_tvdiff_ms(now, config->start_time);
			if (time_left_ms < to)
				to = time_left_ms;

			if (time_left_ms <= 0) {
				if (caller_warning && config->end_sound)
					bridge_playfile(c0, c1, config->end_sound, 0);
				if (callee_warning && config->end_sound)
					bridge_playfile(c1, c0, config->end_sound, 0);
				*fo = NULL;
				if (who) 
					*rc = who;
				res = CW_BRIDGE_COMPLETE;
				break;
			}
			
			if (!to) {
				if (time_left_ms >= 5000) {
					if (caller_warning && config->warning_sound && config->play_warning)
						bridge_playfile(c0, c1, config->warning_sound, time_left_ms / 1000);
					if (callee_warning && config->warning_sound && config->play_warning)
						bridge_playfile(c1, c0, config->warning_sound, time_left_ms / 1000);
				}
				if (config->warning_freq) {
					nexteventts = cw_tvadd(nexteventts, cw_samp2tv(config->warning_freq, 1000));
				} else
					nexteventts = cw_tvadd(config->start_time, cw_samp2tv(config->timelimit, 1000));
			}
		}

		if (c0->_softhangup == CW_SOFTHANGUP_UNBRIDGE || c1->_softhangup == CW_SOFTHANGUP_UNBRIDGE) {
			if (c0->_softhangup == CW_SOFTHANGUP_UNBRIDGE)
				c0->_softhangup = 0;
			if (c1->_softhangup == CW_SOFTHANGUP_UNBRIDGE)
				c1->_softhangup = 0;
			cw_log(CW_LOG_DEBUG, "Unbridge signal received. Ending native bridge.\n");
			continue;
		}

		/* Stop if we're a zombie or need a soft hangup */
		cw_channel_lock(c0);
		if ((x = (cw_test_flag(c0, CW_FLAG_ZOMBIE) || cw_check_hangup(c0))))
			cw_log(CW_LOG_DEBUG, "Bridge stops because %s is %s", c0->name, (cw_test_flag(c0, CW_FLAG_ZOMBIE) ? "a zombie" : "in soft hangup"));
		else if ((x = (cw_test_flag(c1, CW_FLAG_ZOMBIE) || cw_check_hangup(c1))))
			cw_log(CW_LOG_DEBUG, "Bridge stops because %s is %s", c1->name, (cw_test_flag(c1, CW_FLAG_ZOMBIE) ? "a zombie" : "in soft hangup"));
		cw_channel_unlock(c0);
		if (x) {
			*fo = NULL;
			if (who)
				*rc = who;
			res = CW_BRIDGE_COMPLETE;
			break;
		}

		if (c0->tech == c1->tech && c0->tech->bridge && config->timelimit == 0 && !nativefailed && !c0->monitor && !c1->monitor && !c0->spies && !c1->spies) {
			/* Looks like they share a bridge method */
			if (option_verbose > 2) 
				cw_log(CW_LOG_DEBUG,"Attempting native bridge of %s and %s\n", c0->name, c1->name);

			cw_set_flag(c0, CW_FLAG_NBRIDGE);
			cw_set_flag(c1, CW_FLAG_NBRIDGE);
			res = c0->tech->bridge(c0, c1, config->flags, fo, rc, to);
			cw_clear_flag(c0, CW_FLAG_NBRIDGE);
			cw_clear_flag(c1, CW_FLAG_NBRIDGE);

			if (res == CW_BRIDGE_COMPLETE) {
				if (c0->_softhangup != CW_SOFTHANGUP_UNBRIDGE && c1->_softhangup != CW_SOFTHANGUP_UNBRIDGE)
					break;
			} else {
				if (res != CW_BRIDGE_FAILED_NOWARN)
					cw_log(CW_LOG_WARNING, "Native bridge between %s and %s failed\n", c0->name, c1->name);
				nativefailed++;
			}
		} else {
			if ((c0->writeformat != c1->readformat || c0->readformat != c1->writeformat
			|| c0->nativeformats != o0nativeformats || c1->nativeformats != o1nativeformats)
			&& !(cw_generator_is_active(c0) || cw_generator_is_active(c1))) {
				if (cw_channel_make_compatible(c0, c1)) {
					cw_log(CW_LOG_WARNING, "Can't make %s and %s compatible\n", c0->name, c1->name);
					res = CW_BRIDGE_FAILED;
					break;
				}
				o0nativeformats = c0->nativeformats;
				o1nativeformats = c1->nativeformats;
			}
			if ((res = cw_generic_bridge(c0, c1, config, fo, rc, nexteventts)) != CW_BRIDGE_RETRY)
				break;
		}
	}

	cw_channel_lock(c0);
	c0->_bridge = NULL;
	cw_channel_unlock(c0);

	cw_channel_lock(c1);
	c1->_bridge = NULL;
	cw_channel_unlock(c1);

	cw_manager_event(CW_EVENT_FLAG_CALL, "Unlink",
		6,
		cw_msg_tuple("Channel1",  "%s", c0->name),
		cw_msg_tuple("Channel2",  "%s", c1->name),
		cw_msg_tuple("Uniqueid1", "%s", c0->uniqueid),
		cw_msg_tuple("Uniqueid2", "%s", c1->uniqueid),
		cw_msg_tuple("CallerID1", "%s", c0->cid.cid_num),
		cw_msg_tuple("CallerID2", "%s", c1->cid.cid_num)
	);
	cw_log(CW_LOG_DEBUG, "Bridge stops bridging channels %s and %s\n", c0->name, c1->name);

	return res;
}

struct tonepair_def
{
    tone_gen_descriptor_t tone_desc;
};

struct tonepair_state
{
	tone_gen_state_t tone_state;
	int origwfmt;
	struct cw_frame f;
	int16_t data[CW_FRIENDLY_OFFSET / sizeof(int16_t) + 4000];
};

static void tonepair_release(struct cw_channel *chan, void *params)
{
	struct tonepair_state *ts = params;

	if (chan)
		cw_set_write_format(chan, ts->origwfmt);
	free(ts);
}

static void *tonepair_alloc(struct cw_channel *chan, void *params)
{
	struct tonepair_state *ts;
	struct tonepair_def *td = params;

	if ((ts = calloc(1, sizeof(*ts))) == NULL)
		return NULL;
	ts->origwfmt = chan->writeformat;
	if (cw_set_write_format(chan, CW_FORMAT_SLINEAR))
	{
		cw_log(CW_LOG_WARNING, "Unable to set '%s' to signed linear format (write)\n", chan->name);
		tonepair_release(NULL, ts);
		return NULL;
	}
	tone_gen_init(&ts->tone_state, &td->tone_desc);
	cw_set_flag(chan, CW_FLAG_WRITE_INT);
	return ts;
}

static struct cw_frame *tonepair_generate(struct cw_channel *chan, void *data, int samples)
{
	struct tonepair_state *ts = data;

	CW_UNUSED(chan);

	cw_fr_init_ex(&ts->f, CW_FRAME_VOICE, CW_FORMAT_SLINEAR);

	ts->f.datalen = samples * sizeof(ts->data[0]);
	if (ts->f.datalen > sizeof(ts->data) / sizeof(ts->data[0]) - 1)
		ts->f.datalen = sizeof(ts->data) / sizeof(ts->data[0]) - 1;

	ts->f.samples = ts->f.datalen / sizeof(ts->data[0]);
	ts->f.offset = CW_FRIENDLY_OFFSET;
	ts->f.data = (uint8_t *)&ts->data[CW_FRIENDLY_OFFSET / sizeof(ts->data[0])];
	tone_gen(&ts->tone_state, &ts->data[CW_FRIENDLY_OFFSET / sizeof(ts->data[0])], ts->f.datalen / sizeof(ts->data[0]));
	return &ts->f;
}

static struct cw_generator tonepair =
{
	alloc: tonepair_alloc,
	release: tonepair_release,
	generate: tonepair_generate,
};

int cw_tonepair_start(struct cw_channel *chan, int freq1, int freq2, int duration, int vol)
{

    struct tonepair_def d;

    if (!tonepair.is_initialized)
        cw_object_init_obj(&tonepair.obj, CW_OBJECT_CURRENT_MODULE, 0);

    if (vol >= 0)
	vol = -13;

    if (duration == 0) {
        make_tone_gen_descriptor(&d.tone_desc,
                                 freq1,
                                 vol,
                                 freq2,
                                 vol,
                                 1,
                                 0,
                                 0,
                                 0,
                                 1);
    } else {
        make_tone_gen_descriptor(&d.tone_desc,
                                 freq1,
                                 vol,
                                 freq2,
                                 vol,
                                 duration*8,
                                 0,
                                 0,
                                 0,
                                 0);
    }
    if (cw_generator_activate(chan, &chan->generator, &tonepair, &d))
		return -1;
	return 0;
}

void cw_tonepair_stop(struct cw_channel *chan)
{
	cw_generator_deactivate(&chan->generator);
}

int cw_tonepair(struct cw_channel *chan, int freq1, int freq2, int duration, int vol)
{
	int res;

	if ((res = cw_tonepair_start(chan, freq1, freq2, duration, vol)))
		return res;

	/* Don't return to caller until after duration has passed */
	cw_safe_sleep(chan, duration);
        cw_tonepair_stop(chan);
        return 0;
}

cw_group_t cw_get_group(char *s)
{
	char *piece;
	char *c = NULL;
	int start = 0;
	int finish = 0;
	int x;
	cw_group_t group = 0;

	c = cw_strdupa(s);

	while ((piece = strsep(&c, ","))) {
		if (sscanf(piece, "%d-%d", &start, &finish) == 2) {
			/* Range */
		} else if (sscanf(piece, "%d", &start)) {
			/* Just one */
			finish = start;
		} else {
			cw_log(CW_LOG_ERROR, "Syntax error parsing group configuration '%s' at '%s'. Ignoring.\n", s, piece);
			continue;
		}
		for (x = start;  x <= finish;  x++) {
			if ((x > 63)  ||  (x < 0))
				cw_log(CW_LOG_WARNING, "Ignoring invalid group %d (maximum group is 63)\n", x);
			else
				group |= ((cw_group_t) 1 << x);
		}
	}

	return group;
}

static int (*cw_moh_start_ptr)(struct cw_channel *, const char *) = NULL;
static void (*cw_moh_stop_ptr)(struct cw_channel *) = NULL;
static void (*cw_moh_cleanup_ptr)(struct cw_channel *) = NULL;

void cw_install_music_functions(int (*start_ptr)(struct cw_channel *, const char *), void (*stop_ptr)(struct cw_channel *), void (*cleanup_ptr)(struct cw_channel *))
{
	cw_moh_start_ptr = start_ptr;
	cw_moh_stop_ptr = stop_ptr;
	cw_moh_cleanup_ptr = cleanup_ptr;
}

void cw_uninstall_music_functions(void) 
{
	cw_moh_start_ptr = NULL;
	cw_moh_stop_ptr = NULL;
	cw_moh_cleanup_ptr = NULL;
}

/*! Turn on music on hold on a given channel */
int cw_moh_start(struct cw_channel *chan, const char *mclass)
{
	if (cw_moh_start_ptr)
		return cw_moh_start_ptr(chan, mclass);

	if (option_verbose > 2)
		cw_verbose(VERBOSE_PREFIX_3 "Music class %s requested but no musiconhold loaded.\n", mclass ? mclass : "default");
	
	return 0;
}

/*! Turn off music on hold on a given channel */
void cw_moh_stop(struct cw_channel *chan) 
{
	if(cw_moh_stop_ptr)
		cw_moh_stop_ptr(chan);
}

void cw_moh_cleanup(struct cw_channel *chan) 
{
	if(cw_moh_cleanup_ptr)
        cw_moh_cleanup_ptr(chan);
}

int cw_channels_init(void)
{
	cw_cli_register(&cli_show_channeltypes);
	return 0;
}

/*--- cw_print_group: Print call group and pickup group ---*/
char *cw_print_group(char *buf, int buflen, cw_group_t group) 
{
	unsigned int i;
	int first=1;
	char num[3];

	buf[0] = '\0';
	
	if (!group)	/* Return empty string if no group */
		return(buf);

	for (i=0; i<=63; i++) {	/* Max group is 63 */
		if (group & ((cw_group_t) 1 << i)) {
			if (!first) {
				strncat(buf, ", ", buflen);
			} else {
				first=0;
			}
			snprintf(num, sizeof(num), "%u", i);
			strncat(buf, num, buflen);
		}
	}
	return(buf);
}

void cw_set_variables(struct cw_channel *chan, struct cw_variable *vars)
{
	struct cw_variable *cur;

	for (cur = vars; cur; cur = cur->next)
		pbx_builtin_setvar_helper(chan, cur->name, cur->value);	
}

/* If you are calling carefulwrite, it is assumed that you are calling
   it on a file descriptor that _DOES_ have NONBLOCK set.  This way,
   there is only one system call made to do a write, unless we actually
   have a need to wait.  This way, we get better performance. */
int cw_carefulwrite(int fd, char *s, int len, int timeoutms)
{
	/* Try to write string, but wait no more than ms milliseconds before timing out */
	int res = 0, n;
	
	while (len) {
		while ((res = write(fd, s, len)) < 0 && errno == EINTR);
		if (res < 0) {
			if (errno != EAGAIN)
				break;
			res = 0;
		}

		len -= res;
		s += res;

		if (len) {
			struct pollfd pfd;

			pfd.fd = fd;
			pfd.events = POLLOUT;
			res = -1;
			while ((n = poll(&pfd, 1, timeoutms)) < 0 && errno == EINTR);
			if (n < 1)
				break;
		}
	}

	return res;
}

/*--- cw_spy_empty_queues: Quickly empty both queues and return the frames. */
/** Mark both queues as empty and return the frames.
 * @param spy The (unlocked) spy to empty.
 * @param f0 The pointer for the frames of the first queue.
 * @param f1 The pointer for the frames of the second queue.
 */
void cw_spy_empty_queues(struct cw_channel_spy *spy, struct cw_frame **f0, struct cw_frame **f1)
{
    cw_mutex_lock(&spy->lock);
    *f0 = spy->queue[0].head;
    *f1 = spy->queue[1].head;
    spy->queue[0].head = spy->queue[0].tail = NULL;
    spy->queue[1].head = spy->queue[1].tail = NULL;
    spy->queue[0].count = spy->queue[1].count = 0;
    cw_mutex_unlock(&spy->lock);
}
