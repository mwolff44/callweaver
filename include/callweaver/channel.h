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
 * \brief General CallWeaver PBX channel definitions.
 */

#ifndef _CALLWEAVER_CHANNEL_H
#define _CALLWEAVER_CHANNEL_H

#include "confdefs.h"
#include "callweaver/frame.h"
#include "callweaver/sched.h"
#include "callweaver/chanvars.h"
#include "callweaver/config.h"

#include <unistd.h>
#include <setjmp.h>
#ifdef POLLCOMPAT 
#include "callweaver/poll-compat.h"
#else
#include <sys/poll.h>
#endif

#include "callweaver/generic_jb.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "callweaver/lock.h"

/*! Max length of an extension */
#define OPBX_MAX_EXTENSION	80

#define OPBX_MAX_CONTEXT		80

#include "callweaver/cdr.h"
#include "callweaver/monitor.h"
#include "callweaver/utils.h"
#include "callweaver/generator.h"

#define OPBX_CHANNEL_NAME	80

#define MAX_LANGUAGE		20

#define MAX_MUSICCLASS		20

#define OPBX_MAX_FDS		8

enum opbx_bridge_result {
	OPBX_BRIDGE_COMPLETE = 0,
	OPBX_BRIDGE_FAILED = -1,
	OPBX_BRIDGE_FAILED_NOWARN = -2,
	OPBX_BRIDGE_RETRY = -3,
};

typedef unsigned long long opbx_group_t;

struct opbx_callerid {
	/*! Malloc'd Dialed Number Identifier */
	char *cid_dnid;				
	/*! Malloc'd Caller Number */
	char *cid_num;
	/*! Malloc'd Caller Name */
	char *cid_name;
	/*! Malloc'd ANI */
	char *cid_ani;			
	/*! Malloc'd RDNIS */
	char *cid_rdnis;
	/*! Callerid presentation/screening */
	int cid_pres;
	/*! Callerid ANI 2 (Info digits) */
	int cid_ani2;
	/*! Callerid Type of Number */
	int cid_ton;
	/*! Callerid Transit Network Select */
	int cid_tns;
};

/*! Structure to describe a channel "technology" */

struct opbx_channel_tech {
	const char * const type;
	const char * const description;

	/*! Bitmap of formats this channel can handle */
	int capabilities;

	/*! Technology Properties */
	int properties;

	struct opbx_channel *(* const requester)(const char *type, int format, void *data, int *cause);

	int (* const devicestate)(void *data);

	/*! Send a literal DTMF digit */
	int (* const send_digit)(struct opbx_channel *chan, char digit);

	/*! Call a given phone number (address, etc), but don't
	   take longer than timeout seconds to do so.  */
	int (* const call)(struct opbx_channel *chan, char *addr, int timeout);

	/*! Hangup (and possibly destroy) the channel */
	int (* const hangup)(struct opbx_channel *chan);

	/*! Answer the line */
	int (* const answer)(struct opbx_channel *chan);

	/*! Read a frame, in standard format */
	struct opbx_frame * (* const read)(struct opbx_channel *chan);

	/*! Write a frame, in standard format */
	int (* const write)(struct opbx_channel *chan, struct opbx_frame *frame);

	/*! Display or transmit text */
	int (* const send_text)(struct opbx_channel *chan, const char *text);

	/*! Display or send an image */
	int (* const send_image)(struct opbx_channel *chan, struct opbx_frame *frame);

	/*! Send HTML data */
	int (* const send_html)(struct opbx_channel *chan, int subclass, const char *data, int len);

	/*! Handle an exception, reading a frame */
	struct opbx_frame * (* const exception)(struct opbx_channel *chan);

	/*! Bridge two channels of the same type together */
	enum opbx_bridge_result (* const bridge)(struct opbx_channel *c0, struct opbx_channel *c1, int flags,
						struct opbx_frame **fo, struct opbx_channel **rc, int timeoutms);

	/*! Indicate a particular condition (e.g. OPBX_CONTROL_BUSY or OPBX_CONTROL_RINGING or OPBX_CONTROL_CONGESTION */
	int (* const indicate)(struct opbx_channel *c, int condition);

	/*! Fix up a channel:  If a channel is consumed, this is called.  Basically update any ->owner links */
	int (* const fixup)(struct opbx_channel *oldchan, struct opbx_channel *newchan);

	/*! Set a given option */
	int (* const setoption)(struct opbx_channel *chan, int option, void *data, int datalen);

	/*! Query a given option */
	int (* const queryoption)(struct opbx_channel *chan, int option, void *data, int *datalen);

	/*! Blind transfer other side */
	int (* const transfer)(struct opbx_channel *chan, const char *newdest);

	/*! Write a frame, in standard format */
	int (* const write_video)(struct opbx_channel *chan, struct opbx_frame *frame);

	/*! Find bridged channel */
	struct opbx_channel *(* const bridged_channel)(struct opbx_channel *chan, struct opbx_channel *bridge);
};


#define CHANSPY_NEW 0
#define CHANSPY_RUNNING 1
#define CHANSPY_DONE 2

struct opbx_channel_spy {
	struct opbx_frame *queue[2];
	opbx_mutex_t lock;
	char status;
	struct opbx_channel_spy *next;
};


