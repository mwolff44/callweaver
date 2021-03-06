/*
 * (CAPI*)
 *
 * An implementation of Common ISDN API 2.0 for CallWeaver
 *
 * Copyright (C) 2005-2006 Cytronics & Melware
 *
 * Armin Schindler <armin@melware.de>
 * 
 * Reworked, but based on the work of
 * Copyright (C) 2002-2005 Junghanns.NET GmbH
 *
 * Klaus-Peter Junghanns <kapejod@ns1.jnetdns.de>
 *
 * This program is free software and may be modified and 
 * distributed under the terms of the GNU Public License.
 */
#include <sys/time.h>
#include <sys/signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/frame.h" 
#include "callweaver/channel.h"
#include "callweaver/logger.h"
#include "callweaver/module.h"
#include "callweaver/pbx.h"
#include "callweaver/config.h"
#include "callweaver/options.h"
#include "callweaver/features.h"
#include "callweaver/utils.h"
#include "callweaver/cli.h"
#include "callweaver/rtp.h"
#include "callweaver/causes.h"
#include "callweaver/strings.h"
#include "callweaver/devicestate.h"
#include "callweaver/dsp.h"
#include "callweaver/keywords.h"


#include "chan_capi20.h"
#include "chan_capi.h"
#include "chan_capi_rtp.h"
#include "c20msg.h"
#include "xlaw.h"

#define CC_VERSION "cm-cw-0.7"

/*
 * personal stuff
 */
#undef   CAPI_APPLID_UNUSED
#define  CAPI_APPLID_UNUSED 0xffffffff
unsigned capi_ApplID = CAPI_APPLID_UNUSED;

static _cword capi_MessageNumber;
static char ccdesc[] = "Common ISDN API for CallWeaver";
static const char tdesc[] = "Common ISDN API Driver (" CC_VERSION ")";
static const char channeltype[] = "CAPI";
static const struct cw_channel_tech capi_tech;

static void *command_app;
static char commandsyntax[] = "See description";
static char commandtdesc[] = "CAPI command interface.\n"
"The dial command:\n"
"Dial(CAPI/g<group>/[<callerid>:]<destination>[/<params>])\n"
"Dial(CAPI/contr<controller>/[<callerid>:]<destination>[/<params>])\n"
"Dial(CAPI/<interface-name>/[<callerid>:]<destination>[/<params>])\n"
"\"params\" can be:\n"
"early B3:\"b\"=always, \"B\"=on successful calls only\n"
"\"d\":use callerID from capi.conf, \"o\":overlap sending number\n"
"\n"
"capicommand() where () can be:\n"
"\"deflect, to_number\" forwards an unanswered call to number\n"
"\"malicous\" report a call of malicious nature\n"
"\"echocancel, <yes> or <no>\" echo-cancel provided by driver/hardware\n"
"\"echosquelch, <yes> or <no>\" very primitive echo-squelch by chan-capi\n"
"\"holdtype, <local> or <hold>\" set type of 'hold'\n"
"\"hold[, MYHOLDVAR]\" puts an answered call on hold\n"
"\"retrieve, ${MYHOLDVAR}\" gets back the held call\n"
"\"ect, ${MYHOLDVAR})\" explicit call transfer of call on hold\n"
"\"receivefax, filename, stationID, headline\" receive a CAPIfax\n"
"\"sendfax, filename.sff, stationID, headline\" send a CAPIfax\n"
"Variables set after fax receive:\n"
"FAXSTATUS     :0=OK, 1=Error\n"
"FAXREASON     :B3 disconnect reason\n"
"FAXREASONTEXT :FAXREASON as text\n"
"FAXRATE       :baud rate of fax connection\n"
"FAXRESOLUTION :0=standard, 1=high\n"
"FAXFORMAT     :0=SFF\n"
"FAXPAGES      :Number of pages received\n"
"FAXID         :ID of the remote fax machine\n"
"CallWeaver.org variables used/set by chan_capi:\n"
"BCHANNELINFO,CALLEDTON,_CALLERHOLDID,CALLINGSUBADDRESS,CALLEDSUBADDRESS\n"
"CONNECTEDNUMBER,FAXEXTEN,PRI_CAUSE,REDIRECTINGNUMBER,REDIRECTREASON\n"
"!!! for more details and samples, check the README of chan-capi !!!\n";

static char commandapp[] = "capiCommand";
static char commandsynopsis[] = "Execute special CAPI commands";

/*
 * LOCKING RULES
 * =============
 *
 * This channel driver uses several locks. One must be 
 * careful not to reverse the locking order, which will
 * lead to a so called deadlock. Here is the locking order
 * that must be followed:
 *
 * struct capi_pvt *i;
 *
 * 1. cc_mutex_lock(&i->owner->lock); **
 *
 * 2. cc_mutex_lock(&i->lock);
 *
 * 3. cc_mutex_lock(&iflock);
 * 4. cc_mutex_lock(&messagenumber_lock);
 * 5. cc_mutex_lock(&capi_put_lock);
 *
 *
 *  ** the PBX will call the callback functions with 
 *     this lock locked. This lock protects the 
 *     structure pointed to by 'i->owner'. Also note
 *     that calling some PBX functions will lock
 *     this lock!
 */

CW_MUTEX_DEFINE_STATIC(messagenumber_lock);
CW_MUTEX_DEFINE_STATIC(iflock);
CW_MUTEX_DEFINE_STATIC(capi_put_lock);
CW_MUTEX_DEFINE_STATIC(verbose_lock);

static int capi_capability = CW_FORMAT_ALAW;

static pthread_t monitor_thread = CW_PTHREADT_NULL;

static struct capi_pvt *iflist = NULL;
static struct cc_capi_controller *capi_controllers[CAPI_MAX_CONTROLLERS + 1];
static int capi_num_controllers = 0;
static unsigned int capi_counter = 0;
static unsigned long capi_used_controllers = 0;
static char *emptyid = "\0";

static struct cw_channel *chan_for_task;
static int channel_task;
#define CAPI_CHANNEL_TASK_NONE             0
#define CAPI_CHANNEL_TASK_HANGUP           1
#define CAPI_CHANNEL_TASK_SOFTHANGUP       2
#define CAPI_CHANNEL_TASK_PICKUP           3

static char capi_national_prefix[CW_MAX_EXTENSION];
static char capi_international_prefix[CW_MAX_EXTENSION];

static char default_language[MAX_LANGUAGE] = "";

static int capidebug = 0;

/* local prototypes */
static int pbx_capi_indicate(struct cw_channel *c, int condition);

/* */
#define return_on_no_interface(x)                                       \
	if (!i) {                                                       \
		cc_verbose(4, 1, "CAPI: %s no interface for PLCI=%#x\n", x, PLCI);   \
		return;                                                 \
	}

/*
 * helper for <pbx>_verbose with different verbose settings
 */
void cc_verbose(int o_v, int c_d, char *text, ...)
{
	char line[4096];
	va_list ap;

	va_start(ap, text);
	vsnprintf(line, sizeof(line), text, ap);
	va_end(ap);

	if ((o_v == 0) || (option_verbose > o_v)) {
		if ((!c_d) || ((c_d) && (capidebug))) {	
			cc_mutex_lock(&verbose_lock);
			cc_pbx_verbose(line);
			cc_mutex_unlock(&verbose_lock);	
		}
	}
}

/*
 * B protocol settings
 */
static struct {
	_cword b1protocol;
	_cword b2protocol;
	_cword b3protocol;
	_cstruct b1configuration;
	_cstruct b2configuration;
	_cstruct b3configuration;
} b_protocol_table[] =
{
	{ 0x01, 0x01, 0x00,	/* 0 */
		NULL,
		NULL,
		NULL
	},
	{ 0x04, 0x04, 0x04,	/* 1 */
		NULL,
		NULL,
		NULL
	},
	{ 0x1f, 0x1f, 0x1f,	/* 2 */
		(_cstruct) "\x00",
		/* (_cstruct) "\x04\x01\x00\x00\x02", */
		(_cstruct) "\x06\x01\x00\x58\x02\x32\x00",
		(_cstruct) "\x00"
	}
};

#ifndef CC_HAVE_NO_GLOBALCONFIGURATION
/*
 * set the global-configuration (b-channel operation)
 */
static _cstruct capi_set_global_configuration(struct capi_pvt *i)
{
	unsigned short dtedce = 0;
	unsigned char *buf = i->tmpbuf;

	buf[0] = 2; /* len */

	if (i->FaxState & CAPI_FAX_STATE_ACTIVE) {
		if ((i->outgoing) && (!(i->FaxState & CAPI_FAX_STATE_SENDMODE)))
			dtedce = 2;
		if ((!(i->outgoing)) && ((i->FaxState & CAPI_FAX_STATE_SENDMODE)))
			dtedce = 1;
	}
	write_capi_word(&buf[1], dtedce);
	if (dtedce == 0)
		buf = NULL;
	return (_cstruct)buf;
}
#endif

/*
 * command to string function
 */
static const char * capi_command_to_string(unsigned short wCmd)
{
	enum { lowest_value = CAPI_P_MIN,
	       end_value = CAPI_P_MAX,
	       range = end_value - lowest_value,
	};

#undef  CHAN_CAPI_COMMAND_DESC
#define CHAN_CAPI_COMMAND_DESC(n, ENUM, value)		\
	[CAPI_P_REQ(ENUM)-(n)]  = #ENUM "_REQ",		\
	[CAPI_P_CONF(ENUM)-(n)] = #ENUM "_CONF",	\
	[CAPI_P_IND(ENUM)-(n)]  = #ENUM "_IND",		\
	[CAPI_P_RESP(ENUM)-(n)] = #ENUM "_RESP",

	static const char * const table[range] = {
	    CAPI_COMMANDS(CHAN_CAPI_COMMAND_DESC, lowest_value)
	};

	wCmd -= lowest_value;

	if (wCmd >= range) {
	    goto error;
	}

	if (table[wCmd] == NULL) {
	    goto error;
	}
	return table[wCmd];

 error:
	return "UNDEFINED";
}

/*
 * show the text for a CAPI message info value
 */
static void show_capi_info(struct capi_pvt *i, _cword info)
{
	char *p;
	char *name = "?";
	
	if (info == 0x0000) {
		/* no error, do nothing */
		return;
	}

	if (!(p = capi_info_string((unsigned int)info))) {
		/* message not available */
		return;
	}

	if (i)
		name = i->vname;
	
	cc_verbose(3, 0, VERBOSE_PREFIX_4 "%s: CAPI INFO 0x%04x: %s\n",
		name, info, p);
	return;
}

/*
 * get a new capi message number automically
 */
_cword get_capi_MessageNumber(void)
{
	_cword mn;

	cc_mutex_lock(&messagenumber_lock);

	capi_MessageNumber++;
	if (capi_MessageNumber == 0) {
	    /* avoid zero */
	    capi_MessageNumber = 1;
	}

	mn = capi_MessageNumber;

	cc_mutex_unlock(&messagenumber_lock);

	return(mn);
}

/*
 * write a capi message to capi device
 */
MESSAGE_EXCHANGE_ERROR _capi_put_cmsg(_cmsg *CMSG)
{
	MESSAGE_EXCHANGE_ERROR error;
	
	if (cc_mutex_lock(&capi_put_lock)) {
		cc_log(CW_LOG_WARNING, "Unable to lock capi put!\n");
		return -1;
	} 
	
	error = capi20_put_cmsg(CMSG);
	
	if (cc_mutex_unlock(&capi_put_lock)) {
		cc_log(CW_LOG_WARNING, "Unable to unlock capi put!\n");
		return -1;
	}

	if (error) {
		cc_log(CW_LOG_ERROR, "CAPI error sending %s (NCCI=%#x) (error=%#x %s)\n",
			capi_cmsg2str(CMSG), (unsigned int)HEADER_CID(CMSG),
			error, capi_info_string((unsigned int)error));
	} else {
		unsigned short wCmd = HEADER_CMD(CMSG);
		if ((wCmd == CAPI_P_REQ(DATA_B3)) ||
		    (wCmd == CAPI_P_RESP(DATA_B3))) {
			cc_verbose(7, 1, "%s\n", capi_cmsg2str(CMSG));
		} else {
			cc_verbose(4, 1, "%s\n", capi_cmsg2str(CMSG));
		}
	}

	return error;
}

/*
 * wait for a specific message
 */
static MESSAGE_EXCHANGE_ERROR capi_wait_conf(struct capi_pvt *i, unsigned short wCmd)
{
	MESSAGE_EXCHANGE_ERROR error = 0;
	struct timespec abstime;
	unsigned char command, subcommand;

	subcommand = wCmd & 0xff;
	command = (wCmd & 0xff00) >> 8;
	i->waitevent = (unsigned int)wCmd;
	abstime.tv_sec = time(NULL) + 2;
	abstime.tv_nsec = 0;
	cc_verbose(4, 1, "%s: wait for %s (0x%x)\n",
		i->vname, capi_cmd2str(command, subcommand), i->waitevent);
	if (cw_cond_timedwait(&i->event_trigger, &i->lock, &abstime) != 0) {
		error = -1;
		cc_log(CW_LOG_WARNING, "%s: timed out waiting for %s\n",
			i->vname, capi_cmd2str(command, subcommand));
	} else {
		cc_verbose(4, 1, "%s: cond signal received for %s\n",
			i->vname, capi_cmd2str(command, subcommand));
	}
	return error;
}

/*
 * write a capi message and wait for CONF
 * i->lock must be held
 */
static MESSAGE_EXCHANGE_ERROR _capi_put_cmsg_wait_conf(struct capi_pvt *i, _cmsg *CMSG)
{
	MESSAGE_EXCHANGE_ERROR error;

	error = _capi_put_cmsg(CMSG);

	if (!(error)) {
		unsigned short wCmd = CAPICMD(CMSG->Command, CAPI_CONF);
		error = capi_wait_conf(i, wCmd);
	}
	return error;
}

/*
 * wait for B3 up
 */
static void capi_wait_for_b3_up(struct capi_pvt *i)
{
	struct timespec abstime;

	cc_mutex_lock(&i->lock);
	if (!(i->isdnstate & CAPI_ISDN_STATE_B3_UP)) {
		i->waitevent = CAPI_WAITEVENT_B3_UP;
		abstime.tv_sec = time(NULL) + 2;
		abstime.tv_nsec = 0;
		cc_verbose(4, 1, "%s: wait for b3 up.\n",
			i->vname);
		if (cw_cond_timedwait(&i->event_trigger, &i->lock, &abstime) != 0) {
			cc_log(CW_LOG_WARNING, "%s: timed out waiting for b3 up.\n",
				i->vname);
		} else {
			cc_verbose(4, 1, "%s: cond signal received for b3 up.\n",
				i->vname);
		}
	}
	cc_mutex_unlock(&i->lock);
}

/*
 * wait for finishing answering state
 */
static void capi_wait_for_answered(struct capi_pvt *i)
{
	struct timespec abstime;

	cc_mutex_lock(&i->lock);
	if (i->state == CAPI_STATE_ANSWERING) {
		i->waitevent = CAPI_WAITEVENT_ANSWER_FINISH;
		abstime.tv_sec = time(NULL) + 2;
		abstime.tv_nsec = 0;
		cc_verbose(4, 1, "%s: wait for finish answer.\n",
			i->vname);
		if (cw_cond_timedwait(&i->event_trigger, &i->lock, &abstime) != 0) {
			cc_log(CW_LOG_WARNING, "%s: timed out waiting for finish answer.\n",
				i->vname);
		} else {
			cc_verbose(4, 1, "%s: cond signal received for finish answer.\n",
				i->vname);
		}
	}
	cc_mutex_unlock(&i->lock);
}

/*
 * wait until fax activity has finished
 */
static void capi_wait_for_fax_finish(struct capi_pvt *i)
{
	struct timespec abstime;
	unsigned int timeout = 600; /* 10 minutes, to be sure */

	cc_mutex_lock(&i->lock);
	if (i->FaxState & CAPI_FAX_STATE_ACTIVE) {
		i->waitevent = CAPI_WAITEVENT_FAX_FINISH;
		abstime.tv_sec = time(NULL) + timeout;
		abstime.tv_nsec = 0;
		cc_verbose(4, 1, "%s: wait for finish fax (timeout %d seconds).\n",
			i->vname, timeout);
		if (cw_cond_timedwait(&i->event_trigger, &i->lock, &abstime) != 0) {
			cc_log(CW_LOG_WARNING, "%s: timed out waiting for finish fax.\n",
				i->vname);
		} else {
			cc_verbose(4, 1, "%s: cond signal received for finish fax.\n",
				i->vname);
		}
	}
	cc_mutex_unlock(&i->lock);
}

/*
 * wait some time for a new capi message
 */
static MESSAGE_EXCHANGE_ERROR capidev_check_wait_get_cmsg(_cmsg *CMSG)
{
	MESSAGE_EXCHANGE_ERROR Info;
	struct timeval tv;

 repeat:
	Info = capi_get_cmsg(CMSG, capi_ApplID);

#if (CAPI_OS_HINT == 1) || (CAPI_OS_HINT == 2)
	/*
	 * For BSD allow controller 0:
	 */
	if ((HEADER_CID(CMSG) & 0xFF) == 0) {
		HEADER_CID(CMSG) += capi_num_controllers;
 	}
#endif

	/* if queue is empty */
	if (Info == 0x1104) {
		/* try waiting a maximum of 0.100 seconds for a message */
		tv.tv_sec = 0;
		tv.tv_usec = 10000;
		
		Info = capi20_waitformessage(capi_ApplID, &tv);

		if (Info == 0x0000)
			goto repeat;
	}
	
	if ((Info != 0x0000) && (Info != 0x1104)) {
		if (capidebug) {
			cc_log(CW_LOG_DEBUG, "Error waiting for cmsg... INFO = %#x\n", Info);
		}
	}
    
	return Info;
}

/*
 * send Listen to specified controller
 */
static unsigned ListenOnController(unsigned long CIPmask, unsigned controller)
{
	MESSAGE_EXCHANGE_ERROR error;
	_cmsg CMSG;
	int waitcount = 100;

	LISTEN_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(), controller);

	LISTEN_REQ_INFOMASK(&CMSG) = 0xffff; /* lots of info ;) + early B3 connect */
		/* 0x00ff if no early B3 should be done */
		
	LISTEN_REQ_CIPMASK(&CMSG) = CIPmask;
	error = _capi_put_cmsg(&CMSG);

	if (error)
		goto done;

	while (waitcount) {
		error = capidev_check_wait_get_cmsg(&CMSG);

		if (IS_LISTEN_CONF(&CMSG)) {
			error = LISTEN_CONF_INFO(&CMSG);
			break;
		}
		usleep(20000);
		waitcount--;
	}
	if (!waitcount)
		error = 0x100F;

 done:
	return error;
}

/*
 *  TCAP -> CIP Translation Table (TransferCapability->CommonIsdnProfile)
 */
static struct {
	unsigned short tcap;
	unsigned short cip;
	unsigned char digital;
} translate_tcap2cip[] = {
	{ PRI_TRANS_CAP_SPEECH,                 CAPI_CIPI_SPEECH,		0 },
	{ PRI_TRANS_CAP_DIGITAL,                CAPI_CIPI_DIGITAL,		1 },
	{ PRI_TRANS_CAP_RESTRICTED_DIGITAL,     CAPI_CIPI_RESTRICTED_DIGITAL,	1 },
	{ PRI_TRANS_CAP_3K1AUDIO,               CAPI_CIPI_3K1AUDIO,		0 },
	{ PRI_TRANS_CAP_DIGITAL_W_TONES,        CAPI_CIPI_DIGITAL_W_TONES,	1 },
	{ PRI_TRANS_CAP_VIDEO,                  CAPI_CIPI_VIDEO,		1 }
};

static int tcap2cip(unsigned short tcap)
{
	int x;
	
	for (x = 0; x < sizeof(translate_tcap2cip) / sizeof(translate_tcap2cip[0]); x++) {
		if (translate_tcap2cip[x].tcap == tcap)
			return (int)translate_tcap2cip[x].cip;
	}
	return CAPI_CIPI_SPEECH;
}

static unsigned char tcap_is_digital(unsigned short tcap)
{
	int x;
	
	for (x = 0; x < sizeof(translate_tcap2cip) / sizeof(translate_tcap2cip[0]); x++) {
		if (translate_tcap2cip[x].tcap == tcap)
			return translate_tcap2cip[x].digital;
	}
	return 0;
}

/*
 *  CIP -> TCAP Translation Table (CommonIsdnProfile->TransferCapability)
 */
static struct {
	unsigned short cip;
	unsigned short tcap;
} translate_cip2tcap[] = {
	{ CAPI_CIPI_SPEECH,                  PRI_TRANS_CAP_SPEECH },
	{ CAPI_CIPI_DIGITAL,                 PRI_TRANS_CAP_DIGITAL },
	{ CAPI_CIPI_RESTRICTED_DIGITAL,      PRI_TRANS_CAP_RESTRICTED_DIGITAL },
	{ CAPI_CIPI_3K1AUDIO,                PRI_TRANS_CAP_3K1AUDIO },
	{ CAPI_CIPI_7KAUDIO,                 PRI_TRANS_CAP_DIGITAL_W_TONES },
	{ CAPI_CIPI_VIDEO,                   PRI_TRANS_CAP_VIDEO },
	{ CAPI_CIPI_PACKET_MODE,             PRI_TRANS_CAP_DIGITAL },
	{ CAPI_CIPI_56KBIT_RATE_ADAPTION,    PRI_TRANS_CAP_DIGITAL },
	{ CAPI_CIPI_DIGITAL_W_TONES,         PRI_TRANS_CAP_DIGITAL_W_TONES },
	{ CAPI_CIPI_TELEPHONY,               PRI_TRANS_CAP_SPEECH },
	{ CAPI_CIPI_FAX_G2_3,                PRI_TRANS_CAP_3K1AUDIO },
	{ CAPI_CIPI_FAX_G4C1,                PRI_TRANS_CAP_DIGITAL },
	{ CAPI_CIPI_FAX_G4C2_3,              PRI_TRANS_CAP_DIGITAL },
	{ CAPI_CIPI_TELETEX_PROCESSABLE,     PRI_TRANS_CAP_DIGITAL },
	{ CAPI_CIPI_TELETEX_BASIC,           PRI_TRANS_CAP_DIGITAL },
	{ CAPI_CIPI_VIDEOTEX,                PRI_TRANS_CAP_DIGITAL },
	{ CAPI_CIPI_TELEX,                   PRI_TRANS_CAP_DIGITAL },
	{ CAPI_CIPI_X400,                    PRI_TRANS_CAP_DIGITAL },
	{ CAPI_CIPI_X200,                    PRI_TRANS_CAP_DIGITAL },
	{ CAPI_CIPI_7K_TELEPHONY,            PRI_TRANS_CAP_DIGITAL_W_TONES },
	{ CAPI_CIPI_VIDEO_TELEPHONY_C1,      PRI_TRANS_CAP_DIGITAL_W_TONES },
	{ CAPI_CIPI_VIDEO_TELEPHONY_C2,      PRI_TRANS_CAP_DIGITAL }
};

static unsigned short cip2tcap(int cip)
{
	int x;
	
	for (x = 0;x < sizeof(translate_cip2tcap) / sizeof(translate_cip2tcap[0]); x++) {
		if (translate_cip2tcap[x].cip == (unsigned short)cip)
			return translate_cip2tcap[x].tcap;
	}
	return 0;
}

/*
 *  TransferCapability to String conversion
 */
static char *transfercapability2str(int transfercapability)
{
	switch(transfercapability) {
	case PRI_TRANS_CAP_SPEECH:
		return "SPEECH";
	case PRI_TRANS_CAP_DIGITAL:
		return "DIGITAL";
	case PRI_TRANS_CAP_RESTRICTED_DIGITAL:
		return "RESTRICTED_DIGITAL";
	case PRI_TRANS_CAP_3K1AUDIO:
		return "3K1AUDIO";
	case PRI_TRANS_CAP_DIGITAL_W_TONES:
		return "DIGITAL_W_TONES";
	case PRI_TRANS_CAP_VIDEO:
		return "VIDEO";
	default:
		return "UNKNOWN";
	}
}

/*
 * set task for a channel which need to be done out of lock
 * ( after the capi thread loop )
 */
static void capi_channel_task(struct cw_channel *c, int task)
{
	chan_for_task = c;
	channel_task = task;

	cc_verbose(4, 1, VERBOSE_PREFIX_4 "%s: set channel task to %d\n",
		c->name, task);
}

/*
 * Echo cancellation is for cards w/ integrated echo cancellation only
 * (i.e. Eicon active cards support it)
 */
#define EC_FUNCTION_ENABLE              1
#define EC_FUNCTION_DISABLE             2
#define EC_FUNCTION_FREEZE              3
#define EC_FUNCTION_RESUME              4
#define EC_FUNCTION_RESET               5
#define EC_OPTION_DISABLE_NEVER         0
#define EC_OPTION_DISABLE_G165          (1<<2)
#define EC_OPTION_DISABLE_G164_OR_G165  (1<<1 | 1<<2)
#define EC_DEFAULT_TAIL                 0 /* maximum */

static void capi_echo_canceller(struct cw_channel *c, int function)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);
	_cmsg CMSG;
	char buf[10];
	int ecAvail = 0;

	if ((i->isdnstate & CAPI_ISDN_STATE_DISCONNECT))
		return;

	if (((function == EC_FUNCTION_ENABLE) && (i->isdnstate & CAPI_ISDN_STATE_EC)) ||
	    ((function != EC_FUNCTION_ENABLE) && (!(i->isdnstate & CAPI_ISDN_STATE_EC)))) {
		cc_verbose(3, 1, VERBOSE_PREFIX_4 "%s: echo canceller (PLCI=%#x, function=%d) unchanged\n",
			i->vname, i->PLCI, function);
		/* nothing to do */
		return;
	}

	/* check for old echo-cancel configuration */
	if ((i->ecSelector != FACILITYSELECTOR_ECHO_CANCEL) &&
	    (capi_controllers[i->controller]->broadband)) {
		ecAvail = 1;
	}
	if ((i->ecSelector == FACILITYSELECTOR_ECHO_CANCEL) &&
	    (capi_controllers[i->controller]->echocancel)) {
		ecAvail = 1;
	}

	/* If echo cancellation is not requested or supported, don't attempt to enable it */
	if (!ecAvail || !i->doEC) {
		return;
	}

	if (tcap_is_digital(c->transfercapability)) {
		cc_verbose(3, 1, VERBOSE_PREFIX_2 "%s: No echo canceller in digital mode (PLCI=%#x)\n",
			i->vname, i->PLCI);
		return;
	}

	cc_verbose(3, 0, VERBOSE_PREFIX_2 "%s: Setting up echo canceller (PLCI=%#x, function=%d, options=%d, tail=%d)\n",
			i->vname, i->PLCI, function, i->ecOption, i->ecTail);

	FACILITY_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(), 0);
	FACILITY_REQ_PLCI(&CMSG) = i->PLCI;
	FACILITY_REQ_FACILITYSELECTOR(&CMSG) = i->ecSelector;

	memset(buf, 0, sizeof(buf));
        buf[0] = 9; /* msg size */
        write_capi_word(&buf[1], function);
	if (function == EC_FUNCTION_ENABLE) {
		buf[3] = 6; /* echo cancel param struct size */
	        write_capi_word(&buf[4], i->ecOption); /* bit field - ignore echo canceller disable tone */
		write_capi_word(&buf[6], i->ecTail);   /* Tail length, ms */
		/* buf 8 and 9 are "pre-delay lenght ms" */
		i->isdnstate |= CAPI_ISDN_STATE_EC;
	} else {
		i->isdnstate &= ~CAPI_ISDN_STATE_EC;
	}

	FACILITY_REQ_FACILITYREQUESTPARAMETER(&CMSG) = (_cstruct)buf;
        
	if (_capi_put_cmsg(&CMSG) != 0) {
		return;
	}

	return;
}

/*
 * turn on/off DTMF detection
 */
static int capi_detect_dtmf(struct cw_channel *c, int flag)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);
	MESSAGE_EXCHANGE_ERROR error;
	_cmsg CMSG;
	char buf[9];

	if ((i->isdnstate & CAPI_ISDN_STATE_DISCONNECT))
		return 0;

	if (tcap_is_digital(c->transfercapability)) {
		cc_verbose(3, 1, VERBOSE_PREFIX_2 "%s: No dtmf-detect in digital mode (PLCI=%#x)\n",
			i->vname, i->PLCI);
		return 0;
	}

	if (((flag == 1) && (i->isdnstate & CAPI_ISDN_STATE_DTMF)) ||
	    ((flag == 0) && (!(i->isdnstate & CAPI_ISDN_STATE_DTMF)))) {
		cc_verbose(3, 1, VERBOSE_PREFIX_4 "%s: dtmf (PLCI=%#x, flag=%d) unchanged\n",
			i->vname, i->PLCI, flag);
		/* nothing to do */
		return 0;
	}
	
	/* does the controller support dtmf? and do we want to use it? */
	if ((capi_controllers[i->controller]->dtmf != 1) || (i->doDTMF != 0))
		return 0;
	
	memset(buf, 0, sizeof(buf));
	cc_verbose(3, 0, VERBOSE_PREFIX_2 "%s: Setting up DTMF detector (PLCI=%#x, flag=%d)\n",
		i->vname, i->PLCI, flag);
	FACILITY_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(), 0);
	FACILITY_REQ_PLCI(&CMSG) = i->PLCI;
	FACILITY_REQ_FACILITYSELECTOR(&CMSG) = FACILITYSELECTOR_DTMF;
	buf[0] = 8; /* msg length */
	if (flag == 1) {
		write_capi_word(&buf[1], 1); /* start DTMF listen */
	} else {
		write_capi_word(&buf[1], 2); /* stop DTMF listen */
	}
	write_capi_word(&buf[3], CAPI_DTMF_DURATION);
	write_capi_word(&buf[5], CAPI_DTMF_DURATION);
	FACILITY_REQ_FACILITYREQUESTPARAMETER(&CMSG) = (_cstruct)buf;
        
	if ((error = _capi_put_cmsg(&CMSG)) != 0) {
		return error;
	}
	if (flag == 1) {
		i->isdnstate |= CAPI_ISDN_STATE_DTMF;
	} else {
		i->isdnstate &= ~CAPI_ISDN_STATE_DTMF;
	}
	return 0;
}

/*
 * queue a frame to PBX
 */
static int local_queue_frame(struct capi_pvt *i, struct cw_frame *f)
{
	struct cw_channel *chan = i->owner;
	unsigned char *wbuf;
	int wbuflen;

	if (chan == NULL) {
		cc_log(CW_LOG_ERROR, "No owner in local_queue_frame for %s\n",
			i->vname);
		return -1;
	}

	if (!(i->isdnstate & CAPI_ISDN_STATE_PBX)) {
		/* if there is no PBX running yet,
		   we don't need any frames sent */
		return -1;
	}
	if ((i->state == CAPI_STATE_DISCONNECTING) ||
	    (i->isdnstate & CAPI_ISDN_STATE_HANGUP)) {
		cc_verbose(3, 1, VERBOSE_PREFIX_4 "%s: no queue_frame in state disconnecting for %d/%d\n",
			i->vname, f->frametype, f->subclass);
		return 0;
	}

	if ((capidebug) && (f->frametype != CW_FRAME_VOICE)) {
		cw_frame_dump(i->vname, f, VERBOSE_PREFIX_3 "CAPI queue frame:");
	}

	if ((f->frametype == CW_FRAME_CONTROL) &&
	    (f->subclass == CW_CONTROL_HANGUP)) {
		i->isdnstate |= CAPI_ISDN_STATE_HANGUP;
	}

	if (i->writerfd == -1) {
		cc_log(CW_LOG_ERROR, "No writerfd in local_queue_frame for %s\n",
			i->vname);
		return -1;
	}

	if (f->frametype != CW_FRAME_VOICE)
		f->datalen = 0;

	wbuflen = sizeof(struct cw_frame) + f->datalen;
	wbuf = alloca(wbuflen);
	memcpy(wbuf, f, sizeof(struct cw_frame));
	if (f->datalen)
		memcpy(wbuf + sizeof(struct cw_frame), f->data, f->datalen);

	if (write(i->writerfd, wbuf, wbuflen) != wbuflen) {
		cc_log(CW_LOG_ERROR, "Could not write to pipe for %s\n",
			i->vname);
	}
	return 0;
}

/*
 * set a new name for this channel
 */
static void update_channel_name(struct capi_pvt *i)
{
	char name[CW_CHANNEL_NAME];

	snprintf(name, sizeof(name) - 1, "CAPI/%s/%s-%x",
		i->name, i->dnid, capi_counter++);
	cw_change_name(i->owner, name);
	cc_verbose(3, 0, VERBOSE_PREFIX_3 "%s: Updated channel name: %s\n",
			i->vname, name);
}

/*
 * send digits via INFO_REQ
 */
static int capi_send_info_digits(struct capi_pvt *i, char *digits, int len)
{
	MESSAGE_EXCHANGE_ERROR error;
	_cmsg CMSG;
	char buf[64];
	int a;
    
	memset(buf, 0, sizeof(buf));

	INFO_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(), 0);
	INFO_REQ_PLCI(&CMSG) = i->PLCI;

	if (len > (sizeof(buf) - 2))
		len = sizeof(buf) - 2;
	
	buf[0] = len + 1;
	buf[1] = 0x80;
	for (a = 0; a < len; a++) {
		buf[a + 2] = digits[a];
	}
	INFO_REQ_CALLEDPARTYNUMBER(&CMSG) = (_cstruct)buf;

	if ((error = _capi_put_cmsg(&CMSG)) != 0) {
		return error;
	}
	cc_verbose(3, 1, VERBOSE_PREFIX_4 "%s: sent CALLEDPARTYNUMBER INFO digits = '%s' (PLCI=%#x)\n",
		i->vname, buf + 2, i->PLCI);
	return 0;
}

/*
 * send a DTMF digit
 */
static int pbx_capi_send_digit(struct cw_channel *c, char digit)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);
	_cmsg CMSG;
	char buf[10];
	char did[2];
	int ret = 0;
    
	if (i == NULL) {
		cc_log(CW_LOG_ERROR, "No interface!\n");
		return -1;
	}

	memset(buf, 0, sizeof(buf));

	cc_mutex_lock(&i->lock);

	if ((c->_state == CW_STATE_DIALING) &&
	    (i->state != CAPI_STATE_DISCONNECTING)) {
		did[0] = digit;
		did[1] = 0;
		strncat(i->dnid, did, sizeof(i->dnid) - 1);
		update_channel_name(i);	
		if ((i->isdnstate & CAPI_ISDN_STATE_SETUP_ACK) &&
		    (i->doOverlap == 0)) {
			ret = capi_send_info_digits(i, &digit, 1);
		} else {
			/* if no SETUP-ACK yet, add it to the overlap list */
			strncat(i->overlapdigits, &digit, 1);
			i->doOverlap = 1;
		}
		cc_mutex_unlock(&i->lock);
		return ret;
	}

	if ((i->state == CAPI_STATE_CONNECTED) && (i->isdnstate & CAPI_ISDN_STATE_B3_UP)) {
		/* we have a real connection, so send real DTMF */
		if ((capi_controllers[i->controller]->dtmf == 0) || (i->doDTMF > 0)) {
			/* let * fake it */
			cc_mutex_unlock(&i->lock);
			return -1;
		}
		
		FACILITY_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(), 0);
		FACILITY_REQ_PLCI(&CMSG) = i->NCCI;
	        FACILITY_REQ_FACILITYSELECTOR(&CMSG) = FACILITYSELECTOR_DTMF;
        	buf[0] = 8;
	        write_capi_word(&buf[1], 3); /* send DTMF digit */
	        write_capi_word(&buf[3], CAPI_DTMF_DURATION);
	        write_capi_word(&buf[5], CAPI_DTMF_DURATION);
	        buf[7] = 1;
		buf[8] = digit;
		FACILITY_REQ_FACILITYREQUESTPARAMETER(&CMSG) = (_cstruct)buf;
        
		if ((ret = _capi_put_cmsg(&CMSG)) == 0) {
			cc_verbose(3, 0, VERBOSE_PREFIX_4 "%s: sent dtmf '%c'\n",
				i->vname, digit);
		}
	}
	cc_mutex_unlock(&i->lock);
	return ret;
}

/*
 * send ALERT to ISDN line
 */
static int pbx_capi_alert(struct cw_channel *c)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);
	_cmsg CMSG;

	if ((i->state != CAPI_STATE_INCALL) &&
	    (i->state != CAPI_STATE_DID)) {
		cc_verbose(2, 1, VERBOSE_PREFIX_3 "%s: attempting ALERT in state %d\n",
			i->vname, i->state);
		return -1;
	}
	
	ALERT_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(), 0);
	ALERT_REQ_PLCI(&CMSG) = i->PLCI;

	if (_capi_put_cmsg(&CMSG) != 0) {
		return -1;
	}

	i->state = CAPI_STATE_ALERTING;
	cw_setstate(c, CW_STATE_RING);
	
	return 0;
}

/*
 * cleanup the interface
 */
static void interface_cleanup(struct capi_pvt *i)
{
	if (!i)
		return;

	cc_verbose(2, 1, VERBOSE_PREFIX_2 "%s: Interface cleanup PLCI=%#x\n",
		i->vname, i->PLCI);

	if (i->readerfd != -1) {
		close(i->readerfd);
		i->readerfd = -1;
	}
	if (i->writerfd != -1) {
		close(i->writerfd);
		i->writerfd = -1;
	}

	i->isdnstate = 0;
	i->cause = 0;

	i->FaxState &= ~CAPI_FAX_STATE_MASK;

	i->PLCI = 0;
	i->MessageNumber = 0;
	i->NCCI = 0;
	i->onholdPLCI = 0;

	memset(i->cid, 0, sizeof(i->cid));
	memset(i->dnid, 0, sizeof(i->dnid));
	i->cid_ton = 0;

	i->rtpcodec = 0;
	if (i->rtp) {
		cw_rtp_destroy(i->rtp);
		i->rtp = NULL;
	}

	i->owner = NULL;
	return;
}

/*
 * disconnect b3 and wait for confirmation 
 */
static void cc_disconnect_b3(struct capi_pvt *i, int wait) 
{
	_cmsg CMSG;
	struct timespec abstime;

	if (!(i->isdnstate & (CAPI_ISDN_STATE_B3_UP | CAPI_ISDN_STATE_B3_PEND)))
		return;

	cc_mutex_lock(&i->lock);
	DISCONNECT_B3_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(), 0);
	DISCONNECT_B3_REQ_NCCI(&CMSG) = i->NCCI;
	_capi_put_cmsg_wait_conf(i, &CMSG);

	if (!wait) {
		cc_mutex_unlock(&i->lock);
		return;
	}
	
	/* wait for the B3 layer to go down */
	if ((i->isdnstate & (CAPI_ISDN_STATE_B3_UP | CAPI_ISDN_STATE_B3_PEND))) {
		i->waitevent = CAPI_WAITEVENT_B3_DOWN;
		abstime.tv_sec = time(NULL) + 2;
		abstime.tv_nsec = 0;
		cc_verbose(4, 1, "%s: wait for b3 down.\n",
			i->vname);
		if (cw_cond_timedwait(&i->event_trigger, &i->lock, &abstime) != 0) {
			cc_log(CW_LOG_WARNING, "%s: timed out waiting for b3 down.\n",
				i->vname);
		} else {
			cc_verbose(4, 1, "%s: cond signal received for b3 down.\n",
				i->vname);
		}
	}
	cc_mutex_unlock(&i->lock);
	if ((i->isdnstate & CAPI_ISDN_STATE_B3_UP)) {
		cc_log(CW_LOG_ERROR, "capi disconnect b3: didn't disconnect NCCI=0x%08x\n",
			i->NCCI);
	}
	return;
}

/*
 * send CONNECT_B3_REQ
 */
static void cc_start_b3(struct capi_pvt *i)
{
	_cmsg CMSG;

	if (!(i->isdnstate & (CAPI_ISDN_STATE_B3_UP | CAPI_ISDN_STATE_B3_PEND))) {
		i->isdnstate |= CAPI_ISDN_STATE_B3_PEND;
		CONNECT_B3_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(), 0);
		CONNECT_B3_REQ_PLCI(&CMSG) = i->PLCI;
		CONNECT_B3_REQ_NCPI(&CMSG) = capi_rtp_ncpi(i);
		_capi_put_cmsg(&CMSG);
		cc_verbose(4, 1, VERBOSE_PREFIX_3 "%s: sent CONNECT_B3_REQ PLCI=%#x\n",
			i->vname, i->PLCI);
	}
}

/*
 * start early B3
 */
static void start_early_b3(struct capi_pvt *i)
{
	if (i->doB3 != CAPI_B3_DONT) { 
		/* we do early B3 Connect */
		cc_start_b3(i);
	}
}

/*
 * signal 'progress' to PBX 
 */
static void send_progress(struct capi_pvt *i)
{
	struct cw_frame fr = { CW_FRAME_CONTROL, };

	start_early_b3(i);

	if (!(i->isdnstate & CAPI_ISDN_STATE_PROGRESS)) {
		i->isdnstate |= CAPI_ISDN_STATE_PROGRESS;
		fr.subclass = CW_CONTROL_PROGRESS;
		local_queue_frame(i, &fr);
	}
	return;
}

/*
 * hangup a line (CAPI messages)
 */
static void capi_activehangup(struct cw_channel *c, int state)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);
	_cmsg CMSG;
	struct cw_var_t *var;

	i->cause = c->hangupcause;
	if ((var = pbx_builtin_getvar_helper(c, CW_KEYWORD_PRI_CAUSE, "PRI_CAUSE"))) {
		i->cause = atoi(var->value);
		cw_object_put(var);
	}
	
	if ((i->isdnstate & CAPI_ISDN_STATE_ECT)) {
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: activehangup ECT call\n",
			i->vname);
		/* we do nothing, just wait for DISCONNECT_IND */
		return;
	}

	cc_verbose(2, 1, VERBOSE_PREFIX_3 "%s: activehangingup (cause=%d) for PLCI=%#x\n",
		i->vname, i->cause, i->PLCI);


	if ((state == CAPI_STATE_ALERTING) ||
	    (state == CAPI_STATE_DID) || (state == CAPI_STATE_INCALL)) {
		CONNECT_RESP_HEADER(&CMSG, capi_ApplID, i->MessageNumber, 0);
		CONNECT_RESP_PLCI(&CMSG) = i->PLCI;
		CONNECT_RESP_REJECT(&CMSG) = (i->cause) ? (0x3480 | (i->cause & 0x7f)) : 2;
		_capi_put_cmsg(&CMSG);
		return;
	}

	/* active disconnect */
	if ((i->isdnstate & CAPI_ISDN_STATE_B3_UP)) {
		cc_disconnect_b3(i, 0);
		return;
	}
	
	if ((state == CAPI_STATE_CONNECTED) || (state == CAPI_STATE_CONNECTPENDING) ||
	    (state == CAPI_STATE_ANSWERING) || (state == CAPI_STATE_ONHOLD)) {
		cc_mutex_lock(&i->lock);
		if (i->PLCI == 0) {
			/* CONNECT_CONF not received yet? */
			capi_wait_conf(i, CAPI_CONNECT_CONF);
		}
		DISCONNECT_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(), 0);
		DISCONNECT_REQ_PLCI(&CMSG) = i->PLCI;
		_capi_put_cmsg_wait_conf(i, &CMSG);
		cc_mutex_unlock(&i->lock);
	}
	return;
}

/*
 * PBX tells us to hangup a line
 */
static int pbx_capi_hangup(struct cw_channel *c)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);
	int cleanup = 0;
	int state;

	/*
	 * hmm....ok...this is called to free the capi interface (passive disconnect)
	 * or to bring down the channel (active disconnect)
	 */

	if (i == NULL) {
		cc_log(CW_LOG_ERROR, "channel has no interface!\n");
		return -1;
	}

	cc_mutex_lock(&i->lock);

	state = i->state;

	cc_verbose(3, 0, VERBOSE_PREFIX_2 "%s: CAPI Hangingup for PLCI=%#x in state %d\n",
		i->vname, i->PLCI, state);
 
	/* are we down, yet? */
	if (state != CAPI_STATE_DISCONNECTED) {
		/* no */
		i->state = CAPI_STATE_DISCONNECTING;
	} else {
		cleanup = 1;
	}
	
	if ((i->doDTMF > 0) && (i->vad != NULL)) {
		cw_dsp_free(i->vad);
		i->vad = NULL;
	}
	
	if (cleanup) {
		/* disconnect already done, so cleanup */
		interface_cleanup(i);
	}
	cc_mutex_unlock(&i->lock);

	if (!cleanup) {
		/* not disconnected yet, we must actively do it */
		capi_activehangup(c, state);
	}

	CC_CHANNEL_PVT(c) = NULL;
	cw_setstate(c, CW_STATE_DOWN);

	return 0;
}

/*
 * convert a number
 */
static char *capi_number_func(unsigned char *data, unsigned int strip, char *buf)
{
	unsigned int len;

	if (data[0] == 0xff) {
		len = read_capi_word(&data[1]);
		data += 2;
	} else {
		len = data[0];
		data += 1;
	}
	if (len > (CW_MAX_EXTENSION - 1))
		len = (CW_MAX_EXTENSION - 1);
	
	/* convert a capi struct to a \0 terminated string */
	if ((!len) || (len < strip))
		return NULL;
		
	len = len - strip;
	data += strip;

	memcpy(buf, data, len);
	buf[len] = '\0';
	
	return buf;
}
#define capi_number(data, strip) ({ \
  char *s = alloca(CW_MAX_EXTENSION); \
  capi_number_func(data, strip, s); \
})

/*
 * parse the dialstring
 */
static void parse_dialstring(char *buffer, char **interface, char **dest, char **param, char **ocid)
{
	int cp = 0;
	char *buffer_p = buffer;
	char *oc;

	/* interface is the first part of the string */
	*interface = buffer;

	*dest = emptyid;
	*param = emptyid;
	*ocid = NULL;

	while (*buffer_p) {
		if (*buffer_p == '/') {
			*buffer_p = 0;
			buffer_p++;
			if (cp == 0) {
				*dest = buffer_p;
				cp++;
			} else if (cp == 1) {
				*param = buffer_p;
				cp++;
			} else {
				cc_log(CW_LOG_WARNING, "Too many parts in dialstring '%s'\n",
					buffer);
			}
			continue;
		}
		buffer_p++;
	}
	if ((oc = strchr(*dest, ':')) != NULL) {
		*ocid = *dest;
		*oc = '\0';
		*dest = oc + 1;
	}
	cc_verbose(3, 1, VERBOSE_PREFIX_4 "parsed dialstring: '%s' '%s' '%s' '%s'\n",
		*interface, (*ocid) ? *ocid : "NULL", *dest, *param);
	return;
}

/*
 * PBX tells us to make a call
 */
static int pbx_capi_call(struct cw_channel *c, const char *idest)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);
	char *dest, *interface, *param, *ocid;
	char buffer[CW_MAX_EXTENSION];
	char called[CW_MAX_EXTENSION], calling[CW_MAX_EXTENSION];
	char callerid[CW_MAX_EXTENSION];
	char bchaninfo[3];
	int CLIR;
	int callernplan = 0;
	int use_defaultcid = 0;
	char *osa = NULL;
	char *dsa = NULL;
	char callingsubaddress[CW_MAX_EXTENSION];
	char calledsubaddress[CW_MAX_EXTENSION];
	struct cw_var_t *var;
	
	_cmsg CMSG;
	MESSAGE_EXCHANGE_ERROR  error;

	cc_copy_string(buffer, idest, sizeof(buffer));
	parse_dialstring(buffer, &interface, &dest, &param, &ocid);

	/* init param settings */
	i->doB3 = CAPI_B3_DONT;
	i->doOverlap = 0;
	memset(i->overlapdigits, 0, sizeof(i->overlapdigits));

	/* parse the parameters */
	while ((param) && (*param)) {
		switch (*param) {
		case 'b':	/* always B3 */
			if (i->doB3 != CAPI_B3_DONT)
				cc_log(CW_LOG_WARNING, "B3 already set in '%s'\n", idest);
			i->doB3 = CAPI_B3_ALWAYS;
			break;
		case 'B':	/* only do B3 on successfull calls */
			if (i->doB3 != CAPI_B3_DONT)
				cc_log(CW_LOG_WARNING, "B3 already set in '%s'\n", idest);
			i->doB3 = CAPI_B3_ON_SUCCESS;
			break;
		case 'o':	/* overlap sending of digits */
			if (i->doOverlap)
				cc_log(CW_LOG_WARNING, "Overlap already set in '%s'\n", idest);
			i->doOverlap = 1;
			break;
		case 'd':	/* use default cid */
			if (use_defaultcid)
				cc_log(CW_LOG_WARNING, "Default CID already set in '%s'\n", idest);
			use_defaultcid = 1;
			break;
		default:
			cc_log(CW_LOG_WARNING, "Unknown parameter '%c' in '%s', ignoring.\n",
				*param, idest);
		}
		param++;
	}
	if (((!dest) || (!dest[0])) && (i->doB3 != CAPI_B3_ALWAYS)) {
		cc_log(CW_LOG_ERROR, "No destination or dialtone requested in '%s'\n", idest);
		return -1;
	}

	CLIR = c->cid.cid_pres;
	callernplan = c->cid.cid_ton & 0x7f;

	if ((var = pbx_builtin_getvar_helper(c, CW_KEYWORD_CALLERTON, "CALLERTON"))) {
		callernplan = atoi(var->value) & 0x7f;
		cw_object_put(var);
	}
	cc_verbose(1, 1, VERBOSE_PREFIX_2 "%s: Call %s %s%s (pres=0x%02x, ton=0x%02x)\n",
		i->vname, c->name, i->doB3 ? "with B3 ":" ",
		i->doOverlap ? "overlap":"", CLIR, callernplan);

	if ((var = pbx_builtin_getvar_helper(c, CW_KEYWORD_CALLINGSUBADDRESS, "CALLINGSUBADDRESS"))) {
		callingsubaddress[0] = strlen(var->value) + 1;
		callingsubaddress[1] = 0x80;
		strncpy(&callingsubaddress[2], var->value, sizeof(callingsubaddress) - 3);
		osa = callingsubaddress;
		cw_object_put(var);
	}
	if ((var = pbx_builtin_getvar_helper(c, CW_KEYWORD_CALLEDSUBADDRESS, "CALLEDSUBADDRESS"))) {
		calledsubaddress[0] = strlen(var->value) + 1;
		calledsubaddress[1] = 0x80;
		strncpy(&calledsubaddress[2], var->value, sizeof(calledsubaddress) - 3);
		dsa = calledsubaddress;
		cw_object_put(var);
	}

	i->MessageNumber = get_capi_MessageNumber();
	CONNECT_REQ_HEADER(&CMSG, capi_ApplID, i->MessageNumber, i->controller);
	CONNECT_REQ_CONTROLLER(&CMSG) = i->controller;
	CONNECT_REQ_CIPVALUE(&CMSG) = tcap2cip(c->transfercapability);
	if (tcap_is_digital(c->transfercapability)) {
		i->bproto = CC_BPROTO_TRANSPARENT;
		cc_verbose(4, 0, VERBOSE_PREFIX_2 "%s: is digital call, set proto to TRANSPARENT\n",
			i->vname);
	}
	if ((i->doOverlap) && (strlen(dest))) {
		cc_copy_string(i->overlapdigits, dest, sizeof(i->overlapdigits));
		called[0] = 1;
	} else {
		i->doOverlap = 0;
		called[0] = strlen(dest) + 1;
	}
	called[1] = 0x80;
	strncpy(&called[2], dest, sizeof(called) - 3);
	CONNECT_REQ_CALLEDPARTYNUMBER(&CMSG) = (_cstruct)called;
	CONNECT_REQ_CALLEDPARTYSUBADDRESS(&CMSG) = (_cstruct)dsa;

	if (c->cid.cid_num) {
		cc_copy_string(callerid, c->cid.cid_num, sizeof(callerid));
	} else {
		memset(callerid, 0, sizeof(callerid));
	}

	if (use_defaultcid) {
		cc_copy_string(callerid, i->defaultcid, sizeof(callerid));
	} else if (ocid) {
		cc_copy_string(callerid, ocid, sizeof(callerid));
	}
	cc_copy_string(i->cid, callerid, sizeof(i->cid));

	calling[0] = strlen(callerid) + 2;
	calling[1] = callernplan;
	calling[2] = 0x80 | (CLIR & 0x63);
	strncpy(&calling[3], callerid, sizeof(calling) - 4);

	CONNECT_REQ_CALLINGPARTYNUMBER(&CMSG) = (_cstruct)calling;
	CONNECT_REQ_CALLINGPARTYSUBADDRESS(&CMSG) = (_cstruct)osa;

	CONNECT_REQ_B1PROTOCOL(&CMSG) = b_protocol_table[i->bproto].b1protocol;
	CONNECT_REQ_B2PROTOCOL(&CMSG) = b_protocol_table[i->bproto].b2protocol;
	CONNECT_REQ_B3PROTOCOL(&CMSG) = b_protocol_table[i->bproto].b3protocol;
	CONNECT_REQ_B1CONFIGURATION(&CMSG) = b_protocol_table[i->bproto].b1configuration;
	CONNECT_REQ_B2CONFIGURATION(&CMSG) = b_protocol_table[i->bproto].b2configuration;
	CONNECT_REQ_B3CONFIGURATION(&CMSG) = b_protocol_table[i->bproto].b3configuration;

	bchaninfo[0] = 2;
	bchaninfo[1] = 0x0;
	bchaninfo[2] = 0x0;
	CONNECT_REQ_BCHANNELINFORMATION(&CMSG) = (_cstruct)bchaninfo; /* 0 */

	cc_mutex_lock(&i->lock);

	i->outgoing = 1;
	i->isdnstate |= CAPI_ISDN_STATE_PBX;
	i->state = CAPI_STATE_CONNECTPENDING;
	cw_setstate(c, CW_STATE_DIALING);

	if ((error = _capi_put_cmsg(&CMSG))) {
		i->state = CAPI_STATE_DISCONNECTED;
		cw_setstate(c, CW_STATE_RESERVED);
		cc_mutex_unlock(&i->lock);
		return error;
	}
	cc_mutex_unlock(&i->lock);

	/* now we shall return .... the rest has to be done by handle_msg */
	return 0;
}