/*! Main Channel structure associated with a channel. */
/*! 
 * This is the side of it mostly used by the pbx and call management.
 */
struct opbx_channel {
	/*! ASCII Description of channel name */
	char name[OPBX_CHANNEL_NAME];
	
	/*! Technology */
	const struct opbx_channel_tech *tech;
	/*! Private data used by the technology driver */
	void *tech_pvt;

	/*! Language requested */
	char language[MAX_LANGUAGE];		
	/*! Type of channel */
	const char *type;				
	/*! File descriptor for channel -- Drivers will poll on these file descriptors, so at least one must be non -1.  */
	int fds[OPBX_MAX_FDS];			

	/*! Default music class */
	char musicclass[MAX_MUSICCLASS];
	/*! Music State*/
	void *music_state;

	/*! All generator data including generator data lock*/
	struct opbx_generator_channel_data gcd;

	/*! Comfort noise level to generate in dBov's */
	int comfortnoiselevel;

	/*! Who are we bridged to, if we're bridged. Who is proxying for us,
	  if we are proxied (i.e. chan_agent).
	  Do not access directly, use opbx_bridged_channel(chan) */
	struct opbx_channel *_bridge;
	/*! Channel that will masquerade as us */
	struct opbx_channel *masq;		
	/*! Who we are masquerading as */
	struct opbx_channel *masqr;		
	/*! Call Detail Record Flags */
	int cdrflags;										   
	/*! Whether or not we have been hung up...  Do not set this value
	    directly, use opbx_softhangup */
	int _softhangup;				
	/*! Non-zero, set to actual time when channel is to be hung up */
	time_t	whentohangup;
	/*! If anyone is blocking, this is them */
	pthread_t blocker;			
	/*! Lock, can be used to lock a channel for some operations */
	opbx_mutex_t lock;			
	/*! Procedure causing blocking */
	const char *blockproc;			

	/*! Current application */
	char *appl;				
	/*! Data passed to current application */
	char *data;				
	
	/*! Which fd had an event detected on */
	int fdno;				
	/*! Schedule context */
	struct sched_context *sched;		
	/*! For streaming playback, the schedule ID */
	int streamid;
        /*! Stream itself. */
        struct opbx_filestream *stream;
	/*! For streaming playback, the schedule ID */
	int vstreamid;
        /*! Stream itself. */
        struct opbx_filestream *vstream;
	/*! Original writer format */
	int oldwriteformat;			
	
	/*! State of line -- Don't write directly, use opbx_setstate */
	int _state;				
	/*! Number of rings so far */
	int rings;				

	/*! Kinds of data this channel can natively handle */
	int nativeformats;			
	/*! Requested read format */
	int readformat;				
	/*! Requested write format */
	int writeformat;			

	struct opbx_callerid cid;
		
	/*! Current extension context */
	char context[OPBX_MAX_CONTEXT];
	/*! Current non-macro context */
	char macrocontext[OPBX_MAX_CONTEXT];	
	/*! Current non-macro extension */
	char macroexten[OPBX_MAX_EXTENSION];
	/*! Current non-macro priority */
	int macropriority;
	/*! Current extension number */
	char exten[OPBX_MAX_EXTENSION];		
	/* Current extension priority */
	int priority;						
	/*! Any/all queued DTMF characters */
	char dtmfq[OPBX_MAX_EXTENSION];		
	/*! DTMF frame */
	struct opbx_frame dtmff;			

	/*! PBX private structure */
	struct opbx_pbx *pbx;
	/*! Set BEFORE PBX is started to determine AMA flags */
	int 	amaflags;			
	/*! Account code for billing */
	char 	accountcode[OPBX_MAX_ACCOUNT_CODE];		
	/*! Call Detail Record */
	struct opbx_cdr *cdr;			
	/*! Whether or not ADSI is detected on CPE */
	int	adsicpe;
	/*! Where to forward to if asked to dial on this interface */
	char call_forward[OPBX_MAX_EXTENSION];

	/*! Tone zone */
	struct tone_zone *zone;

	/* Channel monitoring */
	struct opbx_channel_monitor *monitor;

	/*! Track the read/written samples for monitor use */
	unsigned long insmpl;
	unsigned long outsmpl;

	/* Frames in/out counters */
	unsigned int fin;
	unsigned int fout;

	/* Unique Channel Identifier */
	char uniqueid[32];

	/* Why is the channel hanged up */
	int hangupcause;
	
	/* A linked list for variables */
	struct varshead varshead;

	opbx_group_t callgroup;
	opbx_group_t pickupgroup;

	/*! channel flags of OPBX_FLAG_ type */
	unsigned int flags;
	
	/* ISDN Transfer Capbility - OPBX_FLAG_DIGITAL is not enough */
	unsigned short transfercapability;

	struct opbx_frame *readq;
	int alertpipe[2];
	/*! Write translation path */
	struct opbx_trans_pvt *writetrans;
	/*! Read translation path */
	struct opbx_trans_pvt *readtrans;
	/*! Raw read format */
	int rawreadformat;
	/*! Raw write format */
	int rawwriteformat;

	/*! Chan Spy stuff */
	struct opbx_channel_spy *spiers;