/*
 * answer a capi call
 */
static int capi_send_answer(struct cw_channel *c, _cstruct b3conf)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);
	_cmsg CMSG;
	char buf[CAPI_MAX_STRING];
	struct cw_var_t *connectednumber;
	const char *dnid;
    
	if ((i->isdnmode == CAPI_ISDNMODE_DID) &&
	    ((strlen(i->incomingmsn) < strlen(i->dnid)) && 
	    (strcmp(i->incomingmsn, "*")))) {
		dnid = i->dnid + strlen(i->incomingmsn);
	} else {
		dnid = i->dnid;
	}
	if ((connectednumber = pbx_builtin_getvar_helper(c, CW_KEYWORD_CONNECTEDNUMBER, "CONNECTEDNUMBER")))
		dnid = connectednumber->value;

	CONNECT_RESP_HEADER(&CMSG, capi_ApplID, i->MessageNumber, 0);
	CONNECT_RESP_PLCI(&CMSG) = i->PLCI;
	CONNECT_RESP_REJECT(&CMSG) = 0;
	if (strlen(dnid)) {
		buf[0] = strlen(dnid) + 2;
		buf[1] = 0x00;
		buf[2] = 0x80;
		strncpy(&buf[3], dnid, sizeof(buf) - 4);
		CONNECT_RESP_CONNECTEDNUMBER(&CMSG) = (_cstruct)buf;
	}
	CONNECT_RESP_B1PROTOCOL(&CMSG) = b_protocol_table[i->bproto].b1protocol;
	CONNECT_RESP_B2PROTOCOL(&CMSG) = b_protocol_table[i->bproto].b2protocol;
	CONNECT_RESP_B3PROTOCOL(&CMSG) = b_protocol_table[i->bproto].b3protocol;
	CONNECT_RESP_B1CONFIGURATION(&CMSG) = b_protocol_table[i->bproto].b1configuration;
	CONNECT_RESP_B2CONFIGURATION(&CMSG) = b_protocol_table[i->bproto].b2configuration;
	if (!b3conf)
		b3conf = b_protocol_table[i->bproto].b3configuration;
	CONNECT_RESP_B3CONFIGURATION(&CMSG) = b3conf;
#ifndef CC_HAVE_NO_GLOBALCONFIGURATION
	CONNECT_RESP_GLOBALCONFIGURATION(&CMSG) = capi_set_global_configuration(i);
#endif

	cc_verbose(3, 0, VERBOSE_PREFIX_2 "%s: Answering for %s\n",
		i->vname, dnid);

	if (connectednumber)
		cw_object_put(connectednumber);

	if (_capi_put_cmsg(&CMSG) != 0) {
		return -1;	
	}
    
	i->state = CAPI_STATE_ANSWERING;
	i->doB3 = CAPI_B3_DONT;
	i->outgoing = 0;

	return 0;
}

/*
 * PBX tells us to answer a call
 */
static int pbx_capi_answer(struct cw_channel *c)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);
	int ret;

	i->bproto = CC_BPROTO_TRANSPARENT;

	if (i->rtp) {
		if (!tcap_is_digital(c->transfercapability))
			i->bproto = CC_BPROTO_RTP;
	}

	ret = capi_send_answer(c, NULL);
	return ret;
}

/*
 * read for a channel
 */
static struct cw_frame *pbx_capi_read(struct cw_channel *c) 
{
        struct capi_pvt *i = CC_CHANNEL_PVT(c);
	struct cw_frame *f;
	int readsize;

	if (i == NULL) {
		cc_log(CW_LOG_ERROR, "channel has no interface\n");
		return NULL;
	}
	if (i->readerfd == -1) {
		cc_log(CW_LOG_ERROR, "no readerfd\n");
		return NULL;
	}

	f = &i->f;
	f->frametype = CW_FRAME_NULL;
	f->subclass = 0;

	readsize = read(i->readerfd, f, sizeof(struct cw_frame));
	if (readsize != sizeof(struct cw_frame)) {
		cc_log(CW_LOG_ERROR, "did not read a whole frame\n");
	}
	
	f->mallocd = 0;
	f->data = NULL;

	if ((f->frametype == CW_FRAME_CONTROL) && (f->subclass == CW_CONTROL_HANGUP)) {
		return NULL;
	}

	if ((f->frametype == CW_FRAME_VOICE) && (f->datalen > 0)) {
		if (f->datalen > sizeof(i->frame_data)) {
			cc_log(CW_LOG_ERROR, "f.datalen(%d) greater than space of frame_data(%d)\n",
				f->datalen, sizeof(i->frame_data));
			f->datalen = sizeof(i->frame_data);
		}
		readsize = read(i->readerfd, i->frame_data + CW_FRIENDLY_OFFSET, f->datalen);
		if (readsize != f->datalen) {
			cc_log(CW_LOG_ERROR, "did not read whole frame data\n");
		}
		f->data = i->frame_data + CW_FRIENDLY_OFFSET;
		if ((i->doDTMF > 0) && (i->vad != NULL) ) {
			f = cw_dsp_process(c, i->vad, f);
		}
	}
	return f;
}

/*
 * PBX tells us to write for a channel
 */
static int pbx_capi_write(struct cw_channel *c, struct cw_frame *f)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);
	MESSAGE_EXCHANGE_ERROR error;
	_cmsg CMSG;
	int j = 0;
	unsigned char *buf;
	struct cw_frame *fsmooth;
	int txavg=0;
	int ret = 0;

	if (!i) {
		cc_log(CW_LOG_ERROR, "channel has no interface\n");
		return -1;
	}
	 
	if ((!(i->isdnstate & CAPI_ISDN_STATE_B3_UP)) || (!i->NCCI) ||
	    ((i->isdnstate & (CAPI_ISDN_STATE_B3_CHANGE | CAPI_ISDN_STATE_LI)))) {
		return 0;
	}

	if ((!(i->ntmode)) && (i->state != CAPI_STATE_CONNECTED)) {
		return 0;
	}

	if (f->frametype == CW_FRAME_NULL) {
		return 0;
	}
	if (f->frametype == CW_FRAME_DTMF) {
		cc_log(CW_LOG_ERROR, "dtmf frame should be written\n");
		return 0;
	}
	if (f->frametype != CW_FRAME_VOICE) {
		cc_log(CW_LOG_ERROR,"not a voice frame\n");
		return 0;
	}
	if (i->FaxState & CAPI_FAX_STATE_ACTIVE) {
		cc_verbose(3, 1, VERBOSE_PREFIX_2 "%s: write on fax activity?\n",
			i->vname);
		return 0;
	}
	if ((!f->data) || (!f->datalen)) {
		cc_log(CW_LOG_DEBUG, "No data for FRAME_VOICE %s\n", c->name);
		return 0;
	}
	if (i->isdnstate & CAPI_ISDN_STATE_RTP) {
		if ((!(f->subclass & i->codec)) &&
		    (f->subclass != capi_capability)) {
			cc_log(CW_LOG_ERROR, "don't know how to write subclass %s(%d)\n",
				cw_getformatname(f->subclass), f->subclass);
			return 0;
		}
		return capi_write_rtp(c, f);
	}

	if ((!i->smoother) || (cw_smoother_feed(i->smoother, f) != 0)) {
		cc_log(CW_LOG_ERROR, "%s: failed to fill smoother\n", i->vname);
		return 0;
	}

	for (fsmooth = cw_smoother_read(i->smoother);
	     fsmooth != NULL;
	     fsmooth = cw_smoother_read(i->smoother)) {
		DATA_B3_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(), 0);
		DATA_B3_REQ_NCCI(&CMSG) = i->NCCI;
		DATA_B3_REQ_DATALENGTH(&CMSG) = fsmooth->datalen;
		DATA_B3_REQ_FLAGS(&CMSG) = 0; 

		DATA_B3_REQ_DATAHANDLE(&CMSG) = i->send_buffer_handle;
		buf = &(i->send_buffer[(i->send_buffer_handle % CAPI_MAX_B3_BLOCKS) *
			(CAPI_MAX_B3_BLOCK_SIZE + CW_FRIENDLY_OFFSET)]);
		DATA_B3_REQ_DATA(&CMSG) = buf;
		i->send_buffer_handle++;

		if ((i->doES == 1) && (!tcap_is_digital(c->transfercapability))) {
			for (j = 0; j < fsmooth->datalen; j++) {
				buf[j] = reversebits[ ((unsigned char *)fsmooth->data)[j] ]; 
				if (capi_capability == CW_FORMAT_ULAW) {
					txavg += abs( capiULAW2INT[reversebits[ ((unsigned char*)fsmooth->data)[j]]] );
				} else {
					txavg += abs( capiALAW2INT[reversebits[ ((unsigned char*)fsmooth->data)[j]]] );
				}
			}
			txavg = txavg / j;
			for(j = 0; j < ECHO_TX_COUNT - 1; j++) {
				i->txavg[j] = i->txavg[j+1];
			}
			i->txavg[ECHO_TX_COUNT - 1] = txavg;
		} else {
			if ((i->txgain == 1.0) || (!tcap_is_digital(c->transfercapability))) {
				for (j = 0; j < fsmooth->datalen; j++) {
					buf[j] = reversebits[((unsigned char *)fsmooth->data)[j]];
				}
			} else {
				for (j = 0; j < fsmooth->datalen; j++) {
					buf[j] = i->g.txgains[reversebits[((unsigned char *)fsmooth->data)[j]]];
				}
			}
		}
   
   		error = 1; 
		if (i->B3q > 0) {
			error = _capi_put_cmsg(&CMSG);
		} else {
			cc_verbose(3, 1, VERBOSE_PREFIX_4 "%s: too much voice to send for NCCI=%#x\n",
				i->vname, i->NCCI);
		}

		if (!error) {
			cc_mutex_lock(&i->lock);
			i->B3q -= fsmooth->datalen;
			if (i->B3q < 0)
				i->B3q = 0;
			cc_mutex_unlock(&i->lock);
		}
	}
	return ret;
}

/*
 * new channel (masq)
 */
static int pbx_capi_fixup(struct cw_channel *oldchan, struct cw_channel *newchan)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(newchan);

	cc_verbose(3, 1, VERBOSE_PREFIX_2 "%s: %s fixup now %s\n",
		i->vname, oldchan->name, newchan->name);

	cc_mutex_lock(&i->lock);
	i->owner = newchan;
	cc_mutex_unlock(&i->lock);
	return 0;
}

/*
 * activate (another B protocol)
 */
static void cc_select_b(struct capi_pvt *i, _cstruct b3conf)
{
	_cmsg CMSG;

	SELECT_B_PROTOCOL_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(), 0);
	SELECT_B_PROTOCOL_REQ_PLCI(&CMSG) = i->PLCI;
	SELECT_B_PROTOCOL_REQ_B1PROTOCOL(&CMSG) = b_protocol_table[i->bproto].b1protocol;
	SELECT_B_PROTOCOL_REQ_B2PROTOCOL(&CMSG) = b_protocol_table[i->bproto].b2protocol;
	SELECT_B_PROTOCOL_REQ_B3PROTOCOL(&CMSG) = b_protocol_table[i->bproto].b3protocol;
	SELECT_B_PROTOCOL_REQ_B1CONFIGURATION(&CMSG) = b_protocol_table[i->bproto].b1configuration;
	SELECT_B_PROTOCOL_REQ_B2CONFIGURATION(&CMSG) = b_protocol_table[i->bproto].b2configuration;
	if (!b3conf)
		b3conf = b_protocol_table[i->bproto].b3configuration;
	SELECT_B_PROTOCOL_REQ_B3CONFIGURATION(&CMSG) = b3conf;
#ifndef CC_HAVE_NO_GLOBALCONFIGURATION
	SELECT_B_PROTOCOL_REQ_GLOBALCONFIGURATION(&CMSG) = capi_set_global_configuration(i);
#endif

	_capi_put_cmsg(&CMSG);
}

/*
 * do line initerconnect
 */
static int line_interconnect(struct capi_pvt *i0, struct capi_pvt *i1, int start)
{
	_cmsg CMSG;
	char buf[20];

	if ((i0->isdnstate & CAPI_ISDN_STATE_DISCONNECT) ||
	    (i1->isdnstate & CAPI_ISDN_STATE_DISCONNECT))
		return -1;

	if ((!(i0->isdnstate & CAPI_ISDN_STATE_B3_UP)) || 
	    (!(i1->isdnstate & CAPI_ISDN_STATE_B3_UP))) {
		cc_verbose(3, 1, VERBOSE_PREFIX_2
			"%s:%s line interconnect aborted, at least "
			"one channel is not connected.\n",
			i0->vname, i1->vname);
		return -1;
	}

	FACILITY_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(), 0);
	FACILITY_REQ_PLCI(&CMSG) = i0->PLCI;
	FACILITY_REQ_FACILITYSELECTOR(&CMSG) = FACILITYSELECTOR_LINE_INTERCONNECT;

	memset(buf, 0, sizeof(buf));

	if (start) {
		/* connect */
		buf[0] = 17; /* msg size */
		write_capi_word(&buf[1], 0x0001);
		buf[3] = 14; /* struct size LI Request Parameter */
		write_capi_dword(&buf[4], 0x00000000); /* Data Path */
		buf[8] = 9; /* struct size */
		buf[9] = 8; /* struct size LI Request Connect Participant */
		write_capi_dword(&buf[10], i1->PLCI);
		write_capi_dword(&buf[14], 0x00000003); /* Data Path Participant */
	} else {
		/* disconnect */
		buf[0] = 7; /* msg size */
		write_capi_word(&buf[1], 0x0002);
		buf[3] = 4; /* struct size */
		write_capi_dword(&buf[4], i1->PLCI);
	}

	FACILITY_REQ_FACILITYREQUESTPARAMETER(&CMSG) = (_cstruct)buf;
        
	_capi_put_cmsg(&CMSG);

	if (start) {
		i0->isdnstate |= CAPI_ISDN_STATE_LI;
		i1->isdnstate |= CAPI_ISDN_STATE_LI;
	} else {
		i0->isdnstate &= ~CAPI_ISDN_STATE_LI;
		i1->isdnstate &= ~CAPI_ISDN_STATE_LI;
	}
	return 0;
}

#if 0
/*
 * disconnect b3 and bring it up with another protocol
 */
static void cc_switch_b_protocol(struct capi_pvt *i)
{
	int waitcount = 200;

	cc_disconnect_b3(i, 1);

	i->isdnstate |= CAPI_ISDN_STATE_B3_CHANGE;
	cc_select_b(i, NULL);

	if (i->outgoing) {
		/* on outgoing call we must do the connect-b3 request */
		cc_start_b3(i);
	}

	/* wait for the B3 layer to come up */
	while ((waitcount > 0) &&
	       (!(i->isdnstate & CAPI_ISDN_STATE_B3_UP))) {
		usleep(10000);
		waitcount--;
	}
	if (!(i->isdnstate & CAPI_ISDN_STATE_B3_UP)) {
		cc_log(CW_LOG_ERROR, "capi switch b3: no b3 up\n");
	}
}

/*
 * set the b3 protocol to transparent
 */
static int cc_set_transparent(struct capi_pvt *i)
{
	if (i->bproto != CC_BPROTO_RTP) {
		/* nothing to do */
		return 0;
	}

	i->bproto = CC_BPROTO_TRANSPARENT;
	cc_switch_b_protocol(i);

	return 1;
}

/*
 * set the b3 protocol to RTP (if wanted)
 */
static void cc_unset_transparent(struct capi_pvt *i, int rtpwanted)
{
	if ((!rtpwanted) ||
	    (i->isdnstate & CAPI_ISDN_STATE_DISCONNECT))
		return;

	i->bproto = CC_BPROTO_RTP;
	cc_switch_b_protocol(i);

	return;
}
#endif

/*
 * native bridging / line interconnect
 */
static CC_BRIDGE_RETURN pbx_capi_bridge(struct cw_channel *c0,
                                    struct cw_channel *c1,
                                    int flags, struct cw_frame **fo,
				    struct cw_channel **rc,
                                    int timeoutms)
{
	struct capi_pvt *i0 = CC_CHANNEL_PVT(c0);
	struct capi_pvt *i1 = CC_CHANNEL_PVT(c1);
	CC_BRIDGE_RETURN ret = CW_BRIDGE_COMPLETE;

	cc_verbose(3, 1, VERBOSE_PREFIX_2 "%s:%s Requested native bridge for %s and %s\n",
		i0->vname, i1->vname, c0->name, c1->name);

	if ((!i0->bridge) || (!i1->bridge))
		return CW_BRIDGE_FAILED_NOWARN;

	if ((!capi_controllers[i0->controller]->lineinterconnect) ||
	    (!capi_controllers[i1->controller]->lineinterconnect)) {
		return CW_BRIDGE_FAILED_NOWARN;
	}

	if ((i0->isdnstate & CAPI_ISDN_STATE_ECT) ||
	    (i0->isdnstate & CAPI_ISDN_STATE_ECT)) {
		return CW_BRIDGE_FAILED;
	}
	
	capi_wait_for_b3_up(i0);
	capi_wait_for_b3_up(i1);

	if (!(flags & CW_BRIDGE_DTMF_CHANNEL_0))
		capi_detect_dtmf(i0->owner, 0);

	if (!(flags & CW_BRIDGE_DTMF_CHANNEL_1))
		capi_detect_dtmf(i1->owner, 0);

	capi_echo_canceller(i0->owner, EC_FUNCTION_DISABLE);
	capi_echo_canceller(i1->owner, EC_FUNCTION_DISABLE);

	if (line_interconnect(i0, i1, 1)) {
		ret = CW_BRIDGE_FAILED;
		goto return_from_bridge;
	}

	for (;;) {
		struct cw_channel *c0_priority[2] = {c0, c1};
		struct cw_channel *c1_priority[2] = {c1, c0};
		int priority = 0;
		struct cw_frame *f;
		struct cw_channel *who;

		who = cw_waitfor_n(priority ? c0_priority : c1_priority, 2, &timeoutms);
		if (!who) {
			if (!timeoutms) {
				ret = CW_BRIDGE_RETRY;
				break;
			}
			continue;
		}
		f = cw_read(who);
		if (!f || (f->frametype == CW_FRAME_CONTROL)
		       || (f->frametype == CW_FRAME_DTMF)) {
			*fo = f;
			*rc = who;
			ret = CW_BRIDGE_COMPLETE;
			break;
		}
		if (who == c0) {
			cw_write(c1, &f);
		} else {
			cw_write(c0, &f);
		}
		cw_fr_free(f);

		/* Swap who gets priority */
		priority = !priority;
	}

	line_interconnect(i0, i1, 0);

return_from_bridge:

	if (!(flags & CW_BRIDGE_DTMF_CHANNEL_0))
		capi_detect_dtmf(i0->owner, 1);

	if (!(flags & CW_BRIDGE_DTMF_CHANNEL_1))
		capi_detect_dtmf(i1->owner, 1);

	capi_echo_canceller(i0->owner, EC_FUNCTION_ENABLE);
	capi_echo_canceller(i1->owner, EC_FUNCTION_ENABLE);

	return ret;
}

/*
 * a new channel is needed
 */
static struct cw_channel *capi_new(struct capi_pvt *i, int state)
{
	struct cw_channel *tmp;
	char *s;
	int fmt;
	int fds[2];
	int flags;

	tmp = cw_channel_alloc(0, "CAPI/%s/%s-%x", i->name, i->dnid, capi_counter++);
	if (tmp == NULL) {
		cc_log(CW_LOG_ERROR,"Unable to allocate channel!\n");
		return(NULL);
	}

	tmp->type = channeltype;

	if (pipe(fds) != 0) {
		cc_log(CW_LOG_ERROR, "%s: unable to create pipe.\n",
			i->vname);
		cw_channel_free(tmp);
		return NULL;
	}
	i->readerfd = fds[0];
	i->writerfd = fds[1];
	flags = fcntl(i->readerfd, F_GETFL);
	fcntl(i->readerfd, F_SETFL, flags | O_NONBLOCK);
	flags = fcntl(i->writerfd, F_GETFL);
	fcntl(i->writerfd, F_SETFL, flags | O_NONBLOCK);

	tmp->fds[0] = i->readerfd;

	if (i->smoother != NULL) {
		cw_smoother_reset(i->smoother, CAPI_MAX_B3_BLOCK_SIZE);
	}

	i->state = CAPI_STATE_DISCONNECTED;
	i->calledPartyIsISDN = 1;
	i->doB3 = CAPI_B3_DONT;
	i->doES = i->ES;
	i->outgoing = 0;
	i->onholdPLCI = 0;
	i->doholdtype = i->holdtype;
	i->B3q = 0;
	memset(i->txavg, 0, ECHO_TX_COUNT);

	if (i->doDTMF > 0) {
		i->vad = cw_dsp_new();
		cw_dsp_set_features(i->vad, DSP_FEATURE_DTMF_DETECT);
		if (i->doDTMF > 1) {
			cw_dsp_digitmode(i->vad, DSP_DIGITMODE_DTMF | DSP_DIGITMODE_RELAXDTMF);
		}
	}

	CC_CHANNEL_PVT(tmp) = i;

	tmp->callgroup = i->callgroup;
	tmp->pickupgroup = i->pickupgroup;
	tmp->nativeformats = capi_capability;
	i->bproto = CC_BPROTO_TRANSPARENT;
	if ((i->rtpcodec = (capi_controllers[i->controller]->rtpcodec & i->capability))) {
		if (capi_alloc_rtp(i)) {
			/* error on rtp alloc */
			i->rtpcodec = 0;
		} else {
			/* start with rtp */
			tmp->nativeformats = i->rtpcodec;
			i->bproto = CC_BPROTO_RTP;
		}
	}
	fmt = cw_best_codec(tmp->nativeformats);
	i->codec = fmt;
	tmp->readformat = fmt;
	tmp->writeformat = fmt;

	tmp->tech = &capi_tech;
	tmp->rawreadformat = fmt;
	tmp->rawwriteformat = fmt;

	s = alloca(80);
	cc_verbose(3, 1, VERBOSE_PREFIX_2 "%s: setting format %s - %s%s\n",
		i->vname, cw_getformatname(fmt),
		cw_getformatname_multiple(s, 80, tmp->nativeformats),
		(i->rtp ? " (RTP)" : ""));
	cc_copy_string(tmp->context, i->context, sizeof(tmp->context));

	if (!cw_strlen_zero(i->cid)) {
		free(tmp->cid.cid_num);
		tmp->cid.cid_num = strdup(i->cid);
	}
	if (!cw_strlen_zero(i->dnid)) {
		free(tmp->cid.cid_dnid);
		tmp->cid.cid_dnid = strdup(i->dnid);
	}
	tmp->cid.cid_ton = i->cid_ton;
	if (i->amaflags)
		tmp->amaflags = i->amaflags;
	
	cc_copy_string(tmp->exten, i->dnid, sizeof(tmp->exten));
#ifdef CC_CW_HAS_STRINGFIELD_IN_CHANNEL
	cw_string_field_set(tmp, accountcode, i->accountcode);
	cw_string_field_set(tmp, language, i->language);
#else
	cc_copy_string(tmp->accountcode, i->accountcode, sizeof(tmp->accountcode));
	cc_copy_string(tmp->language, i->language, sizeof(tmp->language));
#endif
	i->owner = tmp;

	cw_setstate(tmp, state);

	return tmp;
}

/*
 * PBX wants us to dial ...
 */
static struct cw_channel *
pbx_capi_request(const char *type, int format, void *data, int *cause)
{
	struct capi_pvt *i;
	struct cw_channel *tmp = NULL;
	char *dest, *interface, *param, *ocid;
	char buffer[CAPI_MAX_STRING];
	cw_group_t capigroup = 0;
	unsigned int controller = 0;
	int notfound = 1;

	cc_verbose(1, 1, VERBOSE_PREFIX_4 "data = %s format=%d\n", (char *)data, format);

	cc_copy_string(buffer, (char *)data, sizeof(buffer));
	parse_dialstring(buffer, &interface, &dest, &param, &ocid);

	if ((!interface) || (!dest)) {
		cc_log(CW_LOG_ERROR, "Syntax error in dialstring. Read the docs!\n");
		*cause = CW_CAUSE_INVALID_NUMBER_FORMAT;
		return NULL;
	}

	if (interface[0] == 'g') {
		capigroup = cw_get_group(interface + 1);
		cc_verbose(1, 1, VERBOSE_PREFIX_4 "capi request group = %d\n",
				(unsigned int)capigroup);
	} else if (!strncmp(interface, "contr", 5)) {
		controller = atoi(interface + 5);
		cc_verbose(1, 1, VERBOSE_PREFIX_4 "capi request controller = %d\n",
				controller);
	} else {
		cc_verbose(1, 1, VERBOSE_PREFIX_4 "capi request for interface '%s'\n",
				interface);
 	}

	cc_mutex_lock(&iflock);
	
	for (i = iflist; (i && notfound); i = i->next) {
		if ((i->owner) || (i->channeltype != CAPI_CHANNELTYPE_B)) {
			/* if already in use or no real channel */
			continue;
		}
		/* unused channel */
		if (controller) {
			/* DIAL(CAPI/contrX/...) */
			if (i->controller != controller) {
				/* keep on running! */
				continue;
			}
		} else {
			/* DIAL(CAPI/gX/...) */
			if ((interface[0] == 'g') && (!(i->group & capigroup))) {
				/* keep on running! */
				continue;
			}
			/* DIAL(CAPI/<interface-name>/...) */
			if ((interface[0] != 'g') && (strcmp(interface, i->name))) {
				/* keep on running! */
				continue;
			}
		}
		/* when we come here, we found a free controller match */
		cc_copy_string(i->dnid, dest, sizeof(i->dnid));
		tmp = capi_new(i, CW_STATE_RESERVED);
		if (!tmp) {
			cc_log(CW_LOG_ERROR, "cannot create new capi channel\n");
			interface_cleanup(i);
		}
		i->PLCI = 0;
		i->outgoing = 1;	/* this is an outgoing line */
		cc_mutex_unlock(&iflock);
		return tmp;
	}
	cc_mutex_unlock(&iflock);
	cc_verbose(2, 0, VERBOSE_PREFIX_3 "didn't find capi device for interface '%s'\n",
		interface);
	*cause = CW_CAUSE_REQUESTED_CHAN_UNAVAIL;
	return NULL;
}

/*
 * fill out fax conf struct
 */
static void setup_b3_fax_config(B3_PROTO_FAXG3 *b3conf, int fax_format, char *stationid, char *headline)
{
	int len1;
	int len2;

	cc_verbose(3, 1, VERBOSE_PREFIX_3 "Setup fax b3conf fmt=%d, stationid='%s' headline='%s'\n",
		fax_format, stationid, headline);
	b3conf->resolution = 0;
	b3conf->format = (unsigned short)fax_format;
	len1 = strlen(stationid);
	b3conf->Infos[0] = (unsigned char)len1;
	strcpy((char *)&b3conf->Infos[1], stationid);
	len2 = strlen(headline);
	b3conf->Infos[len1 + 1] = (unsigned char)len2;
	strcpy((char *)&b3conf->Infos[len1 + 2], headline);
	b3conf->len = (unsigned char)(2 * sizeof(unsigned short) + len1 + len2 + 2);
	return;
}

/*
 * change b protocol to fax
 */
static void capi_change_bchan_fax(struct cw_channel *c, B3_PROTO_FAXG3 *b3conf) 
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);

	i->isdnstate |= CAPI_ISDN_STATE_B3_SELECT;
	cc_disconnect_b3(i, 1);
	cc_select_b(i, (_cstruct)b3conf);
	return;
}

/*
 * capicommand 'receivefax'
 */
static int pbx_capi_receive_fax(struct cw_channel *c, int argc, char **argv)
{
	char buffer[CAPI_MAX_STRING];
	B3_PROTO_FAXG3 b3conf;
	struct capi_pvt *i = CC_CHANNEL_PVT(c);
	int res = 0;

	if (argc < 1 || !argv[0][0]) {
		cc_log(CW_LOG_ERROR, "capi receivefax requires a filename\n");
		return -1;
	}

	capi_wait_for_answered(i);

	if ((i->fFax = fopen(argv[0], "wb")) == NULL) {
		cc_log(CW_LOG_WARNING, "can't create fax output file (%s)\n", strerror(errno));
		return -1;
	}

	i->FaxState |= CAPI_FAX_STATE_ACTIVE;
	setup_b3_fax_config(&b3conf, FAX_SFF_FORMAT,
			(argc > 1 && argv[1][0] ? argv[1] : ""),
			(argc > 2 && argv[2][0] ? argv[2] : ""));

	i->bproto = CC_BPROTO_FAXG3;

	switch (i->state) {
	case CAPI_STATE_ALERTING:
	case CAPI_STATE_DID:
	case CAPI_STATE_INCALL:
		capi_send_answer(c, (_cstruct)&b3conf);
		break;
	case CAPI_STATE_CONNECTED:
		capi_change_bchan_fax(c, &b3conf);
		break;
	default:
		i->FaxState &= ~CAPI_FAX_STATE_ACTIVE;
		cc_log(CW_LOG_WARNING, "capi receive fax in wrong state (%d)\n",
			i->state);
		return -1;
	}
	capi_wait_for_fax_finish(i);

	res = (i->FaxState & CAPI_FAX_STATE_ERROR) ? 1 : 0;
	i->FaxState &= ~(CAPI_FAX_STATE_ACTIVE | CAPI_FAX_STATE_ERROR);

	/* if the file has zero length */
	if (ftell(i->fFax) == 0L) {
		res = 1;
	}
			
	cc_verbose(2, 1, VERBOSE_PREFIX_3 "Closing fax file...\n");
	fclose(i->fFax);
	i->fFax = NULL;

	if (res != 0) {
		cc_verbose(2, 0,
			VERBOSE_PREFIX_1 "capi receivefax: fax receive failed reason=0x%04x reasonB3=0x%04x\n",
				i->reason, i->reasonb3);
		unlink(argv[0]);
	} else {
		cc_verbose(2, 0,
			VERBOSE_PREFIX_1 "capi receivefax: fax receive successful.\n");
	}
	snprintf(buffer, CAPI_MAX_STRING-1, "%d", res);
	pbx_builtin_setvar_helper(c, "FAXSTATUS", buffer);
	
	return 0;
}

/*
 * capicommand 'sendfax'
 */
static int pbx_capi_send_fax(struct cw_channel *c, int argc, char **argv)
{
	char buffer[CAPI_MAX_STRING];
	B3_PROTO_FAXG3 b3conf;
	struct capi_pvt *i = CC_CHANNEL_PVT(c);
	int res = 0;

	if (argc < 1 || !argv[0][0]) {
		cc_log(CW_LOG_WARNING, "capi sendfax requires a filename\n");
		return -1;
	}

	capi_wait_for_answered(i);

	if ((i->fFax = fopen(argv[0], "rb")) == NULL) {
		cc_log(CW_LOG_WARNING, "can't open fax file (%s)\n", strerror(errno));
		return -1;
	}

	i->FaxState |= (CAPI_FAX_STATE_ACTIVE | CAPI_FAX_STATE_SENDMODE);
	setup_b3_fax_config(&b3conf, FAX_SFF_FORMAT,
			(argc > 1 && argv[1][0] ? argv[1] : ""),
			(argc > 2 && argv[2][0] ? argv[2] : ""));

	i->bproto = CC_BPROTO_FAXG3;

	switch (i->state) {
	case CAPI_STATE_ALERTING:
	case CAPI_STATE_DID:
	case CAPI_STATE_INCALL:
		capi_send_answer(c, (_cstruct)&b3conf);
		break;
	case CAPI_STATE_CONNECTED:
		capi_change_bchan_fax(c, &b3conf);
		break;
	default:
		i->FaxState &= ~CAPI_FAX_STATE_ACTIVE;
		cc_log(CW_LOG_WARNING, "capi send fax in wrong state (%d)\n",
			i->state);
		return -1;
	}
	capi_wait_for_fax_finish(i);

	res = (i->FaxState & CAPI_FAX_STATE_ERROR) ? 1 : 0;
	i->FaxState &= ~(CAPI_FAX_STATE_ACTIVE | CAPI_FAX_STATE_ERROR);

	/* if the file has zero length */
	if (ftell(i->fFax) == 0L) {
		res = 1;
	}
			
	cc_verbose(2, 1, VERBOSE_PREFIX_3 "Closing fax file...\n");
	fclose(i->fFax);
	i->fFax = NULL;

	if (res != 0) {
		cc_verbose(2, 0,
			VERBOSE_PREFIX_1 "capi sendfax: fax send failed reason=0x%04x reasonB3=0x%04x\n",
				i->reason, i->reasonb3);
	} else {
		cc_verbose(2, 0,
			VERBOSE_PREFIX_1 "capi sendfax: fax sent successful.\n");
	}
	snprintf(buffer, CAPI_MAX_STRING-1, "%d", res);
	pbx_builtin_setvar_helper(c, "FAXSTATUS", buffer);
	
	return 0;
}

/*
 * Fax guard tone -- Handle and return NULL
 */
static void capi_handle_dtmf_fax(struct capi_pvt *i)
{
	struct cw_channel *c = i->owner;

	if (!c) {
		cc_log(CW_LOG_ERROR, "No channel!\n");
		return;
	}
	
	if (i->FaxState & CAPI_FAX_STATE_HANDLED) {
		cc_log(CW_LOG_DEBUG, "Fax already handled\n");
		return;
	}
	i->FaxState |= CAPI_FAX_STATE_HANDLED;

	if (((i->outgoing == 1) && (!(i->FaxState & CAPI_FAX_DETECT_OUTGOING))) ||
	    ((i->outgoing == 0) && (!(i->FaxState & CAPI_FAX_DETECT_INCOMING)))) {
		cc_verbose(3, 0, VERBOSE_PREFIX_3 "%s: Fax detected, but not configured for redirection\n",
			i->vname);
		return;
	}
	
	if (!strcmp(c->exten, "fax")) {
		cc_log(CW_LOG_DEBUG, "Already in a fax extension, not redirecting\n");
		return;
	}

	if (!cw_exists_extension(c, c->context, "fax", 1, i->cid)) {
		cc_verbose(3, 0, VERBOSE_PREFIX_3 "Fax tone detected, but no fax extension for %s\n", c->name);
		return;
	}

	cc_verbose(2, 0, VERBOSE_PREFIX_3 "%s: Redirecting %s to fax extension\n",
		i->vname, c->name);
			
	/* Save the DID/DNIS when we transfer the fax call to a "fax" extension */
	pbx_builtin_setvar_helper(c, "FAXEXTEN", c->exten);
	
	if (cw_async_goto_n(c, c->context, "fax", 1))
		cc_log(CW_LOG_WARNING, "Failed to async goto '%s' into fax of '%s'\n", c->name, c->context);
	return;
}

/*
 * find the interface (pvt) the PLCI belongs to
 */
static struct capi_pvt *find_interface_by_plci(unsigned int plci)
{
	struct capi_pvt *i;

	if (plci == 0)
		return NULL;

	cc_mutex_lock(&iflock);
	for (i = iflist; i; i = i->next) {
		if (i->PLCI == plci)
			break;
	}
	cc_mutex_unlock(&iflock);

	return i;
}

/*
 * find the interface (pvt) the messagenumber belongs to
 */
static struct capi_pvt *find_interface_by_msgnum(unsigned short msgnum)
{
	struct capi_pvt *i;

	if (msgnum == 0x0000)
		return NULL;

	cc_mutex_lock(&iflock);
	for (i = iflist; i; i = i->next) {
		    if ((i->PLCI == 0) && (i->MessageNumber == msgnum))
			break;
	}
	cc_mutex_unlock(&iflock);

	return i;
}

/*
 * see if did matches
 */
static int search_did(struct cw_channel *c)
{
	/*
	 * Returns 
	 * -1 = Failure 
	 *  0 = Match
	 *  1 = possible match 
	 */
	struct capi_pvt *i = CC_CHANNEL_PVT(c);
	char *exten;
    
	if (!strlen(i->dnid) && (i->immediate)) {
		exten = "s";
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: %s: %s matches in context %s for immediate\n",
			i->vname, c->name, exten, c->context);
	} else {
		if (strlen(i->dnid) < strlen(i->incomingmsn))
			return 0;
		exten = i->dnid;
	}

	if (cw_exists_extension(NULL, c->context, exten, 1, i->cid)) {
		c->priority = 1;
		cc_copy_string(c->exten, exten, sizeof(c->exten));
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: %s: %s matches in context %s\n",
			i->vname, c->name, exten, c->context);
		return 0;
	}

	if (cw_canmatch_extension(NULL, c->context, exten, 1, i->cid)) {
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: %s: %s would possibly match in context %s\n",
			i->vname, c->name, exten, c->context);
		return 1;
	}

	return -1;
}

/*
 * Progress Indicator
 */
static void handle_progress_indicator(_cmsg *CMSG, unsigned int PLCI, struct capi_pvt *i)
{
	if (INFO_IND_INFOELEMENT(CMSG)[0] < 2) {
		cc_verbose(3, 1, VERBOSE_PREFIX_4 "%s: Progress description missing\n",
			i->vname);
		return;
	}

	switch(INFO_IND_INFOELEMENT(CMSG)[2] & 0x7f) {
	case 0x01:
		cc_verbose(4, 1, VERBOSE_PREFIX_4 "%s: Not end-to-end ISDN\n",
			i->vname);
		break;
	case 0x02:
		cc_verbose(4, 1, VERBOSE_PREFIX_4 "%s: Destination is non ISDN\n",
			i->vname);
		i->calledPartyIsISDN = 0;
		break;
	case 0x03:
		cc_verbose(4, 1, VERBOSE_PREFIX_4 "%s: Origination is non ISDN\n",
			i->vname);
		break;
	case 0x04:
		cc_verbose(4, 1, VERBOSE_PREFIX_4 "%s: Call returned to ISDN\n",
			i->vname);
		break;
	case 0x05:
		cc_verbose(4, 1, VERBOSE_PREFIX_4 "%s: Interworking occured\n",
			i->vname);
		break;
	case 0x08:
		cc_verbose(4, 1, VERBOSE_PREFIX_4 "%s: In-band information available\n",
			i->vname);
		break;
	default:
		cc_verbose(3, 1, VERBOSE_PREFIX_4 "%s: Unknown progress description %02x\n",
			i->vname, INFO_IND_INFOELEMENT(CMSG)[2]);
	}
	send_progress(i);
	return;
}

/*
 * if the dnid matches, start the pbx
 */
static void start_pbx_on_match(struct capi_pvt *i, unsigned int PLCI, _cword MessageNumber)
{
	struct cw_channel *c;
	_cmsg CMSG2;

	c = i->owner;

	if ((i->isdnstate & CAPI_ISDN_STATE_PBX_DONT)) {
		/* we already found non-match here */
		return;
	}
	if ((i->isdnstate & CAPI_ISDN_STATE_PBX)) {
		cc_verbose(3, 1, VERBOSE_PREFIX_2 "%s: pbx already started on channel %s\n",
			i->vname, c->name);
		return;
	}

	/* check for internal pickup extension first */
	if (!strcmp(i->dnid, cw_pickup_ext())) {
		i->isdnstate |= CAPI_ISDN_STATE_PBX;
		cc_verbose(3, 1, VERBOSE_PREFIX_2 "%s: Pickup extension '%s' found.\n",
			i->vname, i->dnid);
		cc_copy_string(c->exten, i->dnid, sizeof(c->exten));
		pbx_capi_alert(c);
		capi_channel_task(c, CAPI_CHANNEL_TASK_PICKUP);
		return;
	}

	switch(search_did(i->owner)) {
	case 0: /* match */
		i->isdnstate |= CAPI_ISDN_STATE_PBX;
		cw_setstate(c, CW_STATE_RING);
		if (cw_pbx_start(i->owner)) {
			cc_log(CW_LOG_ERROR, "%s: Unable to start pbx on channel!\n",
				i->vname);
			capi_channel_task(c, CAPI_CHANNEL_TASK_HANGUP); 
		} else {
			cc_verbose(2, 1, VERBOSE_PREFIX_2 "Started pbx on channel %s\n",
				c->name);
		}
		break;
	case 1:
		/* would possibly match */
		if (i->isdnmode == CAPI_ISDNMODE_DID)
			break;
		/* fall through for MSN mode, because there won't be a longer msn */
	case -1:
	default:
		/* doesn't match */
		i->isdnstate |= CAPI_ISDN_STATE_PBX_DONT; /* don't try again */
		cc_log(CW_LOG_NOTICE, "%s: did not find exten for '%s', ignoring call.\n",
			i->vname, i->dnid);
		CONNECT_RESP_HEADER(&CMSG2, capi_ApplID, MessageNumber, 0);
		CONNECT_RESP_PLCI(&CMSG2) = PLCI;
		CONNECT_RESP_REJECT(&CMSG2) = 1; /* ignore */
		_capi_put_cmsg(&CMSG2);
	}
	return;
}

/*
 * Called Party Number via INFO_IND
 */
static void capidev_handle_did_digits(_cmsg *CMSG, unsigned int PLCI, unsigned int NCCI, struct capi_pvt *i)
{
	char *did;
	struct cw_frame fr = { CW_FRAME_NULL, };
	int a;

	if (!i->owner) {
		cc_log(CW_LOG_ERROR, "No channel for interface!\n");
		return;
	}

	if (i->state != CAPI_STATE_DID) {
		cc_verbose(4, 1, VERBOSE_PREFIX_4 "%s: INFO_IND DID digits not used in this state.\n",
			i->vname);
		return;
	}

	did = capi_number(INFO_IND_INFOELEMENT(CMSG), 1);

	if ((!(i->isdnstate & CAPI_ISDN_STATE_DID)) && 
	    (strlen(i->dnid) && !strcasecmp(i->dnid, did))) {
		did = NULL;
	}

	if ((did) && (strlen(i->dnid) < (sizeof(i->dnid) - 1)))
		strcat(i->dnid, did);

	i->isdnstate |= CAPI_ISDN_STATE_DID;
	
	update_channel_name(i);	
	
	if (i->owner->pbx != NULL) {
		/* we are already in pbx, so we send the digits as dtmf */
		for (a = 0; a < strlen(did); a++) {
			fr.frametype = CW_FRAME_DTMF;
			fr.subclass = did[a];
			local_queue_frame(i, &fr);
		} 
		return;
	}

	start_pbx_on_match(i, PLCI, HEADER_MSGNUM(CMSG));
	return;
}

/*
 * send control according to cause code
 */
static void queue_cause_control(struct capi_pvt *i, int control)
{
	struct cw_frame fr = { CW_FRAME_CONTROL, CW_CONTROL_HANGUP, };
	
	if ((i->owner) && (control)) {
		int cause = i->owner->hangupcause;
		if (cause == CW_CAUSE_NORMAL_CIRCUIT_CONGESTION) {
			fr.subclass = CW_CONTROL_CONGESTION;
		} else if ((cause != CW_CAUSE_NO_USER_RESPONSE) &&
		           (cause != CW_CAUSE_NO_ANSWER)) {
			/* not NOANSWER */
			fr.subclass = CW_CONTROL_BUSY;
		}
	}
	local_queue_frame(i, &fr);
	return;
}

/*
 * Disconnect via INFO_IND
 */
static void capidev_handle_info_disconnect(_cmsg *CMSG, unsigned int PLCI, unsigned int NCCI, struct capi_pvt *i)
{
	_cmsg CMSG2;

	i->isdnstate |= CAPI_ISDN_STATE_DISCONNECT;

	if ((i->isdnstate & CAPI_ISDN_STATE_ECT)) {
		cc_verbose(4, 1, VERBOSE_PREFIX_3 "%s: Disconnect ECT call\n",
			i->vname);
		/* we do nothing, just wait for DISCONNECT_IND */
		return;
	}

	if (PLCI == i->onholdPLCI) {
		cc_verbose(4, 1, VERBOSE_PREFIX_3 "%s: Disconnect onhold call\n",
			i->vname);
		/* the caller onhold hung up (or ECTed away) */
		/* send a disconnect_req , we cannot hangup the channel here!!! */
		DISCONNECT_REQ_HEADER(&CMSG2, capi_ApplID, get_capi_MessageNumber(), 0);
		DISCONNECT_REQ_PLCI(&CMSG2) = i->onholdPLCI;
		_capi_put_cmsg(&CMSG2);
		return;
	}

	/* case 1: B3 on success or no B3 at all */
	if ((i->doB3 != CAPI_B3_ALWAYS) && (i->outgoing == 1)) {
		cc_verbose(4, 1, VERBOSE_PREFIX_3 "%s: Disconnect case 1\n",
			i->vname);
		if (i->state == CAPI_STATE_CONNECTED) 
			queue_cause_control(i, 0);
		else
			queue_cause_control(i, 1);
		return;
	}
	
	/* case 2: we are doing B3, and receive the 0x8045 after a successful call */
	if ((i->doB3 != CAPI_B3_DONT) &&
	    (i->state == CAPI_STATE_CONNECTED) && (i->outgoing == 1)) {
		cc_verbose(4, 1, VERBOSE_PREFIX_3 "%s: Disconnect case 2\n",
			i->vname);
		queue_cause_control(i, 1);
		return;
	}

	/*
	 * case 3: this channel is an incoming channel! the user hung up!
	 * it is much better to hangup now instead of waiting for a timeout and
	 * network caused DISCONNECT_IND!
	 */
	if (i->outgoing == 0) {
		cc_verbose(4, 1, VERBOSE_PREFIX_3 "%s: Disconnect case 3\n",
			i->vname);
		if (i->FaxState & CAPI_FAX_STATE_ACTIVE) {
			/* in fax mode, we just hangup */
			DISCONNECT_REQ_HEADER(&CMSG2, capi_ApplID, get_capi_MessageNumber(), 0);
			DISCONNECT_REQ_PLCI(&CMSG2) = i->PLCI;
			_capi_put_cmsg(&CMSG2);
			return;
		}
		queue_cause_control(i, 0);
		return;
	}
	
	/* case 4 (a.k.a. the italian case): B3 always. call is unsuccessful */
	if ((i->doB3 == CAPI_B3_ALWAYS) && (i->outgoing == 1)) {
		cc_verbose(4, 1, VERBOSE_PREFIX_3 "%s: Disconnect case 4\n",
			i->vname);
		if ((i->state == CAPI_STATE_CONNECTED) &&
		    (i->isdnstate & CAPI_ISDN_STATE_B3_UP)) {
			queue_cause_control(i, 1);
			return;
		}
		/* wait for the 0x001e (PROGRESS), play audio and wait for a timeout from the network */
		return;
	}
	cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: Other case DISCONNECT INFO_IND\n",
		i->vname);
	return;
}

/*
 * incoming call SETUP
 */
static void capidev_handle_setup_element(_cmsg *CMSG, unsigned int PLCI, struct capi_pvt *i)
{
	if ((i->isdnstate & CAPI_ISDN_STATE_SETUP)) {
		cc_verbose(3, 1, VERBOSE_PREFIX_4 "%s: IE SETUP / SENDING-COMPLETE already received.\n",
			i->vname);
		return;
	}

	i->isdnstate |= CAPI_ISDN_STATE_SETUP;

	if (!i->owner) {
		cc_log(CW_LOG_ERROR, "No channel for interface!\n");
		return;
	}

	if (i->isdnmode == CAPI_ISDNMODE_DID) {
		if (!strlen(i->dnid) && (i->immediate)) {
			start_pbx_on_match(i, PLCI, HEADER_MSGNUM(CMSG));
		}
	} else {
		start_pbx_on_match(i, PLCI, HEADER_MSGNUM(CMSG));
	}
	return;
}

/*
 * CAPI INFO_IND
 */
static void capidev_handle_info_indication(_cmsg *CMSG, unsigned int PLCI, unsigned int NCCI, struct capi_pvt *i)
{
	_cmsg CMSG2;
	struct cw_frame fr = { CW_FRAME_NULL, };
	char *p = NULL;
	int val = 0;

	INFO_RESP_HEADER(&CMSG2, capi_ApplID, HEADER_MSGNUM(CMSG), PLCI);
	_capi_put_cmsg(&CMSG2);

	return_on_no_interface("INFO_IND");

	switch(INFO_IND_INFONUMBER(CMSG)) {
	case 0x0008:	/* Cause */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element CAUSE %02x %02x\n",
			i->vname, INFO_IND_INFOELEMENT(CMSG)[1], INFO_IND_INFOELEMENT(CMSG)[2]);
		if (i->owner) {
			i->owner->hangupcause = INFO_IND_INFOELEMENT(CMSG)[2] & 0x7f;
		}
		break;
	case 0x0014:	/* Call State */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element CALL STATE %02x\n",
			i->vname, INFO_IND_INFOELEMENT(CMSG)[1]);
		break;
	case 0x0018:	/* Channel Identification */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element CHANNEL IDENTIFICATION %02x\n",
			i->vname, INFO_IND_INFOELEMENT(CMSG)[1]);
		break;
	case 0x001c:	/*  Facility Q.932 */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element FACILITY\n",
			i->vname);
		break;
	case 0x001e:	/* Progress Indicator */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element PI %02x %02x\n",
			i->vname, INFO_IND_INFOELEMENT(CMSG)[1], INFO_IND_INFOELEMENT(CMSG)[2]);
		handle_progress_indicator(CMSG, PLCI, i);
		break;
	case 0x0027: {	/*  Notification Indicator */
		char *desc = "?";
		if (INFO_IND_INFOELEMENT(CMSG)[0] > 0) {
			switch (INFO_IND_INFOELEMENT(CMSG)[1]) {
			case 0:
				desc = "User suspended";
				break;
			case 1:
				desc = "User resumed";
				break;
			case 2:
				desc = "Bearer service changed";
				break;
			case 0xf9:
				desc = "User put on hold";
				break;
			case 0xfa:
				desc = "User retrieved from hold";
				break;
			}
		}
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element NOTIFICATION INDICATOR '%s' (0x%02x)\n",
			i->vname, desc, INFO_IND_INFOELEMENT(CMSG)[1]);
		break;
	}
	case 0x0028:	/* DSP */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element DSP\n",
			i->vname);
		break;
	case 0x0029:	/* Date/Time */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element Date/Time %02d/%02d/%02d %02d:%02d\n",
			i->vname,
			INFO_IND_INFOELEMENT(CMSG)[1], INFO_IND_INFOELEMENT(CMSG)[2],
			INFO_IND_INFOELEMENT(CMSG)[3], INFO_IND_INFOELEMENT(CMSG)[4],
			INFO_IND_INFOELEMENT(CMSG)[5]);
		break;
	case 0x0070:	/* Called Party Number */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element CALLED PARTY NUMBER\n",
			i->vname);
		capidev_handle_did_digits(CMSG, PLCI, NCCI, i);
		break;
	case 0x0074:	/* Redirecting Number */
		p = capi_number(INFO_IND_INFOELEMENT(CMSG), 3);
		if (INFO_IND_INFOELEMENT(CMSG)[0] > 2) {
			val = INFO_IND_INFOELEMENT(CMSG)[3] & 0x0f;
		}
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element REDIRECTING NUMBER '%s' Reason=0x%02x\n",
			i->vname, p, val);
		if (i->owner) {
			char reasonbuf[16];
			snprintf(reasonbuf, sizeof(reasonbuf) - 1, "%d", val); 
			pbx_builtin_setvar_helper(i->owner, "REDIRECTINGNUMBER", p);
			pbx_builtin_setvar_helper(i->owner, "REDIRECTREASON", reasonbuf);
			free(i->owner->cid.cid_rdnis);
			i->owner->cid.cid_rdnis = strdup(p);
		}
		break;
	case 0x00a1:	/* Sending Complete */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element Sending Complete\n",
			i->vname);
		capidev_handle_setup_element(CMSG, PLCI, i);
		break;
	case 0x4000:	/* CHARGE in UNITS */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element CHARGE in UNITS\n",
			i->vname);
		break;
	case 0x4001:	/* CHARGE in CURRENCY */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element CHARGE in CURRENCY\n",
			i->vname);
		break;
	case 0x8001:	/* ALERTING */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element ALERTING\n",
			i->vname);
		send_progress(i);
		fr.frametype = CW_FRAME_CONTROL;
		fr.subclass = CW_CONTROL_RINGING;
		local_queue_frame(i, &fr);
		if (i->owner)
			cw_setstate(i->owner, CW_STATE_RINGING);
		break;
	case 0x8002:	/* CALL PROCEEDING */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element CALL PROCEEDING\n",
			i->vname);
		fr.frametype = CW_FRAME_CONTROL;
		fr.subclass = CW_CONTROL_PROCEEDING;
		local_queue_frame(i, &fr);
		break;
	case 0x8003:	/* PROGRESS */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element PROGRESS\n",
			i->vname);
		/*
		 * rain - some networks will indicate a USER BUSY cause, send
		 * PROGRESS message, and then send audio for a busy signal for
		 * a moment before dropping the line.  This delays sending the
		 * busy to the end user, so we explicitly check for it here.
		 *
		 * FIXME: should have better CAUSE handling so that we can
		 * distinguish things like status responses and invalid IE
		 * content messages (from bad SetCallerID) from errors actually
		 * related to the call setup; then, we could always abort if we
		 * get a PROGRESS with a hangupcause set (safer?)
		 */
		if (i->doB3 == CAPI_B3_DONT) {
			if ((i->owner) &&
			    (i->owner->hangupcause == CW_CAUSE_USER_BUSY)) {
				queue_cause_control(i, 1);
				break;
			}
		}
		send_progress(i);
		break;
	case 0x8005:	/* SETUP */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element SETUP\n",
			i->vname);
		capidev_handle_setup_element(CMSG, PLCI, i);
		break;
	case 0x8007:	/* CONNECT */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element CONNECT\n",
			i->vname);
		break;
	case 0x800d:	/* SETUP ACK */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element SETUP ACK\n",
			i->vname);
		i->isdnstate |= CAPI_ISDN_STATE_SETUP_ACK;
		/* if some digits of initial CONNECT_REQ are left to dial */
		if (strlen(i->overlapdigits)) {
			capi_send_info_digits(i, i->overlapdigits,
				strlen(i->overlapdigits));
			i->overlapdigits[0] = 0;
			i->doOverlap = 0;
		}
		break;
	case 0x800f:	/* CONNECT ACK */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element CONNECT ACK\n",
			i->vname);
		break;
	case 0x8045:	/* DISCONNECT */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element DISCONNECT\n",
			i->vname);
		capidev_handle_info_disconnect(CMSG, PLCI, NCCI, i);
		break;
	case 0x804d:	/* RELEASE */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element RELEASE\n",
			i->vname);
		break;
	case 0x805a:	/* RELEASE COMPLETE */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element RELEASE COMPLETE\n",
			i->vname);
		break;
	case 0x8062:	/* FACILITY */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element FACILITY\n",
			i->vname);
		break;
	case 0x806e:	/* NOTIFY */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element NOTIFY\n",
			i->vname);
		break;
	case 0x807b:	/* INFORMATION */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element INFORMATION\n",
			i->vname);
		break;
	case 0x807d:	/* STATUS */
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: info element STATUS\n",
			i->vname);
		break;
	default:
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: unhandled INFO_IND %#x (PLCI=%#x)\n",
			i->vname, INFO_IND_INFONUMBER(CMSG), PLCI);
		break;
	}
	return;
}