	/*! For easy linking */
	struct opbx_channel *next;

	/*! The jitterbuffer state  */
	struct opbx_jb jb;

	/*! T38 mode enabled for this channel  */
	int t38mode_enabled;
};

/* Channel tech properties: */
/* Channels have this property if they can accept input with jitter; i.e. most VoIP channels */
#define OPBX_CHAN_TP_WANTSJITTER	(1 << 0)	

/* \defgroup chanprop Channel tech properties:
	\brief Channels have this property if they can create jitter; i.e. most VoIP channels */
/* @{ */
#define OPBX_CHAN_TP_CREATESJITTER (1 << 1)

/* This flag has been deprecated by the transfercapbilty data member in struct opbx_channel */
/* #define OPBX_FLAG_DIGITAL	(1 << 0) */	/* if the call is a digital ISDN call */
#define OPBX_FLAG_DEFER_DTMF	(1 << 1)	/* if dtmf should be deferred */
#define OPBX_FLAG_WRITE_INT	(1 << 2)	/* if write should be interrupt generator */
#define OPBX_FLAG_BLOCKING	(1 << 3)	/* if we are blocking */
#define OPBX_FLAG_ZOMBIE		(1 << 4)	/* if we are a zombie */
#define OPBX_FLAG_EXCEPTION	(1 << 5)	/* if there is a pending exception */
#define OPBX_FLAG_MOH		(1 << 6)	/* XXX anthm promises me this will disappear XXX listening to moh */
#define OPBX_FLAG_SPYING		(1 << 7)	/* XXX might also go away XXX is spying on someone */
#define OPBX_FLAG_NBRIDGE	(1 << 8)	/* is it in a native bridge */
#define OPBX_FLAG_IN_AUTOLOOP	(1 << 9)	/* the channel is in an auto-incrementing dialplan processor,
						   so when ->priority is set, it will get incremented before
						   finding the next priority to run
						*/

#define OPBX_FEATURE_PLAY_WARNING	(1 << 0)
#define OPBX_FEATURE_REDIRECT		(1 << 1)
#define OPBX_FEATURE_DISCONNECT		(1 << 2)
#define OPBX_FEATURE_ATXFER		(1 << 3)
#define OPBX_FEATURE_AUTOMON		(1 << 4)

#define OPBX_FEATURE_FLAG_NEEDSDTMF	(1 << 0)
#define OPBX_FEATURE_FLAG_CALLEE		(1 << 1)
#define OPBX_FEATURE_FLAG_CALLER		(1 << 2)

struct opbx_bridge_config {
	struct opbx_flags features_caller;
	struct opbx_flags features_callee;
	struct timeval start_time;
	long feature_timer;
	long timelimit;
	long play_warning;
	long warning_freq;
	char *warning_sound;
	char *end_sound;
	char *start_sound;
	int firstpass;
	unsigned int flags;
};

struct chanmon;

#define LOAD_OH(oh) {	\
	oh.context = context; \
	oh.exten = exten; \
	oh.priority = priority; \
	oh.cid_num = cid_num; \
	oh.cid_name = cid_name; \
	oh.vars = vars; \
} 

struct outgoing_helper {
	const char *context;
	const char *exten;
	int priority;
	const char *cid_num;
	const char *cid_name;
	struct opbx_variable *vars;
};

#define OPBX_CDR_TRANSFER	(1 << 0)
#define OPBX_CDR_FORWARD		(1 << 1)
#define OPBX_CDR_CALLWAIT	(1 << 2)
#define OPBX_CDR_CONFERENCE	(1 << 3)

#define OPBX_ADSI_UNKNOWN	(0)
#define OPBX_ADSI_AVAILABLE	(1)
#define OPBX_ADSI_UNAVAILABLE	(2)
#define OPBX_ADSI_OFFHOOKONLY	(3)

#define OPBX_SOFTHANGUP_DEV			(1 << 0)	/* Soft hangup by device */
#define OPBX_SOFTHANGUP_ASYNCGOTO	(1 << 1)	/* Soft hangup for async goto */
#define OPBX_SOFTHANGUP_SHUTDOWN		(1 << 2)
#define OPBX_SOFTHANGUP_TIMEOUT		(1 << 3)
#define OPBX_SOFTHANGUP_APPUNLOAD	(1 << 4)
#define OPBX_SOFTHANGUP_EXPLICIT		(1 << 5)
#define OPBX_SOFTHANGUP_UNBRIDGE     (1 << 6)

/* Bits 0-15 of state are reserved for the state (up/down) of the line */
/*! Channel is down and available */
#define OPBX_STATE_DOWN		0		
/*! Channel is down, but reserved */
#define OPBX_STATE_RESERVED	1		
/*! Channel is off hook */
#define OPBX_STATE_OFFHOOK	2		
/*! Digits (or equivalent) have been dialed */
#define OPBX_STATE_DIALING	3		
/*! Line is ringing */
#define OPBX_STATE_RING		4		
/*! Remote end is ringing */
#define OPBX_STATE_RINGING	5		
/*! Line is up */
#define OPBX_STATE_UP		6		
/*! Line is busy */
#define OPBX_STATE_BUSY  	7		
/*! Digits (or equivalent) have been dialed while offhook */
#define OPBX_STATE_DIALING_OFFHOOK	8
/*! Channel has detected an incoming call and is waiting for ring */
#define OPBX_STATE_PRERING       9