/*
 * CAPI FACILITY_IND
 */
static void capidev_handle_facility_indication(_cmsg *CMSG, unsigned int PLCI, unsigned int NCCI, struct capi_pvt *i)
{
	_cmsg CMSG2;
	struct cw_frame fr = { CW_FRAME_NULL, };
	char dtmf;
	unsigned dtmflen;
	unsigned dtmfpos = 0;

	FACILITY_RESP_HEADER(&CMSG2, capi_ApplID, HEADER_MSGNUM(CMSG), PLCI);
	FACILITY_RESP_FACILITYSELECTOR(&CMSG2) = FACILITY_IND_FACILITYSELECTOR(CMSG);
	FACILITY_RESP_FACILITYRESPONSEPARAMETERS(&CMSG2) = FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG);
	_capi_put_cmsg(&CMSG2);
	
	return_on_no_interface("FACILITY_IND");

	if (FACILITY_IND_FACILITYSELECTOR(CMSG) == FACILITYSELECTOR_LINE_INTERCONNECT) {
		/* line interconnect */
		if ((FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[1] == 0x01) &&
		    (FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[2] == 0x00)) {
			cc_verbose(3, 0, VERBOSE_PREFIX_3 "%s: Line Interconnect activated\n",
				i->vname);
		}
		if ((FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[1] == 0x02) &&
		    (FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[2] == 0x00) &&
		    (FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[0] > 8)) {
			show_capi_info(i, read_capi_word(&FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[8]));
		}
	}
	
	if (FACILITY_IND_FACILITYSELECTOR(CMSG) == FACILITYSELECTOR_DTMF) {
		/* DTMF received */
		if (FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[0] != (0xff)) {
			dtmflen = FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[0];
			FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG) += 1;
		} else {
			dtmflen = read_capi_word(FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG) + 1);
			FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG) += 3;
		}
		while (dtmflen) {
			dtmf = (FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG))[dtmfpos];
			cc_verbose(1, 1, VERBOSE_PREFIX_4 "%s: c_dtmf = %c\n",
				i->vname, dtmf);
			if ((!(i->ntmode)) || (i->state == CAPI_STATE_CONNECTED)) {
				if ((dtmf == 'X') || (dtmf == 'Y')) {
					capi_handle_dtmf_fax(i);
				} else {
					fr.frametype = CW_FRAME_DTMF;
					fr.subclass = dtmf;
					local_queue_frame(i, &fr);
				}
			}
			dtmflen--;
			dtmfpos++;
		} 
	}
	
	if (FACILITY_IND_FACILITYSELECTOR(CMSG) == FACILITYSELECTOR_SUPPLEMENTARY) {
		/* supplementary sservices */
		/* ECT */
		if ( (FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[1] == 0x6) &&
		     (FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[3] == 0x2) ) {
			cc_verbose(1, 1, VERBOSE_PREFIX_3 "%s: PLCI=%#x ECT  Reason=0x%02x%02x\n",
				i->vname, PLCI,
				FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[5],
				FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[4]);
			show_capi_info(i, read_capi_word(&FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[4]));
		}

		/* RETRIEVE */
		if ( (FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[1] == 0x3) &&
		     (FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[3] == 0x2) ) {
			if ((FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[5] != 0) || 
			    (FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[4] != 0)) { 
				cc_log(CW_LOG_WARNING, "%s: unable to retrieve PLCI=%#x, REASON = 0x%02x%02x\n",
					i->vname, PLCI,
					FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[5],
					FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[4]);
				show_capi_info(i, read_capi_word(&FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[4]));
			} else {
				/* reason != 0x0000 == problem */
				i->state = CAPI_STATE_CONNECTED;
				i->PLCI = i->onholdPLCI;
				i->onholdPLCI = 0;
				cc_verbose(1, 1, VERBOSE_PREFIX_3 "%s: PLCI=%#x retrieved\n",
					i->vname, PLCI);
				cc_start_b3(i);
			}
		}
		
		/* HOLD */
		if ( (FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[1] == 0x2) &&
		     (FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[3] == 0x2) ) {
			if ((FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[5] != 0) || 
			    (FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[4] != 0)) { 
				/* reason != 0x0000 == problem */
				i->onholdPLCI = 0;
				cc_log(CW_LOG_WARNING, "%s: unable to put PLCI=%#x onhold, REASON = 0x%02x%02x, maybe you need to subscribe for this...\n",
					i->vname, PLCI,
					FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[5],
					FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[4]);
				show_capi_info(i, read_capi_word(&FACILITY_IND_FACILITYINDICATIONPARAMETER(CMSG)[4]));
			} else {
				/* reason = 0x0000 == call on hold */
				i->state = CAPI_STATE_ONHOLD;
				cc_verbose(1, 1, VERBOSE_PREFIX_3 "%s: PLCI=%#x put onhold\n",
					i->vname, PLCI);
			}
		}
	}
	return;
}

/*
 * CAPI DATA_B3_IND
 */
static void capidev_handle_data_b3_indication(_cmsg *CMSG, unsigned int PLCI, unsigned int NCCI, struct capi_pvt *i)
{
	_cmsg CMSG2;
	struct cw_frame fr = { CW_FRAME_NULL, };
	unsigned char *b3buf = NULL;
	int b3len = 0;
	int j;
	int rxavg = 0;
	int txavg = 0;
	int rtpoffset = 0;

	if (i != NULL) {
		if ((i->isdnstate & CAPI_ISDN_STATE_RTP)) rtpoffset = RTP_HEADER_SIZE;
		b3len = DATA_B3_IND_DATALENGTH(CMSG);
		b3buf = &(i->rec_buffer[CW_FRIENDLY_OFFSET - rtpoffset]);
		memcpy(b3buf, (char *)DATA_B3_IND_DATA(CMSG), b3len);
	}
	
	/* send a DATA_B3_RESP very quickly to free the buffer in capi */
	DATA_B3_RESP_HEADER(&CMSG2, capi_ApplID, HEADER_MSGNUM(CMSG), 0);
	DATA_B3_RESP_NCCI(&CMSG2) = NCCI;
	DATA_B3_RESP_DATAHANDLE(&CMSG2) = DATA_B3_IND_DATAHANDLE(CMSG);
	_capi_put_cmsg(&CMSG2);

	return_on_no_interface("DATA_B3_IND");

	if (i->fFax) {
		/* we are in fax mode and have a file open */
		cc_verbose(6, 1, VERBOSE_PREFIX_3 "%s: DATA_B3_IND (len=%d) Fax\n",
			i->vname, b3len);
		if (!(i->FaxState & CAPI_FAX_STATE_SENDMODE)) {
			if (fwrite(b3buf, 1, b3len, i->fFax) != b3len)
				cc_log(CW_LOG_WARNING, "%s : error writing output file (%s)\n",
					i->vname, strerror(errno));
		}
		return;
	}

	if (((i->isdnstate &
	    (CAPI_ISDN_STATE_B3_CHANGE | CAPI_ISDN_STATE_LI | CAPI_ISDN_STATE_HANGUP))) ||
	    (i->state == CAPI_STATE_DISCONNECTING)) {
		/* drop voice frames when we don't want them */
		return;
	}

	if ((i->isdnstate & CAPI_ISDN_STATE_RTP)) {
		struct cw_frame *f = capi_read_rtp(i, b3buf, b3len);
		if (f)
			local_queue_frame(i, f);
		return;
	}

	if (i->B3q < (((CAPI_MAX_B3_BLOCKS - 1) * CAPI_MAX_B3_BLOCK_SIZE) + 1)) {
		i->B3q += b3len;
	}

	if ((i->doES == 1)) {
		for (j = 0; j < b3len; j++) {
			*(b3buf + j) = reversebits[*(b3buf + j)]; 
			if (capi_capability == CW_FORMAT_ULAW) {
				rxavg += abs(capiULAW2INT[ reversebits[*(b3buf + j)]]);
			} else {
				rxavg += abs(capiALAW2INT[ reversebits[*(b3buf + j)]]);
			}
		}
		rxavg = rxavg / j;
		for (j = 0; j < ECHO_EFFECTIVE_TX_COUNT; j++) {
			txavg += i->txavg[j];
		}
		txavg = txavg / j;
			    
		if ( (txavg / ECHO_TXRX_RATIO) > rxavg) {
			if (capi_capability == CW_FORMAT_ULAW) {
				memset(b3buf, 255, b3len);
			} else {
				memset(b3buf, 85, b3len);
			}
			cc_verbose(6, 1, VERBOSE_PREFIX_3 "%s: SUPPRESSING ECHO rx=%d, tx=%d\n",
					i->vname, rxavg, txavg);
		}
	} else {
		if (i->rxgain == 1.0) {
			for (j = 0; j < b3len; j++) {
				*(b3buf + j) = reversebits[*(b3buf + j)];
			}
		} else {
			for (j = 0; j < b3len; j++) {
				*(b3buf + j) = reversebits[i->g.rxgains[*(b3buf + j)]];
			}
		}
	}

	fr.frametype = CW_FRAME_VOICE;
	fr.subclass = capi_capability;
	fr.data = b3buf;
	fr.datalen = b3len;
	fr.samples = b3len;
	fr.offset = CW_FRIENDLY_OFFSET;
	fr.mallocd = 0;
	fr.delivery = cw_tv(0,0);
	cc_verbose(8, 1, VERBOSE_PREFIX_3 "%s: DATA_B3_IND (len=%d) fr.datalen=%d fr.subclass=%d\n",
		i->vname, b3len, fr.datalen, fr.subclass);
	local_queue_frame(i, &fr);
	return;
}

/*
 * signal 'answer' to PBX
 */
static void capi_signal_answer(struct capi_pvt *i)
{
	struct cw_frame fr = { CW_FRAME_CONTROL, CW_CONTROL_ANSWER, };

	if (i->outgoing == 1) {
		local_queue_frame(i, &fr);
	}
}

/*
 * send the next data
 */
static void capidev_send_faxdata(struct capi_pvt *i)
{
	unsigned char faxdata[CAPI_MAX_B3_BLOCK_SIZE];
	size_t len;
	_cmsg CMSG;

	if ((i->fFax) && (!(feof(i->fFax)))) {
		len = fread(faxdata, 1, CAPI_MAX_B3_BLOCK_SIZE, i->fFax);
		if (len > 0) {
			DATA_B3_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(), 0);
			DATA_B3_REQ_NCCI(&CMSG) = i->NCCI;
			DATA_B3_REQ_DATALENGTH(&CMSG) = len;
			DATA_B3_REQ_FLAGS(&CMSG) = 0; 
			DATA_B3_REQ_DATAHANDLE(&CMSG) = i->send_buffer_handle;
			DATA_B3_REQ_DATA(&CMSG) = faxdata;
			i->send_buffer_handle++;
			cc_verbose(5, 1, VERBOSE_PREFIX_3 "%s: send %d fax bytes.\n",
				i->vname, len);
			_capi_put_cmsg(&CMSG);
			return;
		}
	}
	/* finished send fax, so we hangup */
	cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: completed faxsend.\n",
		i->vname);
	DISCONNECT_B3_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(), 0);
	DISCONNECT_B3_REQ_NCCI(&CMSG) = i->NCCI;
	_capi_put_cmsg(&CMSG);
}

/*
 * CAPI MANUFACTURER_IND
 */
static void capidev_handle_manufacturer_indication(_cmsg *CMSG, unsigned int PLCI, unsigned int NCCI, struct capi_pvt *i)
{
	_cmsg CMSG2;
	
	MANUFACTURER_RESP_HEADER(&CMSG2, capi_ApplID, HEADER_MSGNUM(CMSG), 0);
	MANUFACTURER_RESP_CONTROLLER(&CMSG2) = MANUFACTURER_IND_CONTROLLER(CMSG);
	MANUFACTURER_RESP_MANUID(&CMSG2) = MANUFACTURER_IND_MANUID(CMSG);
	_capi_put_cmsg(&CMSG2);
	
	return_on_no_interface("MANUFACTURER_IND");

	cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: Ignored MANUFACTURER_IND Id=0x%x \n",
		i->vname, MANUFACTURER_IND_MANUID(CMSG));

	return;
}

/*
 * CAPI CONNECT_ACTIVE_IND
 */
static void capidev_handle_connect_active_indication(_cmsg *CMSG, unsigned int PLCI, unsigned int NCCI, struct capi_pvt *i)
{
	_cmsg CMSG2;
	
	CONNECT_ACTIVE_RESP_HEADER(&CMSG2, capi_ApplID, HEADER_MSGNUM(CMSG), 0);
	CONNECT_ACTIVE_RESP_PLCI(&CMSG2) = PLCI;
	_capi_put_cmsg(&CMSG2);
	
	return_on_no_interface("CONNECT_ACTIVE_IND");

	if (i->state == CAPI_STATE_DISCONNECTING) {
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: CONNECT_ACTIVE in DISCONNECTING.\n",
			i->vname);
		return;
	}

	i->state = CAPI_STATE_CONNECTED;

	if ((i->FaxState & CAPI_FAX_STATE_SENDMODE)) {
		cc_start_b3(i);
		return;
	}

	if ((i->owner) && (i->FaxState & CAPI_FAX_STATE_ACTIVE)) {
		cw_setstate(i->owner, CW_STATE_UP);
		if (i->owner->cdr)
			cw_cdr_answer(i->owner->cdr);
		return;
	}
	
	/* normal processing */
			    
	if (!(i->isdnstate & CAPI_ISDN_STATE_B3_UP)) {
		/* send a CONNECT_B3_REQ */
		if (i->outgoing == 1) {
			/* outgoing call */
			cc_start_b3(i);
		} else {
			/* incoming call */
			/* RESP already sent ... wait for CONNECT_B3_IND */
		}
	} else {
		capi_signal_answer(i);
	}
	return;
}

/*
 * CAPI CONNECT_B3_ACTIVE_IND
 */
static void capidev_handle_connect_b3_active_indication(_cmsg *CMSG, unsigned int PLCI, unsigned int NCCI, struct capi_pvt *i)
{
	_cmsg CMSG2;

	/* then send a CONNECT_B3_ACTIVE_RESP */
	CONNECT_B3_ACTIVE_RESP_HEADER(&CMSG2, capi_ApplID, HEADER_MSGNUM(CMSG), 0);
	CONNECT_B3_ACTIVE_RESP_NCCI(&CMSG2) = NCCI;
	_capi_put_cmsg(&CMSG2);

	return_on_no_interface("CONNECT_ACTIVE_B3_IND");

	capi_controllers[i->controller]->nfreebchannels--;

	if (i->state == CAPI_STATE_DISCONNECTING) {
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: CONNECT_B3_ACTIVE_IND during disconnect for NCCI %#x\n",
			i->vname, NCCI);
		return;
	}

	i->isdnstate |= CAPI_ISDN_STATE_B3_UP;
	i->isdnstate &= ~CAPI_ISDN_STATE_B3_PEND;

	if (i->bproto == CC_BPROTO_RTP) {
		i->isdnstate |= CAPI_ISDN_STATE_RTP;
	} else {
		i->isdnstate &= ~CAPI_ISDN_STATE_RTP;
		i->B3q = (CAPI_MAX_B3_BLOCK_SIZE * 3);
	}

	if ((i->FaxState & CAPI_FAX_STATE_SENDMODE)) {
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: Start sending fax.\n",
			i->vname);
		capidev_send_faxdata(i);
	}

	if ((i->isdnstate & CAPI_ISDN_STATE_B3_CHANGE)) {
		i->isdnstate &= ~CAPI_ISDN_STATE_B3_CHANGE;
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: B3 protocol changed.\n",
			i->vname);
		return;
	}

	if (!i->owner) {
		cc_log(CW_LOG_ERROR, "%s: No channel for interface!\n",
			i->vname);
		return;
	}

	if (i->FaxState & CAPI_FAX_STATE_ACTIVE) {
		cc_verbose(3, 1, VERBOSE_PREFIX_3 "%s: Fax connection, no EC/DTMF\n",
			i->vname);
	} else {
		capi_echo_canceller(i->owner, EC_FUNCTION_ENABLE);
		capi_detect_dtmf(i->owner, 1);
	}

	if (i->state == CAPI_STATE_CONNECTED) {
		capi_signal_answer(i);
	}
	return;
}

/*
 * CAPI DISCONNECT_B3_IND
 */
static void capidev_handle_disconnect_b3_indication(_cmsg *CMSG, unsigned int PLCI, unsigned int NCCI, struct capi_pvt *i)
{
	_cmsg CMSG2;

	DISCONNECT_B3_RESP_HEADER(&CMSG2, capi_ApplID, HEADER_MSGNUM(CMSG), 0);
	DISCONNECT_B3_RESP_NCCI(&CMSG2) = NCCI;
	_capi_put_cmsg(&CMSG2);

	return_on_no_interface("DISCONNECT_B3_IND");

	i->isdnstate &= ~(CAPI_ISDN_STATE_B3_UP | CAPI_ISDN_STATE_B3_PEND);

	i->reasonb3 = DISCONNECT_B3_IND_REASON_B3(CMSG);
	i->NCCI = 0;

	if ((i->FaxState & CAPI_FAX_STATE_ACTIVE) && (i->owner)) {
		char buffer[CAPI_MAX_STRING];
		char *infostring;
		unsigned char *ncpi = (unsigned char *)DISCONNECT_B3_IND_NCPI(CMSG);
		/* if we have fax infos, set them as variables */
		snprintf(buffer, CAPI_MAX_STRING-1, "%d", i->reasonb3);
		pbx_builtin_setvar_helper(i->owner, "FAXREASON", buffer);
		if (i->reasonb3 == 0) {
			pbx_builtin_setvar_helper(i->owner, "FAXREASONTEXT", "OK");
		} else if ((infostring = capi_info_string(i->reasonb3)) != NULL) {
			pbx_builtin_setvar_helper(i->owner, "FAXREASONTEXT", infostring);
		} else {
			pbx_builtin_setvar_helper(i->owner, "FAXREASONTEXT", "");
		}
		if (ncpi) {
			snprintf(buffer, CAPI_MAX_STRING-1, "%d", read_capi_word(&ncpi[1]));
			pbx_builtin_setvar_helper(i->owner, "FAXRATE", buffer);
			snprintf(buffer, CAPI_MAX_STRING-1, "%d", read_capi_word(&ncpi[3]));
			pbx_builtin_setvar_helper(i->owner, "FAXRESOLUTION", buffer);
			snprintf(buffer, CAPI_MAX_STRING-1, "%d", read_capi_word(&ncpi[5]));
			pbx_builtin_setvar_helper(i->owner, "FAXFORMAT", buffer);
			snprintf(buffer, CAPI_MAX_STRING-1, "%d", read_capi_word(&ncpi[7]));
			pbx_builtin_setvar_helper(i->owner, "FAXPAGES", buffer);
			memcpy(buffer, &ncpi[10], ncpi[9]);
			buffer[ncpi[9]] = 0;
			pbx_builtin_setvar_helper(i->owner, "FAXID", buffer);
		}
	}

	if ((i->state == CAPI_STATE_DISCONNECTING) ||
	    ((!(i->isdnstate & CAPI_ISDN_STATE_B3_SELECT)) &&
	     (i->FaxState & CAPI_FAX_STATE_SENDMODE))) {
		/* active disconnect */
		DISCONNECT_REQ_HEADER(&CMSG2, capi_ApplID, get_capi_MessageNumber(), 0);
		DISCONNECT_REQ_PLCI(&CMSG2) = PLCI;
		_capi_put_cmsg(&CMSG2);
	}

	capi_controllers[i->controller]->nfreebchannels++;
}

/*
 * CAPI CONNECT_B3_IND
 */
static void capidev_handle_connect_b3_indication(_cmsg *CMSG, unsigned int PLCI, unsigned int NCCI, struct capi_pvt *i)
{
	_cmsg CMSG2;

	/* then send a CONNECT_B3_RESP */
	CONNECT_B3_RESP_HEADER(&CMSG2, capi_ApplID, HEADER_MSGNUM(CMSG), 0);
	CONNECT_B3_RESP_NCCI(&CMSG2) = NCCI;
	CONNECT_B3_RESP_REJECT(&CMSG2) = 0;
	CONNECT_B3_RESP_NCPI(&CMSG2) = capi_rtp_ncpi(i);
	_capi_put_cmsg(&CMSG2);

	return_on_no_interface("CONNECT_B3_IND");

	i->NCCI = NCCI;

	return;
}

/*
 * CAPI DISCONNECT_IND
 */
static void capidev_handle_disconnect_indication(_cmsg *CMSG, unsigned int PLCI, unsigned int NCCI, struct capi_pvt *i)
{
	_cmsg CMSG2;
	struct cw_frame fr = { CW_FRAME_CONTROL, CW_CONTROL_HANGUP, };
	int state;

	DISCONNECT_RESP_HEADER(&CMSG2, capi_ApplID, HEADER_MSGNUM(CMSG) , 0);
	DISCONNECT_RESP_PLCI(&CMSG2) = PLCI;
	_capi_put_cmsg(&CMSG2);
	
	show_capi_info(i, DISCONNECT_IND_REASON(CMSG));

	return_on_no_interface("DISCONNECT_IND");

	state = i->state;
	i->state = CAPI_STATE_DISCONNECTED;

	i->reason = DISCONNECT_IND_REASON(CMSG);

	if ((i->owner) && (i->owner->hangupcause == 0)) {
		/* set hangupcause, in case there is no 
		 * "cause" information element:
		 */
		i->owner->hangupcause =
			((i->reason & 0xFF00) == 0x3400) ?
			i->reason & 0x7F : CW_CAUSE_NORMAL_CLEARING;
	}

	if (i->FaxState & CAPI_FAX_STATE_ACTIVE) {
		/* in capiFax */
		switch (i->reason) {
		case 0x3400:
		case 0x3490:
		case 0x349f:
			if (i->reasonb3 != 0)
				i->FaxState |= CAPI_FAX_STATE_ERROR;
			break;
		default:
			i->FaxState |= CAPI_FAX_STATE_ERROR;
		}
		i->FaxState &= ~CAPI_FAX_STATE_ACTIVE;
	}

	if ((i->owner) &&
	    ((state == CAPI_STATE_DID) || (state == CAPI_STATE_INCALL)) &&
	    (!(i->isdnstate & CAPI_ISDN_STATE_PBX))) {
		/* the pbx was not started yet */
		cc_verbose(4, 1, VERBOSE_PREFIX_3 "%s: DISCONNECT_IND on incoming without pbx, doing hangup.\n",
			i->vname);
		capi_channel_task(i->owner, CAPI_CHANNEL_TASK_HANGUP); 
		return;
	}

	if (DISCONNECT_IND_REASON(CMSG) == 0x34a2) {
		fr.subclass = CW_CONTROL_CONGESTION;
	}

	if (state == CAPI_STATE_DISCONNECTING) {
		interface_cleanup(i);
	} else {
		local_queue_frame(i, &fr);
	}
	return;
}

/*
 * CAPI CONNECT_IND
 */