/* Bits 16-32 of state are reserved for flags */
/*! Do not transmit voice data */
#define OPBX_STATE_MUTE		(1 << 16)	

/*! Create a channel structure */
/*! Returns NULL on failure to allocate. New channels are 
	by default set to the "default" context and
	extension "s"
 */
struct opbx_channel *opbx_channel_alloc(int needalertpipe);

/*! Queue an outgoing frame */
int opbx_queue_frame(struct opbx_channel *chan, struct opbx_frame *f);

/*! Queue a hangup frame */
int opbx_queue_hangup(struct opbx_channel *chan);

/*! Queue a control frame */
int opbx_queue_control(struct opbx_channel *chan, int control);

/*! Change the state of a channel */
int opbx_setstate(struct opbx_channel *chan, int state);

void opbx_change_name(struct opbx_channel *chan, char *newname);

/*! Free a channel structure */
void  opbx_channel_free(struct opbx_channel *);

/*! Requests a channel */
/*! 
 * \param type type of channel to request
 * \param format requested channel format
 * \param data data to pass to the channel requester
 * Request a channel of a given type, with data as optional information used 
 * by the low level module
 * Returns an opbx_channel on success, NULL on failure.
 */
struct opbx_channel *opbx_request(const char *type, int format, void *data, int *status);

/*!
 * \param type type of channel to request
 * \param format requested channel format
 * \param data data to pass to the channel requester
 * \param timeout maximum amount of time to wait for an answer
 * \param why unsuccessful (if unsuceessful)
 * Request a channel of a given type, with data as optional information used 
 * by the low level module and attempt to place a call on it
 * Returns an opbx_channel on success or no answer, NULL on failure.  Check the value of chan->_state
 * to know if the call was answered or not.
 */
struct opbx_channel *opbx_request_and_dial(const char *type, int format, void *data, int timeout, int *reason, const char *cidnum, const char *cidname);

struct opbx_channel *__opbx_request_and_dial(const char *type, int format, void *data, int timeout, int *reason, const char *cidnum, const char *cidname, struct outgoing_helper *oh);

/*! Register a channel technology */
/*! 
 * \param tech Structure defining channel technology or "type"
 * Called by a channel module to register the kind of channels it supports.
 * Returns 0 on success, -1 on failure.
 */
int opbx_channel_register(const struct opbx_channel_tech *tech);

/*! Unregister a channel technology */
/*
 * \param tech Structure defining channel technology or "type" that was previously registered
 * No return value.
 */
void opbx_channel_unregister(const struct opbx_channel_tech *tech);

/*! Get a channel technology structure by name
 * \param name name of technology to find
 * \return a pointer to the structure, or NULL if no matching technology found
 */
const struct opbx_channel_tech *opbx_get_channel_tech(const char *name);

/*! Hang up a channel  */
/*! 
 * \param chan channel to hang up
 * This function performs a hard hangup on a channel.  Unlike the soft-hangup, this function
 * performs all stream stopping, etc, on the channel that needs to end.
 * chan is no longer valid after this call.
 * Returns 0 on success, -1 on failure.
 */
int opbx_hangup(struct opbx_channel *chan);

/*! Softly hangup up a channel */
/*! 
 * \param chan channel to be soft-hung-up
 * Call the protocol layer, but don't destroy the channel structure (use this if you are trying to
 * safely hangup a channel managed by another thread.
 * \param cause	opbx hangupcause for hangup
 * Returns 0 regardless
 */
int opbx_softhangup(struct opbx_channel *chan, int cause);
/*! Softly hangup up a channel (no channel lock) 
 * \param cause	opbx hangupcause for hangup */
int opbx_softhangup_nolock(struct opbx_channel *chan, int cause);

/*! Check to see if a channel is needing hang up */
/*! 
 * \param chan channel on which to check for hang up
 * This function determines if the channel is being requested to be hung up.
 * Returns 0 if not, or 1 if hang up is requested (including time-out).
 */
int opbx_check_hangup(struct opbx_channel *chan);

/*! Compare a offset with the settings of when to hang a channel up */
/*! 
 * \param chan channel on which to check for hang up
 * \param offset offset in seconds from current time
 * \return 1, 0, or -1
 * This function compares a offset from current time with the absolute time 
 * out on a channel (when to hang up). If the absolute time out on a channel
 * is earlier than current time plus the offset, it returns 1, if the two
 * time values are equal, it return 0, otherwise, it retturn -1.
 */
int opbx_channel_cmpwhentohangup(struct opbx_channel *chan, time_t offset);

/*! Set when to hang a channel up */
/*! 
 * \param chan channel on which to check for hang up
 * \param offset offset in seconds from current time of when to hang up
 * This function sets the absolute time out on a channel (when to hang up).
 */
void opbx_channel_setwhentohangup(struct opbx_channel *chan, time_t offset);

/*! Answer a ringing call */
/*!
 * \param chan channel to answer
 * This function answers a channel and handles all necessary call
 * setup functions.
 * Returns 0 on success, -1 on failure
 */
int opbx_answer(struct opbx_channel *chan);

/*! Make a call */
/*! 
 * \param chan which channel to make the call on
 * \param addr destination of the call
 * \param timeout time to wait on for connect
 * Place a call, take no longer than timeout ms.  Returns -1 on failure, 
   0 on not enough time (does not auto matically stop ringing), and  
   the number of seconds the connect took otherwise.
   Returns 0 on success, -1 on failure
   */
int opbx_call(struct opbx_channel *chan, char *addr, int timeout);

/*! Indicates condition of channel */
/*! 
 * \param chan channel to change the indication
 * \param condition which condition to indicate on the channel
 * Indicate a condition such as OPBX_CONTROL_BUSY, OPBX_CONTROL_RINGING, or OPBX_CONTROL_CONGESTION on a channel
 * Returns 0 on success, -1 on failure
 */
int opbx_indicate(struct opbx_channel *chan, int condition);

/* Misc stuff */

/*! Wait for input on a channel */
/*! 
 * \param chan channel to wait on
 * \param ms length of time to wait on the channel
 * Wait for input on a channel for a given # of milliseconds (<0 for indefinite). 
  Returns < 0 on  failure, 0 if nothing ever arrived, and the # of ms remaining otherwise */
int opbx_waitfor(struct opbx_channel *chan, int ms);

/*! Wait for a specied amount of time, looking for hangups */
/*!
 * \param chan channel to wait for
 * \param ms length of time in milliseconds to sleep
 * Waits for a specified amount of time, servicing the channel as required.
 * returns -1 on hangup, otherwise 0.
 */
int opbx_safe_sleep(struct opbx_channel *chan, int ms);

/*! Wait for a specied amount of time, looking for hangups and a condition argument */
/*!
 * \param chan channel to wait for
 * \param ms length of time in milliseconds to sleep
 * \param cond a function pointer for testing continue condition
 * \param data argument to be passed to the condition test function
 * Waits for a specified amount of time, servicing the channel as required. If cond
 * returns 0, this function returns.
 * returns -1 on hangup, otherwise 0.
 */
int opbx_safe_sleep_conditional(struct opbx_channel *chan, int ms, int (*cond)(void*), void *data );

/*! Waits for activity on a group of channels */
/*! 
 * \param chan an array of pointers to channels
 * \param n number of channels that are to be waited upon
 * \param fds an array of fds to wait upon
 * \param nfds the number of fds to wait upon
 * \param exception exception flag
 * \param outfd fd that had activity on it
 * \param ms how long the wait was
 * Big momma function here.  Wait for activity on any of the n channels, or any of the nfds
   file descriptors.  Returns the channel with activity, or NULL on error or if an FD
   came first.  If the FD came first, it will be returned in outfd, otherwise, outfd
   will be -1 */
struct opbx_channel *opbx_waitfor_nandfds(struct opbx_channel **chan, int n, int *fds, int nfds, int *exception, int *outfd, int *ms);

/*! Waits for input on a group of channels */
/*! Wait for input on an array of channels for a given # of milliseconds. Return channel
   with activity, or NULL if none has activity.  time "ms" is modified in-place, if applicable */
struct opbx_channel *opbx_waitfor_n(struct opbx_channel **chan, int n, int *ms);

/*! Waits for input on an fd */
/*! This version works on fd's only.  Be careful with it. */
int opbx_waitfor_n_fd(int *fds, int n, int *ms, int *exception);


/*! Reads a frame */
/*!
 * \param chan channel to read a frame from
 * Read a frame.  Returns a frame, or NULL on error.  If it returns NULL, you
   best just stop reading frames and assume the channel has been
   disconnected. */
struct opbx_frame *opbx_read(struct opbx_channel *chan);

/*! Write a frame to a channel */
/*!
 * \param chan destination channel of the frame
 * \param frame frame that will be written
 * This function writes the given frame to the indicated channel.
 * It returns 0 on success, -1 on failure.
 */
int opbx_write(struct opbx_channel *chan, struct opbx_frame *frame);

/*! Write video frame to a channel */
/*!
 * \param chan destination channel of the frame
 * \param frame frame that will be written
 * This function writes the given frame to the indicated channel.
 * It returns 1 on success, 0 if not implemented, and -1 on failure.
 */
int opbx_write_video(struct opbx_channel *chan, struct opbx_frame *frame);

/* Send empty audio to prime a channel driver */
int opbx_prod(struct opbx_channel *chan);

/*! Sets read format on channel chan */
/*! 
 * \param chan channel to change
 * \param format format to change to
 * Set read format for channel to whichever component of "format" is best. 
 * Returns 0 on success, -1 on failure
 */
int opbx_set_read_format(struct opbx_channel *chan, int format);

/*! Sets write format on channel chan */
/*! 
 * \param chan channel to change
 * \param format new format for writing
 * Set write format for channel to whichever compoent of "format" is best. 
 * Returns 0 on success, -1 on failure
 */
int opbx_set_write_format(struct opbx_channel *chan, int format);

/*! Sends text to a channel */
/*! 
 * \param chan channel to act upon
 * \param text string of text to send on the channel
 * Write text to a display on a channel
 * Returns 0 on success, -1 on failure
 */