static void capidev_handle_connect_indication(_cmsg *CMSG, unsigned int PLCI, unsigned int NCCI, struct capi_pvt **interface)
{
	struct capi_pvt *i;
	_cmsg CMSG2;
	char *DNID;
	char *CID;
	int callernplan = 0, callednplan = 0;
	int controller = 0;
	char *msn;
	char buffer[CAPI_MAX_STRING];
	char buffer_r[CAPI_MAX_STRING];
	char *buffer_rp = buffer_r;
	char *magicmsn = "*\0";
	char *emptydnid = "\0";
	int callpres = 0;
	char bchannelinfo[2] = { '0', 0 };

	if (*interface) {
	    /* chan_capi does not support 
	     * double connect indications !
	     * (This is used to update 
	     *  telephone numbers and 
	     *  other information)
	     */
		return;
	}

	DNID = capi_number(CONNECT_IND_CALLEDPARTYNUMBER(CMSG), 1);
	if (!DNID) {
		DNID = emptydnid;
	}
	if (CONNECT_IND_CALLEDPARTYNUMBER(CMSG)[0] > 1) {
		callednplan = (CONNECT_IND_CALLEDPARTYNUMBER(CMSG)[1] & 0x7f);
	}

	CID = capi_number(CONNECT_IND_CALLINGPARTYNUMBER(CMSG), 2);
	if (CONNECT_IND_CALLINGPARTYNUMBER(CMSG)[0] > 1) {
		callernplan = (CONNECT_IND_CALLINGPARTYNUMBER(CMSG)[1] & 0x7f);
		callpres = (CONNECT_IND_CALLINGPARTYNUMBER(CMSG)[2] & 0x63);
	}
	controller = PLCI & 0xff;
	
	cc_verbose(1, 1, VERBOSE_PREFIX_3 "CONNECT_IND (PLCI=%#x,DID=%s,CID=%s,CIP=%#x,CONTROLLER=%#x)\n",
		PLCI, DNID, CID, CONNECT_IND_CIPVALUE(CMSG), controller);

	if (CONNECT_IND_BCHANNELINFORMATION(CMSG)) {
		bchannelinfo[0] = CONNECT_IND_BCHANNELINFORMATION(CMSG)[1] + '0';
	}

	/* well...somebody is calling us. let's set up a channel */
	cc_mutex_lock(&iflock);
	for (i = iflist; i; i = i->next) {
		if (i->owner) {
			/* has already owner */
			continue;
		}
		if (i->controller != controller) {
			continue;
		}
		if (i->channeltype == CAPI_CHANNELTYPE_B) {
			if (bchannelinfo[0] != '0')
				continue;
		} else {
			if (bchannelinfo[0] == '0')
				continue;
		}
		cc_copy_string(buffer, i->incomingmsn, sizeof(buffer));
		for (msn = strtok_r(buffer, ",", &buffer_rp); msn; msn = strtok_r(NULL, ",", &buffer_rp)) {
			if (!strlen(DNID)) {
				/* if no DNID, only accept if '*' was specified */
				if (strncasecmp(msn, magicmsn, strlen(msn))) {
					continue;
				}
				cc_copy_string(i->dnid, emptydnid, sizeof(i->dnid));
			} else {
				/* make sure the number match exactly or may match on ptp mode */
				cc_verbose(4, 1, VERBOSE_PREFIX_4 "%s: msn='%s' DNID='%s' %s\n",
					i->vname, msn, DNID,
					(i->isdnmode == CAPI_ISDNMODE_MSN)?"MSN":"DID");
				if ((strcasecmp(msn, DNID)) &&
				   ((i->isdnmode == CAPI_ISDNMODE_MSN) ||
				    (strlen(msn) >= strlen(DNID)) ||
				    (strncasecmp(msn, DNID, strlen(msn)))) &&
				   (strncasecmp(msn, magicmsn, strlen(msn)))) {
					continue;
				}
				cc_copy_string(i->dnid, DNID, sizeof(i->dnid));
			}
			if (CID != NULL) {
				if ((callernplan & 0x70) == CAPI_ETSI_NPLAN_NATIONAL)
					snprintf(i->cid, (sizeof(i->cid)-1), "%s%s%s",
						i->prefix, capi_national_prefix, CID);
				else if ((callernplan & 0x70) == CAPI_ETSI_NPLAN_INTERNAT)
					snprintf(i->cid, (sizeof(i->cid)-1), "%s%s%s",
						i->prefix, capi_international_prefix, CID);
				else
					snprintf(i->cid, (sizeof(i->cid)-1), "%s%s",
						i->prefix, CID);
			} else {
				cc_copy_string(i->cid, emptyid, sizeof(i->cid));
			}
			i->cip = CONNECT_IND_CIPVALUE(CMSG);
			i->PLCI = PLCI;
			i->MessageNumber = HEADER_MSGNUM(CMSG);
			i->cid_ton = callernplan;

			capi_new(i, CW_STATE_DOWN);
			if (i->isdnmode == CAPI_ISDNMODE_DID) {
				i->state = CAPI_STATE_DID;
			} else {
				i->state = CAPI_STATE_INCALL;
			}

			if (!i->owner) {
				interface_cleanup(i);
				break;
			}
 			i->owner->transfercapability = cip2tcap(i->cip);
			if (tcap_is_digital(i->owner->transfercapability)) {
				i->bproto = CC_BPROTO_TRANSPARENT;
			}
			i->owner->cid.cid_pres = callpres;
			cc_verbose(3, 0, VERBOSE_PREFIX_2 "%s: Incoming call '%s' -> '%s'\n",
				i->vname, i->cid, i->dnid);

			*interface = i;
			cc_mutex_unlock(&iflock);
			cc_mutex_lock(&i->lock);
		
			pbx_builtin_setvar_helper(i->owner, "TRANSFERCAPABILITY", transfercapability2str(i->owner->transfercapability));
			pbx_builtin_setvar_helper(i->owner, "BCHANNELINFO", bchannelinfo);
			sprintf(buffer, "%d", callednplan);
			pbx_builtin_setvar_helper(i->owner, "CALLEDTON", buffer);
			/*
			pbx_builtin_setvar_helper(i->owner, "CALLINGSUBADDRESS",
				CONNECT_IND_CALLINGPARTYSUBADDRESS(CMSG));
			pbx_builtin_setvar_helper(i->owner, "CALLEDSUBADDRESS",
				CONNECT_IND_CALLEDPARTYSUBADDRESS(CMSG));
			pbx_builtin_setvar_helper(i->owner, "USERUSERINFO",
				CONNECT_IND_USERUSERDATA(CMSG));
			*/
			/* TODO : set some more variables on incoming call */
			/*
			pbx_builtin_setvar_helper(i->owner, "ANI2", buffer);
			pbx_builtin_setvar_helper(i->owner, "SECONDCALLERID", buffer);
			*/
			if ((i->isdnmode == CAPI_ISDNMODE_MSN) && (i->immediate)) {
				/* if we don't want to wait for SETUP/SENDING-COMPLETE in MSN mode */
				start_pbx_on_match(i, PLCI, HEADER_MSGNUM(CMSG));
			}
			return;
		}
	}
	cc_mutex_unlock(&iflock);

	/* obviously we are not called...so tell capi to ignore this call */

	if (capidebug) {
		cc_log(CW_LOG_WARNING, "did not find device for msn = %s\n", DNID);
	}
	
	CONNECT_RESP_HEADER(&CMSG2, capi_ApplID, HEADER_MSGNUM(CMSG), 0);
	CONNECT_RESP_PLCI(&CMSG2) = CONNECT_IND_PLCI(CMSG);
	CONNECT_RESP_REJECT(&CMSG2) = 1; /* ignore */
	_capi_put_cmsg(&CMSG2);
	return;
}

/*
 * CAPI FACILITY_CONF
 */
static void capidev_handle_facility_confirmation(_cmsg *CMSG, unsigned int PLCI, unsigned int NCCI, struct capi_pvt *i)
{
	int selector;

	if (i == NULL)
		return;

	selector = FACILITY_CONF_FACILITYSELECTOR(CMSG);

	if (selector == FACILITYSELECTOR_DTMF) {
		cc_verbose(2, 1, VERBOSE_PREFIX_4 "%s: DTMF conf(PLCI=%#x)\n",
			i->vname, PLCI);
		return;
	}
	if (selector == i->ecSelector) {
		if (FACILITY_CONF_INFO(CMSG)) {
			cc_verbose(2, 0, VERBOSE_PREFIX_3 "%s: Error setting up echo canceller (PLCI=%#x)\n",
				i->vname, PLCI);
			return;
		}
		if (FACILITY_CONF_FACILITYCONFIRMATIONPARAMETER(CMSG)[1] == EC_FUNCTION_DISABLE) {
			cc_verbose(3, 0, VERBOSE_PREFIX_3 "%s: Echo canceller successfully disabled (PLCI=%#x)\n",
				i->vname, PLCI);
		} else {
			cc_verbose(3, 0, VERBOSE_PREFIX_3 "%s: Echo canceller successfully set up (PLCI=%#x)\n",
				i->vname, PLCI);
		}
		return;
	}
	if (selector == FACILITYSELECTOR_SUPPLEMENTARY) {
		/* HOLD */
		if ((FACILITY_CONF_FACILITYCONFIRMATIONPARAMETER(CMSG)[1] == 0x2) &&
		    (FACILITY_CONF_FACILITYCONFIRMATIONPARAMETER(CMSG)[2] == 0x0) &&
		    ((FACILITY_CONF_FACILITYCONFIRMATIONPARAMETER(CMSG)[4] != 0x0) ||
		     (FACILITY_CONF_FACILITYCONFIRMATIONPARAMETER(CMSG)[5] != 0x0))) {
			cc_verbose(2, 0, VERBOSE_PREFIX_3 "%s: Call on hold (PLCI=%#x)\n",
				i->vname, PLCI);
		}
		return;
	}
	if (selector == FACILITYSELECTOR_LINE_INTERCONNECT) {
		if ((FACILITY_CONF_FACILITYCONFIRMATIONPARAMETER(CMSG)[1] == 0x1) &&
		    (FACILITY_CONF_FACILITYCONFIRMATIONPARAMETER(CMSG)[2] == 0x0)) {
			/* enable */
			if (FACILITY_CONF_FACILITYCONFIRMATIONPARAMETER(CMSG)[0] > 12) {
				show_capi_info(i, read_capi_word(&FACILITY_CONF_FACILITYCONFIRMATIONPARAMETER(CMSG)[12]));
			}
		} else {
			/* disable */
			if (FACILITY_CONF_FACILITYCONFIRMATIONPARAMETER(CMSG)[0] > 12) {
				show_capi_info(i, read_capi_word(&FACILITY_CONF_FACILITYCONFIRMATIONPARAMETER(CMSG)[12]));
			}
		}
		return;
	}
	cc_log(CW_LOG_ERROR, "%s: unhandled FACILITY_CONF 0x%x\n",
		i->vname, FACILITY_CONF_FACILITYSELECTOR(CMSG));
}

/*
 * show error in confirmation
 */
static void show_capi_conf_error(struct capi_pvt *i, 
				 unsigned int PLCI, u_int16_t wInfo, 
				 u_int16_t wCmd)
{
	const char *name = channeltype;

	if (i)
		name = i->vname;
	
	if ((wCmd == CAPI_P_CONF(ALERT)) && (wInfo == 0x0003)) {
		/* Alert already sent by another application */
		return;
	}
		
	if (wInfo == 0x2002) {
		cc_verbose(1, 1, VERBOSE_PREFIX_3 "%s: "
			       "0x%x (wrong state) PLCI=0x%x "
			       "Command=%s,0x%04x\n",
			       name, wInfo, PLCI, capi_command_to_string(wCmd), wCmd);
	} else {
		cc_log(CW_LOG_WARNING, "%s: conf_error 0x%04x "
			"PLCI=0x%x Command=%s,0x%04x\n",
			name, wInfo, PLCI, capi_command_to_string(wCmd), wCmd);
	}
	return;
}

/*
 * check special conditions, wake waiting threads and send outstanding commands
 * for the given interface
 */
static void capidev_post_handling(struct capi_pvt *i, _cmsg *CMSG)
{
	unsigned short capicommand = CAPICMD(CMSG->Command, CMSG->Subcommand);

	if ((i->waitevent == CAPI_WAITEVENT_B3_UP) &&
	    ((i->isdnstate & CAPI_ISDN_STATE_B3_UP))) {
		i->waitevent = 0;
		cw_cond_signal(&i->event_trigger);
		cc_verbose(4, 1, "%s: found and signal for b3 up state.\n",
			i->vname);
		return;
	}
	if ((i->waitevent == CAPI_WAITEVENT_B3_DOWN) &&
	    (!(i->isdnstate & (CAPI_ISDN_STATE_B3_UP | CAPI_ISDN_STATE_B3_PEND)))) {
		i->waitevent = 0;
		cw_cond_signal(&i->event_trigger);
		cc_verbose(4, 1, "%s: found and signal for b3 down state.\n",
			i->vname);
		return;
	}
	if ((i->waitevent == CAPI_WAITEVENT_FAX_FINISH) &&
	    (!(i->FaxState & CAPI_FAX_STATE_ACTIVE))) {
		i->waitevent = 0;
		cw_cond_signal(&i->event_trigger);
		cc_verbose(4, 1, "%s: found and signal for finished fax state.\n",
			i->vname);
		return;
	}
	if ((i->waitevent == CAPI_WAITEVENT_ANSWER_FINISH) &&
	    (i->state != CAPI_STATE_ANSWERING)) {
		i->waitevent = 0;
		cw_cond_signal(&i->event_trigger);
		cc_verbose(4, 1, "%s: found and signal for finished ANSWER state.\n",
			i->vname);
		return;
	}
	if (i->waitevent == capicommand) {
		i->waitevent = 0;
		cw_cond_signal(&i->event_trigger);
		cc_verbose(4, 1, "%s: found and signal for %s\n",
			i->vname, capi_cmd2str(CMSG->Command, CMSG->Subcommand));
		return;
	}
}

/*
 * handle CAPI msg
 */
static void capidev_handle_msg(_cmsg *CMSG)
{
	unsigned int NCCI = HEADER_CID(CMSG);
	unsigned int PLCI = (NCCI & 0xffff);
	unsigned short wCmd = HEADER_CMD(CMSG);
	unsigned short wMsgNum = HEADER_MSGNUM(CMSG);
	unsigned short wInfo = 0xffff;
	struct capi_pvt *i = find_interface_by_plci(PLCI);

	if ((wCmd == CAPI_P_IND(DATA_B3)) ||
	    (wCmd == CAPI_P_CONF(DATA_B3))) {
		cc_verbose(7, 1, "%s\n", capi_cmsg2str(CMSG));
	} else {
		cc_verbose(4, 1, "%s\n", capi_cmsg2str(CMSG));
	}

	if (i != NULL)
		cc_mutex_lock(&i->lock);

	/* main switch table */

	switch (wCmd) {

	  /*
	   * CAPI indications
	   */
	case CAPI_P_IND(CONNECT):
		capidev_handle_connect_indication(CMSG, PLCI, NCCI, &i);
		break;
	case CAPI_P_IND(DATA_B3):
		capidev_handle_data_b3_indication(CMSG, PLCI, NCCI, i);
		break;
	case CAPI_P_IND(CONNECT_B3):
		capidev_handle_connect_b3_indication(CMSG, PLCI, NCCI, i);
		break;
	case CAPI_P_IND(CONNECT_B3_ACTIVE):
		capidev_handle_connect_b3_active_indication(CMSG, PLCI, NCCI, i);
		break;
	case CAPI_P_IND(DISCONNECT_B3):
		capidev_handle_disconnect_b3_indication(CMSG, PLCI, NCCI, i);
		break;
	case CAPI_P_IND(DISCONNECT):
		capidev_handle_disconnect_indication(CMSG, PLCI, NCCI, i);
		break;
	case CAPI_P_IND(FACILITY):
		capidev_handle_facility_indication(CMSG, PLCI, NCCI, i);
		break;
	case CAPI_P_IND(INFO):
		capidev_handle_info_indication(CMSG, PLCI, NCCI, i);
		break;
	case CAPI_P_IND(CONNECT_ACTIVE):
		capidev_handle_connect_active_indication(CMSG, PLCI, NCCI, i);
		break;
	case CAPI_P_IND(MANUFACTURER):
		capidev_handle_manufacturer_indication(CMSG, PLCI, NCCI, i);
		break;

	  /*
	   * CAPI confirmations
	   */

	case CAPI_P_CONF(FACILITY):
		wInfo = FACILITY_CONF_INFO(CMSG);
		capidev_handle_facility_confirmation(CMSG, PLCI, NCCI, i);
		break;
	case CAPI_P_CONF(CONNECT):
		wInfo = CONNECT_CONF_INFO(CMSG);
		if (i) {
			cc_log(CW_LOG_ERROR, "CAPI: CONNECT_CONF for already "
				"defined interface received\n");
			break;
		}
		i = find_interface_by_msgnum(wMsgNum);
		if ((i == NULL) || (!i->owner))
			break;
		cc_verbose(1, 1, VERBOSE_PREFIX_3 "%s: received CONNECT_CONF PLCI = %#x\n",
			i->vname, PLCI);
		if (wInfo == 0) {
			i->PLCI = PLCI;
		} else {
			/* error in connect, so set correct state and signal busy */
			i->state = CAPI_STATE_DISCONNECTED;
			struct cw_frame fr = { CW_FRAME_CONTROL, CW_CONTROL_BUSY, };
			local_queue_frame(i, &fr);
		}
		break;
	case CAPI_P_CONF(CONNECT_B3):
		wInfo = CONNECT_B3_CONF_INFO(CMSG);
		if(i == NULL) break;
		if (wInfo == 0) {
			i->NCCI = NCCI;
		} else {
			i->isdnstate &= ~(CAPI_ISDN_STATE_B3_UP | CAPI_ISDN_STATE_B3_PEND);
		}
		break;
	case CAPI_P_CONF(ALERT):
		wInfo = ALERT_CONF_INFO(CMSG);
		if(i == NULL) break;
		if (!i->owner) break;
		if ((wInfo & 0xff00) == 0) {
			if (i->state != CAPI_STATE_DISCONNECTING) {
				i->state = CAPI_STATE_ALERTING;
			}
		}
		break;	    
	case CAPI_P_CONF(SELECT_B_PROTOCOL):
		wInfo = SELECT_B_PROTOCOL_CONF_INFO(CMSG);
		if(i == NULL) break;
		if (!wInfo) {
			i->isdnstate &= ~CAPI_ISDN_STATE_B3_SELECT;
			if ((i->outgoing) && (i->FaxState & CAPI_FAX_STATE_SENDMODE)) {
				cc_start_b3(i);
			}
			if ((i->owner) && (i->FaxState & CAPI_FAX_STATE_ACTIVE)) {
				capi_echo_canceller(i->owner, EC_FUNCTION_DISABLE);
				capi_detect_dtmf(i->owner, 0);
			}
		}
		break;
	case CAPI_P_CONF(DATA_B3):
		wInfo = DATA_B3_CONF_INFO(CMSG);
		if ((i) && (i->B3q > 0) && (i->isdnstate & CAPI_ISDN_STATE_RTP)) {
			i->B3q--;
		}
		if ((i) && (i->FaxState & CAPI_FAX_STATE_SENDMODE)) {
			capidev_send_faxdata(i);
		}
		break;
 
	case CAPI_P_CONF(DISCONNECT):
		wInfo = DISCONNECT_CONF_INFO(CMSG);
		break;

	case CAPI_P_CONF(DISCONNECT_B3):
		wInfo = DISCONNECT_B3_CONF_INFO(CMSG);
		break;

	case CAPI_P_CONF(LISTEN):
		wInfo = LISTEN_CONF_INFO(CMSG);
		break;

	case CAPI_P_CONF(INFO):
		wInfo = INFO_CONF_INFO(CMSG);
		break;

	default:
		cc_log(CW_LOG_ERROR, "CAPI: Command=%s,0x%04x",
			capi_command_to_string(wCmd), wCmd);
		break;
	}

	if (wInfo != 0xffff) {
		if (wInfo) {
			show_capi_conf_error(i, PLCI, wInfo, wCmd);
		}
		show_capi_info(i, wInfo);
	}

	if (i == NULL) {
		cc_verbose(2, 1, VERBOSE_PREFIX_4
			"CAPI: Command=%s,0x%04x: no interface for PLCI="
			"%#x, MSGNUM=%#x!\n", capi_command_to_string(wCmd),
			wCmd, PLCI, wMsgNum);
	} else {
		capidev_post_handling(i, CMSG);
		cc_mutex_unlock(&i->lock);
	}

	return;
}

/*
 * deflect a call
 */
static int pbx_capi_call_deflect(struct cw_channel *c, int argc, char **argv)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);
	_cmsg	CMSG;
	char	fac[64];
	int	res = 0;
	int numberlen;

	if (argc != 1 || !argv[0][0]) {
		cc_log(CW_LOG_ERROR, "capi deflection requires an argument (destination phone number)\n");
		return -1;
	}

	numberlen = strlen(argv[0]);

	if (numberlen > 35) {
		cc_log(CW_LOG_WARNING, "capi deflection does only support phone number up to 35 digits\n");
		return -1;
	}

	if (!(capi_controllers[i->controller]->CD)) {
		cc_log(CW_LOG_NOTICE,"%s: CALL DEFLECT for %s not supported by controller.\n",
			i->vname, c->name);
		return -1;
	}

	cc_mutex_lock(&i->lock);

	if ((i->state != CAPI_STATE_INCALL) &&
	    (i->state != CAPI_STATE_DID) &&
	    (i->state != CAPI_STATE_ALERTING)) {
		cc_mutex_unlock(&i->lock);
		cc_log(CW_LOG_WARNING, "wrong state of call for call deflection\n");
		return -1;
	}
	if (i->state != CAPI_STATE_ALERTING) {
		pbx_capi_alert(c);
	}
	
	fac[0] = 0x0a + numberlen; /* length */
	fac[1] = 0x0d; /* call deflection */
	fac[2] = 0x00;
	fac[3] = 0x07 + numberlen; /* struct len */
	fac[4] = 0x01; /* display of own address allowed */
	fac[5] = 0x00;
	fac[6] = 0x03 + numberlen;
	fac[7] = 0x00; /* type of facility number */
	fac[8] = 0x00; /* number plan */
	fac[9] = 0x00; /* presentation allowed */
	fac[10 + numberlen] = 0x00; /* subaddress len */

	memcpy((unsigned char *)fac + 10, argv[0], numberlen);

	FACILITY_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(),0);
	FACILITY_REQ_PLCI(&CMSG) = i->PLCI;
	FACILITY_REQ_FACILITYSELECTOR(&CMSG) = FACILITYSELECTOR_SUPPLEMENTARY;
	FACILITY_REQ_FACILITYREQUESTPARAMETER(&CMSG) = (_cstruct)&fac;
	
	_capi_put_cmsg_wait_conf(i, &CMSG);

	cc_mutex_unlock(&i->lock);

	cc_verbose(2, 1, VERBOSE_PREFIX_3 "%s: sent FACILITY_REQ for CD PLCI = %#x\n",
		i->vname, i->PLCI);

	return(res);
}

/*
 * retrieve a hold on call
 */
static int pbx_capi_retrieve(struct cw_channel *c, int argc, char **argv)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c); 
	_cmsg	CMSG;
	char	fac[4];
	unsigned int plci = 0;

	if (c->tech->type == channeltype) {
		plci = i->onholdPLCI;
	} else {
		i = NULL;
	}

	if (argc > 0 && argv[0][0]) {
		plci = (unsigned int)strtoul(argv[0], NULL, 0);
		cc_mutex_lock(&iflock);
		for (i = iflist; i; i = i->next) {
			if (i->onholdPLCI == plci)
				break;
		}
		cc_mutex_unlock(&iflock);
		if (!i) {
			plci = 0;
		}
	}

	if (!i) {
		cc_log(CW_LOG_WARNING, "%s is not valid or not on capi hold to retrieve!\n",
			c->name);
		return 0;
	}

	if ((i->state != CAPI_STATE_ONHOLD) &&
	    (i->isdnstate & CAPI_ISDN_STATE_HOLD)) {
		int waitcount = 20;
		while ((waitcount > 0) && (i->state != CAPI_STATE_ONHOLD)) {
			usleep(10000);
			waitcount--;
		}
	}

	if ((!plci) || (i->state != CAPI_STATE_ONHOLD)) {
		cc_log(CW_LOG_WARNING, "%s: 0x%x is not valid or not on hold to retrieve!\n",
			i->vname, plci);
		return 0;
	}
	cc_verbose(2, 1, VERBOSE_PREFIX_4 "%s: using PLCI=%#x for retrieve\n",
		i->vname, plci);

	if (!(capi_controllers[i->controller]->holdretrieve)) {
		cc_log(CW_LOG_NOTICE,"%s: RETRIEVE for %s not supported by controller.\n",
			i->vname, c->name);
		return -1;
	}

	fac[0] = 3;	/* len */
	fac[1] = 0x03;	/* retrieve */
	fac[2] = 0x00;
	fac[3] = 0;	

	FACILITY_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(),0);
	FACILITY_REQ_PLCI(&CMSG) = plci;
	FACILITY_REQ_FACILITYSELECTOR(&CMSG) = FACILITYSELECTOR_SUPPLEMENTARY;
	FACILITY_REQ_FACILITYREQUESTPARAMETER(&CMSG) = (_cstruct)&fac;

	_capi_put_cmsg(&CMSG);
	cc_verbose(2, 1, VERBOSE_PREFIX_4 "%s: sent RETRIEVE for PLCI=%#x\n",
		i->vname, plci);

	i->isdnstate &= ~CAPI_ISDN_STATE_HOLD;
	pbx_builtin_setvar_helper(i->owner, "_CALLERHOLDID", NULL);

	return 0;
}

/*
 * explicit transfer a held call
 */