int opbx_sendtext(struct opbx_channel *chan, const char *text);

/*! Receives a text character from a channel */
/*! 
 * \param chan channel to act upon
 * \param timeout timeout in milliseconds (0 for infinite wait)
 * Read a char of text from a channel
 * Returns 0 on success, -1 on failure
 */
int opbx_recvchar(struct opbx_channel *chan, int timeout);

/*! Send a DTMF digit to a channel */
/*! 
 * \param chan channel to act upon
 * \param digit the DTMF digit to send, encoded in ASCII
 * Send a DTMF digit to a channel.
 * Returns 0 on success, -1 on failure
 */
int opbx_senddigit(struct opbx_channel *chan, char digit);

/*! Receives a text string from a channel */
/*! 
 * \param chan channel to act upon
 * \param timeout timeout in milliseconds (0 for infinite wait)
 * \return the received text, or NULL to signify failure.
 * Read a string of text from a channel
 */
char *opbx_recvtext(struct opbx_channel *chan, int timeout);

/*! Browse channels in use */
/*! 
 * \param prev where you want to start in the channel list
 * Browse the channels currently in use 
 * Returns the next channel in the list, NULL on end.
 * If it returns a channel, that channel *has been locked*!
 */
struct opbx_channel *opbx_channel_walk_locked(const struct opbx_channel *prev);

/*! Get channel by name (locks channel) */
struct opbx_channel *opbx_get_channel_by_name_locked(const char *chan);

/*! Get channel by name prefix (locks channel) */
struct opbx_channel *opbx_get_channel_by_name_prefix_locked(const char *name, const int namelen);

/*! Get channel by name prefix (locks channel) */
struct opbx_channel *opbx_walk_channel_by_name_prefix_locked(struct opbx_channel *chan, const char *name, const int namelen);

/*--- opbx_get_channel_by_exten_locked: Get channel by exten (and optionally context) and lock it */
struct opbx_channel *opbx_get_channel_by_exten_locked(const char *exten, const char *context);

/*! Waits for a digit */
/*! 
 * \param c channel to wait for a digit on
 * \param ms how many milliseconds to wait
 * Wait for a digit.  Returns <0 on error, 0 on no entry, and the digit on success. */
int opbx_waitfordigit(struct opbx_channel *c, int ms);

/* Same as above with audio fd for outputing read audio and ctrlfd to monitor for
   reading. Returns 1 if ctrlfd becomes available */
int opbx_waitfordigit_full(struct opbx_channel *c, int ms, int audiofd, int ctrlfd);

/*! Reads multiple digits */
/*! 
 * \param c channel to read from
 * \param s string to read in to.  Must be at least the size of your length
 * \param len how many digits to read (maximum)
 * \param timeout how long to timeout between digits
 * \param rtimeout timeout to wait on the first digit
 * \param enders digits to end the string
 * Read in a digit string "s", max length "len", maximum timeout between 
   digits "timeout" (-1 for none), terminated by anything in "enders".  Give them rtimeout
   for the first digit.  Returns 0 on normal return, or 1 on a timeout.  In the case of
   a timeout, any digits that were read before the timeout will still be available in s.  
   RETURNS 2 in full version when ctrlfd is available, NOT 1*/
int opbx_readstring(struct opbx_channel *c, char *s, int len, int timeout, int rtimeout, char *enders);
int opbx_readstring_full(struct opbx_channel *c, char *s, int len, int timeout, int rtimeout, char *enders, int audiofd, int ctrlfd);

/*! Report DTMF on channel 0 */
#define OPBX_BRIDGE_DTMF_CHANNEL_0		(1 << 0)		
/*! Report DTMF on channel 1 */
#define OPBX_BRIDGE_DTMF_CHANNEL_1		(1 << 1)		
/*! Return all voice frames on channel 0 */
#define OPBX_BRIDGE_REC_CHANNEL_0		(1 << 2)		
/*! Return all voice frames on channel 1 */
#define OPBX_BRIDGE_REC_CHANNEL_1		(1 << 3)		
/*! Ignore all signal frames except NULL */
#define OPBX_BRIDGE_IGNORE_SIGS			(1 << 4)		


/*! Makes two channel formats compatible */
/*! 
 * \param c0 first channel to make compatible
 * \param c1 other channel to make compatible
 * Set two channels to compatible formats -- call before opbx_channel_bridge in general .  Returns 0 on success
   and -1 if it could not be done */
int opbx_channel_make_compatible(struct opbx_channel *c0, struct opbx_channel *c1);

/*! Bridge two channels together */
/*! 
 * \param c0 first channel to bridge
 * \param c1 second channel to bridge
 * \param flags for the channels
 * \param fo destination frame(?)
 * \param rc destination channel(?)
 * Bridge two channels (c0 and c1) together.  If an important frame occurs, we return that frame in
   *rf (remember, it could be NULL) and which channel (0 or 1) in rc */
/* int opbx_channel_bridge(struct opbx_channel *c0, struct opbx_channel *c1, int flags, struct opbx_frame **fo, struct opbx_channel **rc); */
int opbx_channel_bridge(struct opbx_channel *c0,struct opbx_channel *c1,struct opbx_bridge_config *config, struct opbx_frame **fo, struct opbx_channel **rc);

/*! Weird function made for call transfers */
/*! 
 * \param original channel to make a copy of
 * \param clone copy of the original channel
 * This is a very strange and freaky function used primarily for transfer.  Suppose that
   "original" and "clone" are two channels in random situations.  This function takes
   the guts out of "clone" and puts them into the "original" channel, then alerts the
   channel driver of the change, asking it to fixup any private information (like the
   p->owner pointer) that is affected by the change.  The physical layer of the original
   channel is hung up.  */
int opbx_channel_masquerade(struct opbx_channel *original, struct opbx_channel *clone);

/*! Gives the string form of a given cause code */
/*! 
 * \param cause cause to get the description of
 * Give a name to a cause code
 * Returns the text form of the binary cause code given
 */
const char *opbx_cause2str(int state);

/*! Gives the string form of a given control frame type */
/*! 
 * \param control to get the description of
 * Give a name to a control code
 * Returns the text form of the binary control code given
 */
const char *opbx_control2str(int control);
/*! Gives the string form of a given channel state */
/*! 
 * \param state state to get the name of
 * Give a name to a state 
 * Returns the text form of the binary state given
 */
char *opbx_state2str(int state);

/*! Gives the string form of a given transfer capability */
/*!
 * \param transercapability transfercapabilty to get the name of
 * Give a name to a transfercapbility
 * See above
 * Returns the text form of the binary transfer capbility
 */
char *opbx_transfercapability2str(int transfercapability);

/* Options: Some low-level drivers may implement "options" allowing fine tuning of the
   low level channel.  See frame.h for options.  Note that many channel drivers may support
   none or a subset of those features, and you should not count on this if you want your
   callweaver application to be portable.  They're mainly useful for tweaking performance */

/*! Sets an option on a channel */
/*! 
 * \param channel channel to set options on
 * \param option option to change
 * \param data data specific to option
 * \param datalen length of the data
 * \param block blocking or not
 * Set an option on a channel (see frame.h), optionally blocking awaiting the reply 
 * Returns 0 on success and -1 on failure
 */
int opbx_channel_setoption(struct opbx_channel *channel, int option, void *data, int datalen, int block);

/*! Pick the best codec  */
/* Choose the best codec...  Uhhh...   Yah. */
extern int opbx_best_codec(int fmts);


/*! Checks the value of an option */
/*! 
 * Query the value of an option, optionally blocking until a reply is received
 * Works similarly to setoption except only reads the options.
 */
struct opbx_frame *opbx_channel_queryoption(struct opbx_channel *channel, int option, void *data, int *datalen, int block);

/*! Checks for HTML support on a channel */
/*! Returns 0 if channel does not support HTML or non-zero if it does */
int opbx_channel_supports_html(struct opbx_channel *channel);

/*! Sends HTML on given channel */
/*! Send HTML or URL on link.  Returns 0 on success or -1 on failure */
int opbx_channel_sendhtml(struct opbx_channel *channel, int subclass, const char *data, int datalen);

/*! Sends a URL on a given link */
/*! Send URL on link.  Returns 0 on success or -1 on failure */
int opbx_channel_sendurl(struct opbx_channel *channel, const char *url);

/*! Defers DTMF */
/*! Defer DTMF so that you only read things like hangups and audio.  Returns
   non-zero if channel was already DTMF-deferred or 0 if channel is just now
   being DTMF-deferred */
int opbx_channel_defer_dtmf(struct opbx_channel *chan);

/*! Undeos a defer */
/*! Undo defer.  opbx_read will return any dtmf characters that were queued */
void opbx_channel_undefer_dtmf(struct opbx_channel *chan);

/*! Initiate system shutdown -- prevents new channels from being allocated.
    If "hangup" is non-zero, all existing channels will receive soft
     hangups */
void opbx_begin_shutdown(int hangup);

/*! Cancels an existing shutdown and returns to normal operation */
void opbx_cancel_shutdown(void);

/*! Returns number of active/allocated channels */
int opbx_active_channels(void);

/*! Returns non-zero if CallWeaver is being shut down */
int opbx_shutting_down(void);

void opbx_set_callerid(struct opbx_channel *chan, const char *cidnum, const char *cidname, const char *ani);

/*! Start a tone going */
int opbx_tonepair_start(struct opbx_channel *chan, int freq1, int freq2, int duration, int vol);

/*! Stop a tone from playing */
void opbx_tonepair_stop(struct opbx_channel *chan);

/*! Play a tone pair for a given amount of time */
int opbx_tonepair(struct opbx_channel *chan, int freq1, int freq2, int duration, int vol);

/*! Automatically service a channel for us... */
int opbx_autoservice_start(struct opbx_channel *chan);

/*! Stop servicing a channel for us...  Returns -1 on error or if channel has been hungup */
int opbx_autoservice_stop(struct opbx_channel *chan);

/*!	\brief Transfer a channel (if supported).  Returns -1 on error, 0 if not supported
   and 1 if supported and requested 
	\param chan current channel
	\param dest destination extension for transfer
*/
int opbx_transfer(struct opbx_channel *chan, char *dest);