static int pbx_capi_ect(struct cw_channel *c, int argc, char **argv)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);
	struct capi_pvt *ii = NULL;
	_cmsg CMSG;
	char fac[8];
	struct cw_var_t *var;
	unsigned int plci = 0;

	if (argc > 0 && argv[0][0]) {
		plci = (unsigned int)strtoul(argv[0], NULL, 0);
	} else if ((var = pbx_builtin_getvar_helper(c, CW_KEYWORD_CALLERHOLDID, "CALLERHOLDID"))) {
		plci = (unsigned int)strtoul(var->value, NULL, 0);
		cw_object_put(var);
	}

	if (!plci) {
		cc_log(CW_LOG_WARNING, "%s: No id for ECT !\n", i->vname);
		return -1;
	}

	cc_mutex_lock(&iflock);
	for (ii = iflist; ii; ii = ii->next) {
		if (ii->onholdPLCI == plci)
			break;
	}
	cc_mutex_unlock(&iflock);

	if (!ii) {
		cc_log(CW_LOG_WARNING, "%s: 0x%x is not on hold !\n",
			i->vname, plci);
		return -1;
	}

	cc_verbose(2, 1, VERBOSE_PREFIX_4 "%s: using PLCI=%#x for ECT\n",
		i->vname, plci);

	if (!(capi_controllers[i->controller]->ECT)) {
		cc_log(CW_LOG_WARNING, "%s: ECT for %s not supported by controller.\n",
			i->vname, c->name);
		return -1;
	}

	if (!(ii->isdnstate & CAPI_ISDN_STATE_HOLD)) {
		cc_log(CW_LOG_WARNING, "%s: PLCI %#x (%s) is not on hold for ECT\n",
			i->vname, plci, ii->vname);
		return -1;
	}

	cc_disconnect_b3(i, 1);

	if (i->state != CAPI_STATE_CONNECTED) {
		cc_log(CW_LOG_WARNING, "%s: destination not connected for ECT\n",
			i->vname);
		return -1;
	}

	fac[0] = 7;	/* len */
	fac[1] = 0x06;	/* ECT (function) */
	fac[2] = 0x00;
	fac[3] = 4;	/* len / sservice specific parameter , cstruct */
	write_capi_dword(&(fac[4]), plci);

	FACILITY_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(), 0);
	FACILITY_REQ_CONTROLLER(&CMSG) = i->controller;
	FACILITY_REQ_PLCI(&CMSG) = plci; /* implicit ECT */
	FACILITY_REQ_FACILITYSELECTOR(&CMSG) = FACILITYSELECTOR_SUPPLEMENTARY;
	FACILITY_REQ_FACILITYREQUESTPARAMETER(&CMSG) = (_cstruct)&fac;

	cc_mutex_lock(&ii->lock);
	_capi_put_cmsg_wait_conf(ii, &CMSG);
	
	ii->isdnstate &= ~CAPI_ISDN_STATE_HOLD;
	ii->isdnstate |= CAPI_ISDN_STATE_ECT;
	i->isdnstate |= CAPI_ISDN_STATE_ECT;
	
	cc_mutex_unlock(&ii->lock);

	cc_verbose(2, 1, VERBOSE_PREFIX_4 "%s: sent ECT for PLCI=%#x to PLCI=%#x\n",
		i->vname, plci, i->PLCI);

	return 0;
}

/*
 * hold a call
 */
static int pbx_capi_hold(struct cw_channel *c, int argc, char **argv)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);
	_cmsg	CMSG;
	char buffer[16];
	char	fac[4];

	/*  TODO: support holdtype notify */

	if ((i->isdnstate & CAPI_ISDN_STATE_HOLD)) {
		cc_log(CW_LOG_NOTICE,"%s: %s already on hold.\n",
			i->vname, c->name);
		return 0;
	}

	if (!(i->isdnstate & CAPI_ISDN_STATE_B3_UP)) {
		cc_log(CW_LOG_NOTICE,"%s: Cannot put on hold %s while not connected.\n",
			i->vname, c->name);
		return 0;
	}
	if (!(capi_controllers[i->controller]->holdretrieve)) {
		cc_log(CW_LOG_NOTICE,"%s: HOLD for %s not supported by controller.\n",
			i->vname, c->name);
		return 0;
	}

	fac[0] = 3;	/* len */
	fac[1] = 0x02;	/* this is a HOLD up */
	fac[2] = 0x00;
	fac[3] = 0;	

	FACILITY_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(),0);
	FACILITY_REQ_PLCI(&CMSG) = i->PLCI;
	FACILITY_REQ_FACILITYSELECTOR(&CMSG) = FACILITYSELECTOR_SUPPLEMENTARY;
	FACILITY_REQ_FACILITYREQUESTPARAMETER(&CMSG) = (_cstruct)&fac;

	_capi_put_cmsg(&CMSG);
	cc_verbose(2, 1, VERBOSE_PREFIX_4 "%s: sent HOLD for PLCI=%#x\n",
		i->vname, i->PLCI);

	i->onholdPLCI = i->PLCI;
	i->isdnstate |= CAPI_ISDN_STATE_HOLD;

	snprintf(buffer, sizeof(buffer) - 1, "%d", i->PLCI);
	if (argc > 0) {
		pbx_builtin_setvar_helper(i->owner, argv[0], buffer);
	}
	pbx_builtin_setvar_helper(i->owner, "_CALLERHOLDID", buffer);

	return 0;
}

/*
 * report malicious call
 */
static int pbx_capi_malicious(struct cw_channel *c, int argc, char **argv)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);
	_cmsg	CMSG;
	char	fac[4];

	if (!(capi_controllers[i->controller]->MCID)) {
		cc_log(CW_LOG_NOTICE, "%s: MCID for %s not supported by controller.\n",
			i->vname, c->name);
		return -1;
	}

	fac[0] = 3;      /* len */
	fac[1] = 0x0e;   /* MCID */
	fac[2] = 0x00;
	fac[3] = 0;	

	FACILITY_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(),0);
	FACILITY_REQ_PLCI(&CMSG) = i->PLCI;
	FACILITY_REQ_FACILITYSELECTOR(&CMSG) = FACILITYSELECTOR_SUPPLEMENTARY;
	FACILITY_REQ_FACILITYREQUESTPARAMETER(&CMSG) = (_cstruct)&fac;

	cc_mutex_lock(&i->lock);
	_capi_put_cmsg_wait_conf(i, &CMSG);
	cc_mutex_unlock(&i->lock);

	cc_verbose(2, 1, VERBOSE_PREFIX_4 "%s: sent MCID for PLCI=%#x\n",
		i->vname, i->PLCI);

	return 0;
}

/*
 * set echo cancel
 */
static int pbx_capi_echocancel(struct cw_channel *c, int argc, char **argv)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);

	if (argc < 1 || !argv[0][0]) {
		cc_log(CW_LOG_WARNING, "Parameter for echocancel missing.\n");
		return -1;
	}
	if (cw_true(argv[0])) {
		i->doEC = 1;
		capi_echo_canceller(c, EC_FUNCTION_ENABLE);
	} else if (cw_false(argv[0])) {
		capi_echo_canceller(c, EC_FUNCTION_DISABLE);
		i->doEC = 0;
	} else {
		cc_log(CW_LOG_WARNING, "Parameter for echocancel invalid.\n");
		return -1;
	}
	cc_verbose(2, 0, VERBOSE_PREFIX_4 "%s: echocancel switched %s\n",
		i->vname, i->doEC ? "ON":"OFF");
	return 0;
}

/*
 * set echo squelch
 */
static int pbx_capi_echosquelch(struct cw_channel *c, int argc, char **argv)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);

	if (argc < 1 || !argv[0][0]) {
		cc_log(CW_LOG_ERROR, "Parameter for echosquelch missing.\n");
		return -1;
	}
	if (cw_true(argv[0])) {
		i->doES = 1;
	} else if (cw_false(argv[0])) {
		i->doES = 0;
	} else {
		cc_log(CW_LOG_ERROR, "Parameter for echosquelch invalid.\n");
		return -1;
	}
	cc_verbose(2, 0, VERBOSE_PREFIX_4 "%s: echosquelch switched %s\n",
		i->vname, i->doES ? "ON":"OFF");
	return 0;
}

/*
 * set holdtype
 */
static int pbx_capi_holdtype(struct cw_channel *c, int argc, char **argv)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);

	if (argc < 1 || !argv[0][0]) {
		cc_log(CW_LOG_WARNING, "Parameter for holdtype missing.\n");
		return -1;
	}
	if (!strcasecmp(argv[0], "hold")) {
		i->doholdtype = CC_HOLDTYPE_HOLD;
	} else if (!strcasecmp(argv[0], "notify")) {
		i->doholdtype = CC_HOLDTYPE_NOTIFY;
	} else if (!strcasecmp(argv[0], "local")) {
		i->doholdtype = CC_HOLDTYPE_LOCAL;
	} else {
		cc_log(CW_LOG_WARNING, "Parameter for holdtype invalid.\n");
		return -1;
	}
	cc_verbose(2, 0, VERBOSE_PREFIX_4 "%s: holdtype switched to %s\n",
		i->vname, argv[0]);
	return 0;
}

/*
 * set early-B3 (progress) for incoming connections
 * (only for NT mode)
 */
static int pbx_capi_signal_progress(struct cw_channel *c, int argc, char **argv)
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);

	if ((i->state != CAPI_STATE_DID) && (i->state != CAPI_STATE_INCALL)) {
		cc_log(CW_LOG_DEBUG, "wrong channel state to signal PROGRESS\n");
		return 0;
	}
	if (!(i->ntmode)) {
		cc_log(CW_LOG_WARNING, "PROGRESS sending for non NT-mode not possible\n");
		return 0;
	}
	if ((i->isdnstate & CAPI_ISDN_STATE_B3_UP)) {
		cc_verbose(4, 1, VERBOSE_PREFIX_4 "%s: signal_progress in NT: B-channel already up\n",
			i->vname);
		return 0;
	}

	cc_select_b(i, NULL);

	return 0;
}

/*
 * struct of capi commands
 */
static struct capicommands_s {
	char *cmdname;
	int (*cmd)(struct cw_channel *, int, char **);
	int capionly;
} capicommands[] = {
	{ "progress",     pbx_capi_signal_progress, 1 },
	{ "deflect",      pbx_capi_call_deflect,    1 },
	{ "receivefax",   pbx_capi_receive_fax,     1 },
	{ "sendfax",      pbx_capi_send_fax,        1 },
	{ "echosquelch",  pbx_capi_echosquelch,     1 },
	{ "echocancel",   pbx_capi_echocancel,      1 },
	{ "malicious",    pbx_capi_malicious,       1 },
	{ "hold",         pbx_capi_hold,            1 },
	{ "holdtype",     pbx_capi_holdtype,        1 },
	{ "retrieve",     pbx_capi_retrieve,        0 },
	{ "ect",          pbx_capi_ect,             1 },
	{ NULL, NULL, 0 }
};

/*
 * capi command interface
 */
static int pbx_capicommand_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct localuser *u;
	struct capicommands_s *capicmd = &capicommands[0];
	int res = 0;

	CW_UNUSED(result);

	if (argc < 1 || !argv[0][0])
		return cw_function_syntax("capiCommand(command[, args...])");

	LOCAL_USER_ADD(u);

	while(capicmd->cmd) {
		if (!strcasecmp(capicmd->cmdname, argv[0]))
			break;
		capicmd++;
	}
	if (!capicmd->cmd) {
		LOCAL_USER_REMOVE(u);
		cc_log(CW_LOG_WARNING, "Unknown command '%s' for capiCommand\n",
			argv[0]);
		return -1;
	}

	if ((capicmd->capionly) && (chan->tech->type != channeltype)) {
		LOCAL_USER_REMOVE(u);
		cc_log(CW_LOG_WARNING, "capiCommand works on CAPI channels only, check your extensions.conf!\n");
		return -1;
	}

	res = (capicmd->cmd)(chan, argc - 1, argv + 1);
	
	LOCAL_USER_REMOVE(u);
	return(res);
}

/*
 * we don't support own indications
 */
#ifdef CC_CW_HAS_INDICATE_DATA
static int pbx_capi_indicate(struct cw_channel *c, int condition, const void *data, size_t datalen)
#else
static int pbx_capi_indicate(struct cw_channel *c, int condition)
#endif
{
	struct capi_pvt *i = CC_CHANNEL_PVT(c);
	_cmsg CMSG;
	int ret = -1;

	if (i == NULL) {
		return -1;
	}

	cc_mutex_lock(&i->lock);

	switch (condition) {
	case CW_CONTROL_RINGING:
		cc_verbose(3, 1, VERBOSE_PREFIX_2 "%s: Requested RINGING-Indication for %s\n",
			i->vname, c->name);
		/* TODO somehow enable unhold on ringing, but when wanted only */
		/* 
		if (i->isdnstate & CAPI_ISDN_STATE_HOLD)
			pbx_capi_retrieve(c, NULL, 0);
		*/
		if (i->ntmode) {
			if ((i->isdnstate & CAPI_ISDN_STATE_B3_UP)) {
				ret = 0;
			}
			pbx_capi_signal_progress(c, 0, NULL);
			pbx_capi_alert(c);
		} else {
			ret = pbx_capi_alert(c);
		}
		break;
	case CW_CONTROL_BUSY:
		cc_verbose(3, 1, VERBOSE_PREFIX_2 "%s: Requested BUSY-Indication for %s\n",
			i->vname, c->name);
		if ((i->state == CAPI_STATE_ALERTING) ||
		    (i->state == CAPI_STATE_DID) || (i->state == CAPI_STATE_INCALL)) {
			CONNECT_RESP_HEADER(&CMSG, capi_ApplID, i->MessageNumber, 0);
			CONNECT_RESP_PLCI(&CMSG) = i->PLCI;
			CONNECT_RESP_REJECT(&CMSG) = 3;
			_capi_put_cmsg(&CMSG);
			ret = 0;
		}
		if ((i->isdnstate & CAPI_ISDN_STATE_HOLD))
			pbx_capi_retrieve(c, 0, NULL);
		break;
	case CW_CONTROL_CONGESTION:
		cc_verbose(3, 1, VERBOSE_PREFIX_2 "%s: Requested CONGESTION-Indication for %s\n",
			i->vname, c->name);
		if ((i->state == CAPI_STATE_ALERTING) ||
		    (i->state == CAPI_STATE_DID) || (i->state == CAPI_STATE_INCALL)) {
			CONNECT_RESP_HEADER(&CMSG, capi_ApplID, i->MessageNumber, 0);
			CONNECT_RESP_PLCI(&CMSG) = i->PLCI;
			CONNECT_RESP_REJECT(&CMSG) = 4;
			_capi_put_cmsg(&CMSG);
			ret = 0;
		}
		if ((i->isdnstate & CAPI_ISDN_STATE_HOLD))
			pbx_capi_retrieve(c, 0, NULL);
		break;
	case CW_CONTROL_PROGRESS:
		cc_verbose(3, 1, VERBOSE_PREFIX_2 "%s: Requested PROGRESS-Indication for %s\n",
			i->vname, c->name);
		if (i->ntmode) pbx_capi_signal_progress(c, 0, NULL);
		break;
	case CW_CONTROL_PROCEEDING:
		cc_verbose(3, 1, VERBOSE_PREFIX_2 "%s: Requested PROCEEDING-Indication for %s\n",
			i->vname, c->name);
		if (i->ntmode) pbx_capi_signal_progress(c, 0, NULL);
		break;
	case CW_CONTROL_HOLD:
		cc_verbose(3, 1, VERBOSE_PREFIX_2 "%s: Requested HOLD-Indication for %s\n",
			i->vname, c->name);
		if (i->doholdtype != CC_HOLDTYPE_LOCAL) {
			ret = pbx_capi_hold(c, 0, NULL);
		}
		break;
	case CW_CONTROL_UNHOLD:
		cc_verbose(3, 1, VERBOSE_PREFIX_2 "%s: Requested UNHOLD-Indication for %s\n",
			i->vname, c->name);
		if (i->doholdtype != CC_HOLDTYPE_LOCAL) {
			ret = pbx_capi_retrieve(c, 0, NULL);
		}
		break;
	case -1: /* stop indications */
		cc_verbose(3, 1, VERBOSE_PREFIX_2 "%s: Requested Indication-STOP for %s\n",
			i->vname, c->name);
		if ((i->isdnstate & CAPI_ISDN_STATE_HOLD))
			pbx_capi_retrieve(c, 0, NULL);
		break;
	default:
		cc_verbose(3, 1, VERBOSE_PREFIX_2 "%s: Requested unknown Indication %d for %s\n",
			i->vname, condition, c->name);
		break;
	}
	cc_mutex_unlock(&i->lock);
	return(ret);
}

/*
 * PBX wants to know the state for a specific device
 */
static int pbx_capi_devicestate(void *data)
{
	int res = CW_DEVICE_UNKNOWN;

	if (!data) {
		cc_verbose(3, 1, VERBOSE_PREFIX_2 "No data for capi_devicestate\n");
		return res;
	}

	cc_verbose(3, 1, VERBOSE_PREFIX_4 "CAPI devicestate requested for %s\n",
		(char *)data);

	return res;
}

static void capi_do_channel_task(void)
{
	if (chan_for_task == NULL)
		return;

	switch(channel_task) {
	case CAPI_CHANNEL_TASK_HANGUP:
		/* deferred (out of lock) hangup */
		cw_hangup(chan_for_task);
		break;
	case CAPI_CHANNEL_TASK_SOFTHANGUP:
		/* deferred (out of lock) soft-hangup */
		cw_softhangup(chan_for_task, CW_SOFTHANGUP_DEV);
		break;
	case CAPI_CHANNEL_TASK_PICKUP:
		if (cw_pickup_call(chan_for_task)) {
			cc_verbose(3, 1, VERBOSE_PREFIX_2 "%s: Pickup not possible.\n",
				chan_for_task->name);
		}
		cw_hangup(chan_for_task);
		break;
	default:
		/* nothing to do */
		break;
	}
	chan_for_task = NULL;
	channel_task = CAPI_CHANNEL_TASK_NONE;
}

/*
 * module stuff, monitor...
 */
static void *capidev_loop(void *data)
{
	unsigned int Info;
	_cmsg monCMSG;

	/* FIXME: capidev_check_wait_get_cmsg not only waits but _loops_
	 * waiting. Until this is rewritten to be cancellation safe
	 * reloads are going to leak memory, descriptors, whatever
	 */
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	for (/* for ever */;;) {
		switch(Info = capidev_check_wait_get_cmsg(&monCMSG)) {
		case 0x0000:
			capidev_handle_msg(&monCMSG);
			capi_do_channel_task();
			break;
		case 0x1104:
			/* CAPI queue is empty */
			break;
		case 0x1101:
			/* The application ID is no longer valid.
			 * This error is fatal, and "chan_capi" 
			 * should restart.
			 */
			cc_log(CW_LOG_ERROR, "CAPI reports application ID no longer valid, PANIC\n");
			return NULL;
		default:
			/* something is wrong! */
			break;
		} /* switch */
	} /* for */
	
	/* never reached */
	return NULL;
}

/*
 * GAIN
 */
static void capi_gains(struct cc_capi_gains *g, float rxgain, float txgain)
{
	int i = 0;
	int x = 0;
	
	if (rxgain != 1.0) {
		for (i = 0; i < 256; i++) {
			if (capi_capability == CW_FORMAT_ULAW) {
				x = (int)(((float)capiULAW2INT[i]) * rxgain);
			} else {
				x = (int)(((float)capiALAW2INT[i]) * rxgain);
			}
			if (x > 32767)
				x = 32767;
			if (x < -32767)
				x = -32767;
			if (capi_capability == CW_FORMAT_ULAW) {
				g->rxgains[i] = capi_int2ulaw(x);
			} else {
				g->rxgains[i] = capi_int2alaw(x);
			}
		}
	}
	
	if (txgain != 1.0) {
		for (i = 0; i < 256; i++) {
			if (capi_capability == CW_FORMAT_ULAW) {
				x = (int)(((float)capiULAW2INT[i]) * txgain);
			} else {
				x = (int)(((float)capiALAW2INT[i]) * txgain);
			}
			if (x > 32767)
				x = 32767;
			if (x < -32767)
				x = -32767;
			if (capi_capability == CW_FORMAT_ULAW) {
				g->txgains[i] = capi_int2ulaw(x);
			} else {
				g->txgains[i] = capi_int2alaw(x);
			}
		}
	}
}

/*
 * create new interface
 */
static int mkif(struct cc_capi_conf *conf)
{
	struct capi_pvt *tmp;
	int i = 0;
	u_int16_t unit;

	for (i = 0; i <= conf->devices; i++) {
		tmp = malloc(sizeof(struct capi_pvt));
		if (!tmp) {
			return -1;
		}
		memset(tmp, 0, sizeof(struct capi_pvt));
	
		tmp->readerfd = -1;
		tmp->writerfd = -1;
		
		cc_mutex_init(&tmp->lock);
		cw_cond_init(&tmp->event_trigger, NULL);
	
		if (i == 0) {
			snprintf(tmp->name, sizeof(tmp->name) - 1, "%s-pseudo-D", conf->name);
			tmp->channeltype = CAPI_CHANNELTYPE_D;
		} else {
			cc_copy_string(tmp->name, conf->name, sizeof(tmp->name));
			tmp->channeltype = CAPI_CHANNELTYPE_B;
		}
		snprintf(tmp->vname, sizeof(tmp->vname) - 1, "%s#%02d", conf->name, i);
		cc_copy_string(tmp->context, conf->context, sizeof(tmp->context));
		cc_copy_string(tmp->incomingmsn, conf->incomingmsn, sizeof(tmp->incomingmsn));
		cc_copy_string(tmp->defaultcid, conf->defaultcid, sizeof(tmp->defaultcid));
		cc_copy_string(tmp->prefix, conf->prefix, sizeof(tmp->prefix));
		cc_copy_string(tmp->accountcode, conf->accountcode, sizeof(tmp->accountcode));
		cc_copy_string(tmp->language, conf->language, sizeof(tmp->language));

		unit = atoi(conf->controllerstr);
			/* There is no reason not to
			 * allow controller 0 !
			 *
			 * Hide problem from user:
			 */
			if (unit == 0) {
				/* The ISDN4BSD kernel will modulo
				 * the controller number by 
				 * "capi_num_controllers", so this
				 * is equivalent to "0":
				 */
				unit = capi_num_controllers;
			}

		/* always range check user input */
		if (unit > CAPI_MAX_CONTROLLERS)
			unit = CAPI_MAX_CONTROLLERS;

		tmp->controller = unit;
		capi_used_controllers |= (1 << unit);
		tmp->doEC = conf->echocancel;
		tmp->ecOption = conf->ecoption;
		if (conf->ecnlp) tmp->ecOption |= 0x01; /* bit 0 of ec-option is NLP */
		tmp->ecTail = conf->ectail;
		tmp->isdnmode = conf->isdnmode;
		tmp->ntmode = conf->ntmode;
		tmp->ES = conf->es;
		tmp->callgroup = conf->callgroup;
		tmp->pickupgroup = conf->pickupgroup;
		tmp->group = conf->group;
		tmp->amaflags = conf->amaflags;
		tmp->immediate = conf->immediate;
		tmp->holdtype = conf->holdtype;
		tmp->ecSelector = conf->ecSelector;
		tmp->bridge = conf->bridge;
		tmp->FaxState = conf->faxsetting;
		
		tmp->smoother = cw_smoother_new(CAPI_MAX_B3_BLOCK_SIZE);

		tmp->rxgain = conf->rxgain;
		tmp->txgain = conf->txgain;
		capi_gains(&tmp->g, conf->rxgain, conf->txgain);

		tmp->doDTMF = conf->softdtmf;
		tmp->capability = conf->capability;

		tmp->next = iflist; /* prepend */
		iflist = tmp;
		cc_verbose(2, 0, VERBOSE_PREFIX_3 "capi %c %s (%s:%s) contr=%d devs=%d EC=%d,opt=%d,tail=%d\n",
			(tmp->channeltype == CAPI_CHANNELTYPE_B)? 'B' : 'D',
			tmp->vname, tmp->incomingmsn, tmp->context, tmp->controller,
			conf->devices, tmp->doEC, tmp->ecOption, tmp->ecTail);
	}
	return 0;
}

/*
 * eval supported services
 */
static void supported_sservices(struct cc_capi_controller *cp)
{
	MESSAGE_EXCHANGE_ERROR error;
	_cmsg CMSG, CMSG2;
	struct timeval tv;
	unsigned char fac[20];
	unsigned int services;

	memset(fac, 0, sizeof(fac));
	FACILITY_REQ_HEADER(&CMSG, capi_ApplID, get_capi_MessageNumber(), 0);
	FACILITY_REQ_CONTROLLER(&CMSG) = cp->controller;
	FACILITY_REQ_FACILITYSELECTOR(&CMSG) = FACILITYSELECTOR_SUPPLEMENTARY;
	fac[0] = 3;
	FACILITY_REQ_FACILITYREQUESTPARAMETER(&CMSG) = (_cstruct)&fac;
	_capi_put_cmsg(&CMSG);

	tv.tv_sec = 1;
	tv.tv_usec = 0;
	
	for (/* for ever */;;) {
		error = capi20_waitformessage(capi_ApplID, &tv);
		error = capi_get_cmsg(&CMSG2, capi_ApplID); 
		if (error == 0) {
			if (IS_FACILITY_CONF(&CMSG2)) {
				cc_verbose(5, 0, VERBOSE_PREFIX_4 "FACILITY_CONF INFO = %#x\n",
					FACILITY_CONF_INFO(&CMSG2));
				break;
			}
		}
	} 

	/* parse supported sservices */
	if (FACILITY_CONF_FACILITYSELECTOR(&CMSG2) != FACILITYSELECTOR_SUPPLEMENTARY) {
		cc_log(CW_LOG_NOTICE, "unexpected FACILITY_SELECTOR = %#x\n",
			FACILITY_CONF_FACILITYSELECTOR(&CMSG2));
		return;
	}

	if (FACILITY_CONF_FACILITYCONFIRMATIONPARAMETER(&CMSG2)[4] != 0) {
		cc_log(CW_LOG_NOTICE, "supplementary services info  = %#x\n",
			(short)FACILITY_CONF_FACILITYCONFIRMATIONPARAMETER(&CMSG2)[1]);
		return;
	}
	services = read_capi_dword(&(FACILITY_CONF_FACILITYCONFIRMATIONPARAMETER(&CMSG2)[6]));
	cc_verbose(3, 0, VERBOSE_PREFIX_4 "supplementary services : 0x%08x\n",
		services);
	
	/* success, so set the features we have */
	cc_verbose(3, 0, VERBOSE_PREFIX_4 " ");
	if (services & 0x0001) {
		cp->holdretrieve = 1;
		cc_verbose(3, 0, "HOLD/RETRIEVE ");
	}
	if (services & 0x0002) {
		cp->terminalportability = 1;
		cc_verbose(3, 0, "TERMINAL-PORTABILITY ");
	}
	if (services & 0x0004) {
		cp->ECT = 1;
		cc_verbose(3, 0, "ECT ");
	}
	if (services & 0x0008) {
		cp->threePTY = 1;
		cc_verbose(3, 0, "3PTY ");
	}
	if (services & 0x0010) {
		cp->CF = 1;
		cc_verbose(3, 0, "CF ");
	}
	if (services & 0x0020) {
		cp->CD = 1;
		cc_verbose(3, 0, "CD ");
	}
	if (services & 0x0040) {
		cp->MCID = 1;
		cc_verbose(3, 0, "MCID ");
	}
	if (services & 0x0080) {
		cp->CCBS = 1;
		cc_verbose(3, 0, "CCBS ");
	}
	if (services & 0x0100) {
		cp->MWI = 1;
		cc_verbose(3, 0, "MWI ");
	}
	if (services & 0x0200) {
		cp->CCNR = 1;
		cc_verbose(3, 0, "CCNR ");
	}
	if (services & 0x0400) {
		cp->CONF = 1;
		cc_verbose(3, 0, "CONF");
	}
	cc_verbose(3, 0, "\n");
	return;
}