/*!	\brief  Start masquerading a channel
	XXX This is a seriously wacked out operation.  We're essentially putting the guts of
           the clone channel into the original channel.  Start by killing off the original
           channel's backend.   I'm not sure we're going to keep this function, because
           while the features are nice, the cost is very high in terms of pure nastiness. XXX
	\param chan 	Channel to masquerade
*/
int opbx_do_masquerade(struct opbx_channel *chan);

/*!	\brief Find bridged channel 
	\param chan Current channel
*/
struct opbx_channel *opbx_bridged_channel(struct opbx_channel *chan);

/*!
  \brief Inherits channel variable from parent to child channel
  \param parent Parent channel
  \param child Child channel

  Scans all channel variables in the parent channel, looking for those
  that should be copied into the child channel.
  Variables whose names begin with a single '_' are copied into the
  child channel with the prefix removed.
  Variables whose names begin with '__' are copied into the child
  channel with their names unchanged.
*/
void opbx_channel_inherit_variables(const struct opbx_channel *parent, struct opbx_channel *child);

/*!
  \brief adds a list of channel variables to a channel
  \param chan the channel
  \param vars a linked list of variables

  Variable names can be for a regular channel variable or a dialplan function
  that has the ability to be written to.
*/
void opbx_set_variables(struct opbx_channel *chan, struct opbx_variable *vars);

/* Misc. functions below */

/* If you are calling carefulwrite, it is assumed that you are calling
   it on a file descriptor that _DOES_ have NONBLOCK set.  This way,
   there is only one system call made to do a write, unless we actually
   have a need to wait.  This way, we get better performance. */
int opbx_carefulwrite(int fd, char *s, int len, int timeoutms);

/* Helper function for migrating select to poll */
static inline int opbx_fdisset(struct pollfd *pfds, int fd, int max, int *start)
{
	int x;
	for (x=start ? *start : 0;x<max;x++)
		if (pfds[x].fd == fd) {
			if (start) {
				if (x==*start)
					(*start)++;
			}
			return pfds[x].revents;
		}
	return 0;
}

#ifdef SOLARIS
static inline void timersub(struct timeval *tvend, struct timeval *tvstart, struct timeval *tvdiff)
{
	tvdiff->tv_sec = tvend->tv_sec - tvstart->tv_sec;
	tvdiff->tv_usec = tvend->tv_usec - tvstart->tv_usec;
	if (tvdiff->tv_usec < 0) {
		tvdiff->tv_sec --;
		tvdiff->tv_usec += 1000000;
	}

}
#endif

/*! Waits for activity on a group of channels */
/*! 
 * \param nfds the maximum number of file descriptors in the sets
 * \param rfds file descriptors to check for read availability
 * \param wfds file descriptors to check for write availability
 * \param efds file descriptors to check for exceptions (OOB data)
 * \param tvp timeout while waiting for events
 * This is the same as a standard select(), except it guarantees the
 * behaviour where the passed struct timeval is updated with how much
 * time was not slept while waiting for the specified events
 */
static inline int opbx_select(int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds, struct timeval *tvp)
{
#ifdef __linux__
	return select(nfds, rfds, wfds, efds, tvp);
#else
	if (tvp) {
		struct timeval tv, tvstart, tvend, tvlen;
		int res;

		tv = *tvp;
		gettimeofday(&tvstart, NULL);
		res = select(nfds, rfds, wfds, efds, tvp);
		gettimeofday(&tvend, NULL);
		timersub(&tvend, &tvstart, &tvlen);
		timersub(&tv, &tvlen, tvp);
		if (tvp->tv_sec < 0 || (tvp->tv_sec == 0 && tvp->tv_usec < 0)) {
			tvp->tv_sec = 0;
			tvp->tv_usec = 0;
		}
		return res;
	}
	else
		return select(nfds, rfds, wfds, efds, NULL);
#endif
}

#if !defined(opbx_strdupa) && defined(__GNUC__)
# define opbx_strdupa(s)									\
  (__extension__										\
    ({													\
      __const char *__old = (s);						\
      size_t __len = strlen (__old) + 1;				\
      char *__new = (char *) __builtin_alloca (__len);	\
      (char *) memcpy (__new, __old, __len);			\
    }))
#endif

#ifdef DO_CRASH
#define CRASH do { fprintf(stderr, "!! Forcing immediate crash a-la abort !!\nFile %s Line %d\n\n", __FILE__, __LINE__); *((int *)0) = 0; } while(0)
#else
#define CRASH do { } while(0)
#endif

#define CHECK_BLOCKING(c) { 	 \
							if (opbx_test_flag(c, OPBX_FLAG_BLOCKING)) {\
								opbx_log(LOG_WARNING, "Thread %ld Blocking '%s', already blocked by thread %ld in procedure %s\n", (long) pthread_self(), (c)->name, (long) (c)->blocker, (c)->blockproc); \
								CRASH; \
							} else { \
								(c)->blocker = pthread_self(); \
								(c)->blockproc = __PRETTY_FUNCTION__; \
									opbx_set_flag(c, OPBX_FLAG_BLOCKING); \
									} }

extern opbx_group_t opbx_get_group(char *s);
/* print call- and pickup groups into buffer */
extern char *opbx_print_group(char *buf, int buflen, opbx_group_t group);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_CHANNEL_H */