/*
 * helper functions to convert conf value to string
 */
static char *show_bproto(int bproto)
{
	switch(bproto) {
	case CC_BPROTO_TRANSPARENT:
		return "trans";
	case CC_BPROTO_FAXG3:
		return " fax ";
	case CC_BPROTO_RTP:
		return " rtp ";
	}
	return " ??? ";
}
static char *show_state(int state)
{
	switch(state) {
	case CAPI_STATE_ALERTING:
		return "Ring ";
	case CAPI_STATE_CONNECTED:
		return "Conn ";
	case CAPI_STATE_DISCONNECTING:
		return "discP";
	case CAPI_STATE_DISCONNECTED:
		return "Disc ";
	case CAPI_STATE_CONNECTPENDING:
		return "Dial ";
	case CAPI_STATE_ANSWERING:
		return "Answ ";
	case CAPI_STATE_DID:
		return "DIDin";
	case CAPI_STATE_INCALL:
		return "icall";
	case CAPI_STATE_ONHOLD:
		return "Hold ";
	}
	return "-----";
}
static char *show_isdnstate(unsigned int isdnstate, char *str)
{
	str[0] = '\0';

	if (isdnstate & CAPI_ISDN_STATE_PBX)
		strcat(str, "*");
	if (isdnstate & CAPI_ISDN_STATE_LI)
		strcat(str, "G");
	if (isdnstate & CAPI_ISDN_STATE_B3_UP)
		strcat(str, "B");
	if (isdnstate & CAPI_ISDN_STATE_B3_PEND)
		strcat(str, "b");
	if (isdnstate & CAPI_ISDN_STATE_PROGRESS)
		strcat(str, "P");
	if (isdnstate & CAPI_ISDN_STATE_HOLD)
		strcat(str, "H");
	if (isdnstate & CAPI_ISDN_STATE_ECT)
		strcat(str, "T");
	if (isdnstate & (CAPI_ISDN_STATE_SETUP | CAPI_ISDN_STATE_SETUP_ACK))
		strcat(str, "S");

	return str;
}

/*
 * do command capi show channels
 */
static int pbxcli_capi_show_channels(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct capi_pvt *i;
	char iochar;
	char i_state[80];
	char b3q[16];
	
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	
	cw_dynstr_printf(ds_p,
		"CAPI B-channel information:\n"
		"Line-Name       NTmode state i/o bproto isdnstate   ton  number\n"
		"----------------------------------------------------------------\n");

	cc_mutex_lock(&iflock);

	for (i = iflist; i; i = i->next) {
		if (i->channeltype != CAPI_CHANNELTYPE_B)
			continue;

		if ((i->state == 0) || (i->state == CAPI_STATE_DISCONNECTED))
			iochar = '-';
		else if (i->outgoing)
			iochar = 'O';
		else
			iochar = 'I';

		if (capidebug)
			snprintf(b3q, sizeof(b3q), "  B3q=%d", i->B3q);
		else
			b3q[0] = '\0';

		cw_dynstr_printf(ds_p,
			"%-16s %s   %s  %c  %s  %-10s  0x%02x '%s'->'%s'%s\n",
			i->vname,
			i->ntmode ? "yes":"no ",
			show_state(i->state),
			iochar,
			show_bproto(i->bproto),
			show_isdnstate(i->isdnstate, i_state),
			i->cid_ton,
			i->cid,
			i->dnid,
			b3q
		);
	}

	cc_mutex_unlock(&iflock);
		
	return RESULT_SUCCESS;
}

/*
 * do command capi info
 */
static int pbxcli_capi_info(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int i = 0;
	
	if (argc != 2)
		return RESULT_SHOWUSAGE;
		
	for (i = 1; i <= capi_num_controllers; i++) {
		if (capi_controllers[i] != NULL) {
			cw_dynstr_printf(ds_p, "Contr%d: %d B channels total, %d B channels free.\n",
				i, capi_controllers[i]->nbchannels,
				capi_controllers[i]->nfreebchannels);
		}
	}
	return RESULT_SUCCESS;
}

/*
 * enable debugging
 */
static int pbxcli_capi_do_debug(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
		
	capidebug = 1;
	cw_dynstr_printf(ds_p, "CAPI Debugging Enabled\n");
	
	return RESULT_SUCCESS;
}

/*
 * disable debugging
 */
static int pbxcli_capi_no_debug(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	if (argc != 3)
		return RESULT_SHOWUSAGE;

	capidebug = 0;
	cw_dynstr_printf(ds_p, "CAPI Debugging Disabled\n");
	
	return RESULT_SUCCESS;
}

/*
 * usages
 */
static const char info_usage[] =
"Usage: capi info\n"
"       Show info about B channels on controllers.\n";

static const char show_channels_usage[] =
"Usage: capi show channels\n"
"       Show info about B channels.\n";

static const char debug_usage[] =
"Usage: capi debug\n"
"       Enables dumping of CAPI packets for debugging purposes\n";

static const char no_debug_usage[] =
"Usage: capi no debug\n"
"       Disables dumping of CAPI packets for debugging purposes\n";

/*
 * define commands
 */
static struct cw_clicmd  cli_info = {
	.cmda = { "capi", "info", NULL },
	.handler = pbxcli_capi_info,
	.summary = "Show CAPI info",
	.usage = info_usage,
};
static struct cw_clicmd  cli_show_channels = {
	.cmda = { "capi", "show", "channels", NULL },
	.handler = pbxcli_capi_show_channels,
	.summary = "Show B-channel info",
	.usage = show_channels_usage,
};
static struct cw_clicmd  cli_debug = {
	.cmda = { "capi", "debug", NULL },
	.handler = pbxcli_capi_do_debug,
	.summary = "Enable CAPI debugging",
	.usage = debug_usage,
};
static struct cw_clicmd  cli_no_debug = {
	.cmda = { "capi", "no", "debug", NULL },
	.handler = pbxcli_capi_no_debug,
	.summary = "Disable CAPI debugging",
	.usage = no_debug_usage,
};


static const struct cw_channel_tech capi_tech = {
	.type = channeltype,
	.description = tdesc,
	.capabilities = CW_FORMAT_ALAW,
	.requester = pbx_capi_request,
	.send_digit = pbx_capi_send_digit,
	.call = pbx_capi_call,
	.hangup = pbx_capi_hangup,
	.answer = pbx_capi_answer,
	.read = pbx_capi_read,
	.write = pbx_capi_write,
	.bridge = pbx_capi_bridge,
	.exception = NULL,
	.indicate = pbx_capi_indicate,
	.fixup = pbx_capi_fixup,
	.devicestate = pbx_capi_devicestate,
};

/*
 * register at CAPI interface
 */
static int cc_register_capi(unsigned blocksize)
{
	u_int16_t error = 0;

	if (capi_ApplID != CAPI_APPLID_UNUSED) {
		if (capi20_release(capi_ApplID) != 0)
			cc_log(CW_LOG_WARNING,"Unable to unregister from CAPI!\n");
	}
	cc_verbose(3, 0, VERBOSE_PREFIX_3 "Registering at CAPI "
		   "(blocksize=%d)\n", blocksize);

#if (CAPI_OS_HINT == 2)
	error = capi20_register(CAPI_BCHANS, CAPI_MAX_B3_BLOCKS, 
				blocksize, &capi_ApplID, CAPI_STACK_VERSION);
#else
	error = capi20_register(CAPI_BCHANS, CAPI_MAX_B3_BLOCKS, 
				blocksize, &capi_ApplID);
#endif
	if (error != 0) {
		capi_ApplID = CAPI_APPLID_UNUSED;
		cc_log(CW_LOG_NOTICE,"unable to register application at CAPI!\n");
		return -1;
	}
	return 0;
}

/*
 * init capi stuff
 */
static int cc_init_capi(void)
{
#if (CAPI_OS_HINT == 1)
	CAPIProfileBuffer_t profile;
#else
	struct cc_capi_profile profile;
#endif
	struct cc_capi_controller *cp;
	int controller;
	unsigned int privateoptions;

	if (capi20_isinstalled() != 0) {
		cc_log(CW_LOG_WARNING, "CAPI not installed, CAPI disabled!\n");
		return -1;
	}

	if (cc_register_capi(CAPI_MAX_B3_BLOCK_SIZE))
		return -1;

#if (CAPI_OS_HINT == 1)
	if (capi20_get_profile(0, &profile) != 0) {
#elif (CAPI_OS_HINT == 2)
	if (capi20_get_profile(0, &profile, sizeof(profile)) != 0) {
#else
	if (capi20_get_profile(0, (unsigned char *)&profile) != 0) {
#endif
		cc_log(CW_LOG_NOTICE,"unable to get CAPI profile!\n");
		return -1;
	} 

#if (CAPI_OS_HINT == 1)
	capi_num_controllers = profile.wCtlr;
#else
	capi_num_controllers = profile.ncontrollers;
#endif

	cc_verbose(3, 0, VERBOSE_PREFIX_2 "This box has %d capi controller(s).\n",
		capi_num_controllers);
	
	for (controller = 1 ;controller <= capi_num_controllers; controller++) {

		memset(&profile, 0, sizeof(profile));
#if (CAPI_OS_HINT == 1)
		capi20_get_profile(controller, &profile);
#elif (CAPI_OS_HINT == 2)
		capi20_get_profile(controller, &profile, sizeof(profile));
#else
		capi20_get_profile(controller, (unsigned char *)&profile);
#endif
		cp = malloc(sizeof(struct cc_capi_controller));
		if (!cp) {
			cc_log(CW_LOG_ERROR, "Error allocating memory for struct cc_capi_controller\n");
			return -1;
		}
		memset(cp, 0, sizeof(struct cc_capi_controller));
		cp->controller = controller;
#if (CAPI_OS_HINT == 1)
		cp->nbchannels = profile.wNumBChannels;
		cp->nfreebchannels = profile.wNumBChannels;
		if (profile.dwGlobalOptions & CAPI_PROFILE_DTMF_SUPPORT) {
#else
		cp->nbchannels = profile.nbchannels;
		cp->nfreebchannels = profile.nbchannels;
		if (profile.globaloptions & 0x08) {
#endif
			cc_verbose(3, 0, VERBOSE_PREFIX_3 "CAPI/contr%d supports DTMF\n",
				controller);
			cp->dtmf = 1;
		}

		
#if (CAPI_OS_HINT == 1)
		if (profile.dwGlobalOptions & 0x01) {
#else
		if (profile.globaloptions2 & 0x01) {
#endif
			cc_verbose(3, 0, VERBOSE_PREFIX_3 "CAPI/contr%d supports broadband (or old echo-cancel)\n",
				controller);
			cp->broadband = 1;
		}

#if (CAPI_OS_HINT == 1)
		if (profile.dwGlobalOptions & CAPI_PROFILE_ECHO_CANCELLATION) {
#else
		if (profile.globaloptions2 & 0x02) {
#endif
			cc_verbose(3, 0, VERBOSE_PREFIX_3 "CAPI/contr%d supports echo cancellation\n",
				controller);
			cp->echocancel = 1;
		}

#if (CAPI_OS_HINT == 1)
		if (profile.dwGlobalOptions & CAPI_PROFILE_SUPPLEMENTARY_SERVICES)  {
#else
		if (profile.globaloptions & 0x10) {
#endif
			cp->sservices = 1;
		}

#if (CAPI_OS_HINT == 1)
		if (profile.dwGlobalOptions & 0x80)  {
#else
		if (profile.globaloptions & 0x80) {
#endif
			cc_verbose(3, 0, VERBOSE_PREFIX_3 "CAPI/contr%d supports line interconnect\n",
				controller);
			cp->lineinterconnect = 1;
		}
		
		if (cp->sservices == 1) {
			cc_verbose(3, 0, VERBOSE_PREFIX_3 "CAPI/contr%d supports supplementary services\n",
				controller);
			supported_sservices(cp);
		}

		/* New profile options for e.g. RTP with Eicon DIVA */
		privateoptions = read_capi_dword(&profile.manufacturer[0]);
		cc_verbose(3, 0, VERBOSE_PREFIX_3 "CAPI/contr%d private options=0x%08x\n",
			controller, privateoptions);
		if (privateoptions & 0x02) {
			cc_verbose(3, 0, VERBOSE_PREFIX_4 "VoIP/RTP is supported\n");
			voice_over_ip_profile(cp);
		}
		if (privateoptions & 0x04) {
			cc_verbose(3, 0, VERBOSE_PREFIX_4 "T.38 is supported (not implemented yet)\n");
		}

		capi_controllers[controller] = cp;
	}
	return 0;
}

/*
 * final capi init
 */
static int cc_post_init_capi(void)
{
	struct capi_pvt *i;
	int controller;
	unsigned error;
	int use_rtp = 0;

	for (i = iflist; i && !use_rtp; i = i->next) {
		/* if at least one line wants RTP, we need to re-register with
		   bigger block size for RTP-header */
		if (capi_controllers[i->controller]->rtpcodec & i->capability) {
			cc_verbose(3, 0, VERBOSE_PREFIX_4 "at least one CAPI controller wants RTP.\n");
			use_rtp = 1;
		}
	}
	if (use_rtp) {
		if (cc_register_capi(CAPI_MAX_B3_BLOCK_SIZE + RTP_HEADER_SIZE))
			return -1;
	}

	for (controller = 1; controller <= capi_num_controllers; controller++) {
		if (capi_used_controllers & (1 << controller)) {
			if ((error = ListenOnController(ALL_SERVICES, controller)) != 0) {
				cc_log(CW_LOG_ERROR,"Unable to listen on contr%d (error=0x%x)\n",
					controller, error);
			} else {
				cc_verbose(2, 0, VERBOSE_PREFIX_3 "listening on contr%d CIPmask = %#x\n",
					controller, ALL_SERVICES);
			}
		} else {
			cc_log(CW_LOG_NOTICE, "Unused contr%d\n",controller);
		}
	}

	return 0;
}

/*
 * build the interface according to configs
 */
static int conf_interface(struct cc_capi_conf *conf, struct cw_variable *v)
{
	int y;

#define CONF_STRING(var, token)            \
	if (!strcasecmp(v->name, token)) { \
		cc_copy_string(var, v->value, sizeof(var)); \
		continue;                  \
	} else
#define CONF_INTEGER(var, token)           \
	if (!strcasecmp(v->name, token)) { \
		var = atoi(v->value);      \
		continue;                  \
	} else
#define CONF_TRUE(var, token, val)         \
	if (!strcasecmp(v->name, token)) { \
		if (cw_true(v->value))    \
			var = val;         \
		continue;                  \
	} else

	for (; v; v = v->next) {
		CONF_INTEGER(conf->devices, "devices")
		CONF_STRING(conf->context, "context")
		CONF_STRING(conf->incomingmsn, "incomingmsn")
		CONF_STRING(conf->defaultcid, "defaultcid")
		CONF_STRING(conf->controllerstr, "controller")
		CONF_STRING(conf->prefix, "prefix")
		CONF_STRING(conf->accountcode, "accountcode")
		CONF_STRING(conf->language, "language")

		if (!strcasecmp(v->name, "softdtmf")) {
			if ((!conf->softdtmf) && (cw_true(v->value))) {
				conf->softdtmf = 1;
			}
			continue;
		} else
		CONF_TRUE(conf->softdtmf, "relaxdtmf", 2)
		if (!strcasecmp(v->name, "holdtype")) {
			if (!strcasecmp(v->value, "hold")) {
				conf->holdtype = CC_HOLDTYPE_HOLD;
			} else if (!strcasecmp(v->value, "notify")) {
				conf->holdtype = CC_HOLDTYPE_NOTIFY;
			} else {
				conf->holdtype = CC_HOLDTYPE_LOCAL;
			}
			continue;
		} else
		CONF_TRUE(conf->immediate, "immediate", 1)
		CONF_TRUE(conf->es, "echosquelch", 1)
		CONF_TRUE(conf->bridge, "bridge", 1)
		CONF_TRUE(conf->ntmode, "ntmode", 1)
		if (!strcasecmp(v->name, "callgroup")) {
			conf->callgroup = cw_get_group(v->value);
			continue;
		} else
		if (!strcasecmp(v->name, "pickupgroup")) {
			conf->pickupgroup = cw_get_group(v->value);
			continue;
		} else
		if (!strcasecmp(v->name, "group")) {
			conf->group = cw_get_group(v->value);
			continue;
		} else
		if (!strcasecmp(v->name, "amaflags")) {
			y = cw_cdr_amaflags2int(v->value);
			if (y < 0) {
				cw_log(CW_LOG_WARNING, "Invalid AMA flags: %s at line %d\n",
					v->value, v->lineno);
			} else {
				conf->amaflags = y;
			}
		} else
		if (!strcasecmp(v->name, "rxgain")) {
			if (sscanf(v->value, "%f", &conf->rxgain) != 1) {
				cc_log(CW_LOG_ERROR,"invalid rxgain\n");
			}
			continue;
		} else
		if (!strcasecmp(v->name, "txgain")) {
			if (sscanf(v->value, "%f", &conf->txgain) != 1) {
				cc_log(CW_LOG_ERROR, "invalid txgain\n");
			}
			continue;
		} else
		if (!strcasecmp(v->name, "echocancelold")) {
			if (cw_true(v->value)) {
				conf->ecSelector = 6;
			}
			continue;
		} else
		if (!strcasecmp(v->name, "faxdetect")) {
			if (!strcasecmp(v->value, "incoming")) {
				conf->faxsetting |= CAPI_FAX_DETECT_INCOMING;
				conf->faxsetting &= ~CAPI_FAX_DETECT_OUTGOING;
			} else if (!strcasecmp(v->value, "outgoing")) {
				conf->faxsetting |= CAPI_FAX_DETECT_OUTGOING;
				conf->faxsetting &= ~CAPI_FAX_DETECT_INCOMING;
			} else if (!strcasecmp(v->value, "both") || cw_true(v->value))
				conf->faxsetting |= (CAPI_FAX_DETECT_OUTGOING | CAPI_FAX_DETECT_INCOMING);
			else
				conf->faxsetting &= ~(CAPI_FAX_DETECT_OUTGOING | CAPI_FAX_DETECT_INCOMING);
		} else
		if (!strcasecmp(v->name, "echocancel")) {
			if (cw_true(v->value)) {
				conf->echocancel = 1;
				conf->ecoption = EC_OPTION_DISABLE_G165;
			}	
			else if (cw_false(v->value)) {
				conf->echocancel = 0;
				conf->ecoption = 0;
			}	
			else if (!strcasecmp(v->value, "g165") || !strcasecmp(v->value, "g.165")) {
				conf->echocancel = 1;
				conf->ecoption = EC_OPTION_DISABLE_G165;
			}	
			else if (!strcasecmp(v->value, "g164") || !strcasecmp(v->value, "g.164")) {
				conf->echocancel = 1;
				conf->ecoption = EC_OPTION_DISABLE_G164_OR_G165;
			}	
			else if (!strcasecmp(v->value, "force")) {
				conf->echocancel = 1;
				conf->ecoption = EC_OPTION_DISABLE_NEVER;
			}
			else {
				cc_log(CW_LOG_ERROR,"Unknown echocancel parameter \"%s\" -- ignoring\n",v->value);
			}
			continue;
		} else
		CONF_TRUE(conf->ecnlp, "echocancelnlp", 1)
		if (!strcasecmp(v->name, "echotail")) {
			conf->ectail = atoi(v->value);
			if (conf->ectail > 255) {
				conf->ectail = 255;
			} 
			continue;
		} else
		if (!strcasecmp(v->name, "isdnmode")) {
			if (!strcasecmp(v->value, "did"))
			    conf->isdnmode = CAPI_ISDNMODE_DID;
			else if (!strcasecmp(v->value, "msn"))
			    conf->isdnmode = CAPI_ISDNMODE_MSN;
			else
			    cc_log(CW_LOG_ERROR,"Unknown isdnmode parameter \"%s\" -- ignoring\n",
			    	v->value);
		} else
		if (!strcasecmp(v->name, "allow")) {
			cw_parse_allow_disallow(&conf->prefs, &conf->capability, v->value, 1);
		} else
		if (!strcasecmp(v->name, "disallow")) {
			cw_parse_allow_disallow(&conf->prefs, &conf->capability, v->value, 0);
		}
	}
#undef CONF_STRING
#undef CONF_INTEGER
#undef CONF_TRUE
	return 0;
}

/*
 * load the config
 */
static int capi_eval_config(struct cw_config *cfg)
{
	struct cc_capi_conf conf;
	struct cw_variable *v;
	char *cat = NULL;
	float rxgain = 1.0;
	float txgain = 1.0;

	/* prefix defaults */
	cc_copy_string(capi_national_prefix, CAPI_NATIONAL_PREF, sizeof(capi_national_prefix));
	cc_copy_string(capi_international_prefix, CAPI_INTERNAT_PREF, sizeof(capi_international_prefix));

	/* read the general section */
	for (v = cw_variable_browse(cfg, "general"); v; v = v->next) {
		if (!strcasecmp(v->name, "nationalprefix")) {
			cc_copy_string(capi_national_prefix, v->value, sizeof(capi_national_prefix));
		} else if (!strcasecmp(v->name, "internationalprefix")) {
			cc_copy_string(capi_international_prefix, v->value, sizeof(capi_international_prefix));
		} else if (!strcasecmp(v->name, "language")) {
			cc_copy_string(default_language, v->value, sizeof(default_language));
		} else if (!strcasecmp(v->name, "rxgain")) {
			if (sscanf(v->value,"%f",&rxgain) != 1) {
				cc_log(CW_LOG_ERROR,"invalid rxgain\n");
			}
		} else if (!strcasecmp(v->name, "txgain")) {
			if (sscanf(v->value,"%f",&txgain) != 1) {
				cc_log(CW_LOG_ERROR,"invalid txgain\n");
			}
		} else if (!strcasecmp(v->name, "ulaw")) {
			if (cw_true(v->value)) {
				capi_capability = CW_FORMAT_ULAW;
			}
		}
	}

	/* go through all other sections, which are our interfaces */
	for (cat = cw_category_browse(cfg, NULL); cat; cat = cw_category_browse(cfg, cat)) {
		if (!strcasecmp(cat, "general"))
			continue;
			
		if (!strcasecmp(cat, "interfaces")) {
			cc_log(CW_LOG_WARNING, "Config file syntax has changed! Don't use 'interfaces'\n");
			return -1;
		}
		cc_verbose(4, 0, VERBOSE_PREFIX_2 "Reading config for %s\n",
			cat);
		
		/* init the conf struct */
		memset(&conf, 0, sizeof(conf));
		conf.rxgain = rxgain;
		conf.txgain = txgain;
		conf.ecoption = EC_OPTION_DISABLE_G165;
		conf.ectail = EC_DEFAULT_TAIL;
		conf.ecSelector = FACILITYSELECTOR_ECHO_CANCEL;
		cc_copy_string(conf.name, cat, sizeof(conf.name));
		cc_copy_string(conf.language, default_language, sizeof(conf.language));

		if (conf_interface(&conf, cw_variable_browse(cfg, cat))) {
			cc_log(CW_LOG_ERROR, "Error interface config.\n");
			return -1;
		}

		if (mkif(&conf)) {
			cc_log(CW_LOG_ERROR,"Error creating interface list\n");
			return -1;
		}
	}
	return 0;
}

/*
 * unload the module
 */
static int unload_module(void)
{
	struct capi_pvt *i, *itmp;
	int controller;
	int res = 0;

	res |= cw_unregister_function(command_app);

	cw_cli_unregister(&cli_info);
	cw_cli_unregister(&cli_show_channels);
	cw_cli_unregister(&cli_debug);
	cw_cli_unregister(&cli_no_debug);

	if (!pthread_equal(monitor_thread, CW_PTHREADT_NULL)) {
		pthread_cancel(monitor_thread);
		pthread_kill(monitor_thread, SIGURG);
		pthread_join(monitor_thread, NULL);
	}

	cc_mutex_lock(&iflock);

	if (capi_ApplID != CAPI_APPLID_UNUSED) {
		if (capi20_release(capi_ApplID) != 0)
			cc_log(CW_LOG_WARNING,"Unable to unregister from CAPI!\n");
	}

	for (controller = 1; controller <= capi_num_controllers; controller++) {
		if (capi_used_controllers & (1 << controller))
			free(capi_controllers[controller]);
	}
	
	i = iflist;
	while (i) {
		if (i->owner)
			cc_log(CW_LOG_WARNING, "On unload, interface still has owner.\n");
		cw_smoother_free(i->smoother);
		cc_mutex_destroy(&i->lock);
		cw_cond_destroy(&i->event_trigger);
		itmp = i;
		i = i->next;
		free(itmp);
	}

	cc_mutex_unlock(&iflock);
	
	cw_channel_unregister(&capi_tech);
	
	return res;
}

/*
 * main: load the module
 */
static int load_module(void)
{
	struct cw_config *cfg;
	char *config = "capi.conf";
	int res = 0;

	cfg = cw_config_load(config);

	/* We *must* have a config file otherwise stop immediately, well no */
	if (!cfg) {
		cc_log(CW_LOG_ERROR, "Unable to load config %s, CAPI disabled\n", config);
		return 0;
	}

	if (cc_mutex_lock(&iflock)) {
		cc_log(CW_LOG_ERROR, "Unable to lock interface list???\n");
		return -1;
	}

	if ((res = cc_init_capi()) != 0) {
		cc_mutex_unlock(&iflock);
		return(res);
	}

	res = capi_eval_config(cfg);
	cw_config_destroy(cfg);

	if (res != 0) {
		cc_mutex_unlock(&iflock);
		return(res);
	}

	if ((res = cc_post_init_capi()) != 0) {
		cc_mutex_unlock(&iflock);
		unload_module();
		return(res);
	}
	
	cc_mutex_unlock(&iflock);
	
	if (cw_channel_register(&capi_tech)) {
		cc_log(CW_LOG_ERROR, "Unable to register channel class %s\n", channeltype);
		unload_module();
		return -1;
	}

	cw_cli_register(&cli_info);
	cw_cli_register(&cli_show_channels);
	cw_cli_register(&cli_debug);
	cw_cli_register(&cli_no_debug);
	
	command_app = cw_register_function(commandapp, pbx_capicommand_exec, commandsynopsis, commandsyntax, commandtdesc);

	if (cw_pthread_create(&monitor_thread, &global_attr_default, capidev_loop, NULL) < 0) {
		monitor_thread = CW_PTHREADT_NULL;
		cc_log(CW_LOG_ERROR, "Unable to start monitor thread!\n");
		return -1;
	}

	return 0;
}

MODULE_INFO(load_module, NULL, unload_module, NULL, ccdesc)
