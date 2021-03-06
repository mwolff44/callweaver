/*
 * CallWeaver -- An open source telephony toolkit.
 * 
 * Copyright (C) 2004, Christian Richter
 *
 * Christian Richter <crich@beronet.com>
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
 *
 */

/*!
 * \file
 *
 * \brief the chan_misdn channel driver for CallWeaver
 * \author Christian Richter <crich@beronet.com>
 *
 * \ingroup channel_drivers
 */

#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <sys/file.h>
#include <semaphore.h>

#include "callweaver/channel.h"
#include "callweaver/config.h"
#include "callweaver/logger.h"
#include "callweaver/module.h"
#include "callweaver/pbx.h"
#include "callweaver/options.h"
#include "callweaver/io.h"
#include "callweaver/frame.h"
#include "callweaver/translate.h"
#include "callweaver/cli.h"
#include "callweaver/musiconhold.h"
#include "callweaver/dsp.h"
#include "callweaver/translate.h"
#include "callweaver/config.h"
#include "callweaver/file.h"
#include "callweaver/phone_no_utils.h"
#include "callweaver/indications.h"
#include "callweaver/app.h"
#include "callweaver/features.h"
#include "callweaver/sched.h"
#include "callweaver/keywords.h"


#include <chan_misdn_config.h>
#include <isdn_lib.h>

char global_tracefile[BUFFERSIZE+1];

struct misdn_jb{
	int size;
	int upper_threshold;
	char *samples, *ok;
	int wp,rp;
	int state_empty;
	int state_full;
	int state_buffer;
	int bytes_wrote;
	cw_mutex_t mutexjb;
};



/* allocates the jb-structure and initialise the elements*/
struct misdn_jb *misdn_jb_init(int size, int upper_threshold);

/* frees the data and destroys the given jitterbuffer struct */
void misdn_jb_destroy(struct misdn_jb *jb);

/* fills the jitterbuffer with len data returns < 0 if there was an
error (bufferoverun). */
int misdn_jb_fill(struct misdn_jb *jb, const char *data, int len);

/* gets len bytes out of the jitterbuffer if available, else only the
available data is returned and the return value indicates the number
of data. */
int misdn_jb_empty(struct misdn_jb *jb, char *data, int len);

/* get the level of the buffer */
int misdn_jb_get_level(struct misdn_jb *jb);

enum misdn_chan_state {
	MISDN_NOTHING,		/*!< at beginning */
	MISDN_WAITING4DIGS, /*!<  when waiting for infos */
	MISDN_EXTCANTMATCH, /*!<  when callweaver couldnt match our ext */
	MISDN_DIALING, /*!<  when pbx_start */
	MISDN_PROGRESS, /*!<  we got a progress */
	MISDN_PROCEEDING, /*!<  we got a progress */
	MISDN_CALLING, /*!<  when misdn_call is called */
	MISDN_CALLING_ACKNOWLEDGE, /*!<  when we get SETUP_ACK */
	MISDN_ALERTING, /*!<  when Alerting */
	MISDN_BUSY, /*!<  when BUSY */
	MISDN_CONNECTED, /*!<  when connected */
	MISDN_PRECONNECTED, /*!<  when connected */
	MISDN_DISCONNECTED, /*!<  when connected */
	MISDN_RELEASED, /*!<  when connected */
	MISDN_BRIDGED, /*!<  when bridged */
	MISDN_CLEANING, /*!< when hangup from * but we were connected before */
	MISDN_HUNGUP_FROM_MISDN, /*!< when DISCONNECT/RELEASE/REL_COMP  cam from misdn */
	MISDN_HUNGUP_FROM_CW, /*!< when DISCONNECT/RELEASE/REL_COMP came out of */
	/* misdn_hangup */
	MISDN_HOLDED, /*!< if this chan is holded */
	MISDN_HOLD_DISCONNECT /*!< if this chan is holded */
  
};

#define ORG_CW 1
#define ORG_MISDN 2

struct chan_list {
  
	char allowed_bearers[BUFFERSIZE+1];
	
	enum misdn_chan_state state;
	int need_queue_hangup;
	int need_hangup;
	int need_busy;
	
	int orginator;

	int norxtone;
	int notxtone; 

	int toggle_ec;
	
	int incoming_early_audio;

	int ignore_dtmf;

	int pipe[2];
	char cw_rd_buf[4096];
	struct cw_frame frame;
	char framedata[160];
	int framepos;

	int faxdetect; /* 0:no 1:yes 2:yes+nojump */
	int faxdetect_timeout;
	struct timeval faxdetect_tv;
	int faxhandled;

	int cw_dsp;

	int jb_len;
	int jb_upper_threshold;
	struct misdn_jb *jb;

 	struct misdn_jb *jb_rx;
 		
	struct cw_dsp *dsp;
	struct cw_trans_pvt *trans;
  
	struct cw_channel *cw;

	int dummy;
  
	struct misdn_bchannel *bc;
	struct misdn_bchannel *holded_bc;

	unsigned int l3id;
	int addr;

	char context[BUFFERSIZE];

	int zero_read_cnt;
	int dropped_frame_cnt;

	int far_alerting;
	int other_pid;
	struct chan_list *other_ch;

	const struct tone_zone_sound *ts;
	
	int overlap_dial;
	int overlap_dial_task;
	cw_mutex_t overlap_tv_lock;
	struct timeval overlap_tv;
  
	struct chan_list *peer;
	struct chan_list *next;
	struct chan_list *prev;
	struct chan_list *first;
};



void export_ch(struct cw_channel *chan, struct misdn_bchannel *bc, struct chan_list *ch);
void import_ch(struct cw_channel *chan, struct misdn_bchannel *bc, struct chan_list *ch);

struct robin_list {
	char *group;
	int port;
	int channel;
	struct robin_list *next;
	struct robin_list *prev;
};
static struct robin_list *robin = NULL;



static struct cw_frame *process_cw_dsp(struct chan_list *tmp, struct cw_frame *frame);



static inline void free_robin_list_r (struct robin_list *r)
{
        if (r) {
                if (r->next) free_robin_list_r(r->next);
                free(r->group);
                free(r);
        }
}

static void free_robin_list ( void )
{
	free_robin_list_r(robin);
	robin = NULL;
}

static struct robin_list* get_robin_position (char *group) 
{
	struct robin_list *iter = robin;
	for (; iter; iter = iter->next) {
		if (!strcasecmp(iter->group, group))
			return iter;
	}
	struct robin_list *new = (struct robin_list *)calloc(1, sizeof(struct robin_list));
	new->group = strndup(group, strlen(group));
	new->channel = 1;
	if (robin) {
		new->next = robin;
		robin->prev = new;
	}
	robin = new;
	return robin;
}


/* the main schedule context for stuff like l1 watcher, overlap dial, ... */
static struct sched_context *misdn_tasks = NULL;
static pthread_t misdn_tasks_thread;

static void chan_misdn_log(int level, int port, char *tmpl, ...);

static struct cw_channel *misdn_new(struct chan_list *cl, int state,  char *exten, char *callerid, int format, int port, int c);
static void send_digit_to_chan(struct chan_list *cl, char digit );

static int pbx_start_chan(struct chan_list *ch);

#define CW_CID_P(cw) cw->cid.cid_num
#define CW_LOAD_CFG cw_config_load
#define CW_DESTROY_CFG cw_config_destroy

#define MISDN_CALLWEAVER_TECH_PVT(cw) cw->tech_pvt
#define MISDN_CALLWEAVER_PVT(cw) 1

#include "callweaver/strings.h"

/* #define MISDN_DEBUG 1 */

static const char desc[] = "Channel driver for mISDN Support (Bri/Pri)";
static const char misdn_type[] = "mISDN";

static int tracing = 0 ;

static char **misdn_key_vector=NULL;
static int misdn_key_vector_size=0;

/* Only alaw and mulaw is allowed for now */
static int prefformat =  CW_FORMAT_ALAW ; /*  CW_FORMAT_SLINEAR ;  CW_FORMAT_ULAW | */

static int *misdn_debug;
static int *misdn_debug_only;
static int max_ports;

static int *misdn_in_calls;
static int *misdn_out_calls;


struct chan_list dummy_cl;

struct chan_list *cl_te=NULL;
cw_mutex_t cl_te_lock;

static enum event_response_e
cb_events(enum event_e event, struct misdn_bchannel *bc, void *user_data);

static void send_cause2cw(struct cw_channel *cw, struct misdn_bchannel*bc, struct chan_list *ch);

static void cl_queue_chan(struct chan_list **list, struct chan_list *chan);
static void cl_dequeue_chan(struct chan_list **list, struct chan_list *chan);
static struct chan_list *find_chan_by_bc(struct chan_list *list, struct misdn_bchannel *bc);
static struct chan_list *find_chan_by_pid(struct chan_list *list, int pid);



static int dialtone_indicate(struct chan_list *cl);
static int hanguptone_indicate(struct chan_list *cl);
static int stop_indicate(struct chan_list *cl);

static int start_bc_tones(struct chan_list *cl);
static int stop_bc_tones(struct chan_list *cl);
static void release_chan(struct misdn_bchannel *bc);

static int misdn_set_opt_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result);
static int misdn_facility_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result);

int chan_misdn_jb_empty(struct misdn_bchannel *bc, char *buf, int len);

int add_out_calls(int port);
int add_in_calls(int port);

static int update_ec_config(struct misdn_bchannel *bc);

static int load_module(void);
static int unload_module(void);

void trigger_read(struct chan_list *ch, char *data, int len);

/*************** Helpers *****************/

static struct chan_list * get_chan_by_cw(struct cw_channel *cw)
{
	struct chan_list *tmp;
  
	for (tmp = cl_te;  tmp;  tmp = tmp->next)
    {
		if (tmp->cw == cw)
            return tmp;
	}
  
	return NULL;
}

static struct chan_list *get_chan_by_cw_name(char *name)
{
	struct chan_list *tmp;
  
	for (tmp=cl_te; tmp; tmp = tmp->next)
    {
		if ( tmp->cw  && strcmp(tmp->cw->name,name) == 0)
            return tmp;
	}
  
	return NULL;
}



struct allowed_bearers {
	int cap;
	int val;
	char *name;
};

struct allowed_bearers allowed_bearers_array[]={
	{INFO_CAPABILITY_SPEECH,1,"speech"},
	{INFO_CAPABILITY_AUDIO_3_1K,2,"3_1khz"},
	{INFO_CAPABILITY_DIGITAL_UNRESTRICTED,4,"digital_unrestricted"},
	{INFO_CAPABILITY_DIGITAL_RESTRICTED,8,"digital_restriced"},
	{INFO_CAPABILITY_VIDEO,16,"video"}
};

static char *bearer2str(int cap) {
	static char *bearers[]={
		"Speech",
		"Audio 3.1k",
		"Unres Digital",
		"Res Digital",
		"Video",
		"Unknown Bearer"
	};
	
	switch (cap) {
	case INFO_CAPABILITY_SPEECH:
		return bearers[0];
		break;
	case INFO_CAPABILITY_AUDIO_3_1K:
		return bearers[1];
		break;
	case INFO_CAPABILITY_DIGITAL_UNRESTRICTED:
		return bearers[2];
		break;
	case INFO_CAPABILITY_DIGITAL_RESTRICTED:
		return bearers[3];
		break;
	case INFO_CAPABILITY_VIDEO:
		return bearers[4];
		break;
	default:
		return bearers[5];
		break;
	}
}


static void print_facility( struct misdn_bchannel *bc)
{
	switch (bc->fac_type) {
	case FACILITY_CALLDEFLECT:
		chan_misdn_log(0,bc->port," --> calldeflect: %s\n",
			       bc->fac.calldeflect_nr);
		break;
	case FACILITY_CENTREX:
		chan_misdn_log(0,bc->port," --> centrex: %s\n",
			       bc->fac.cnip);
		break;
	default:
		chan_misdn_log(0,bc->port," --> unknown\n");
		
	}
}

static void print_bearer(struct misdn_bchannel *bc) 
{
	
	chan_misdn_log(2, bc->port, " --> Bearer: %s\n",bearer2str(bc->capability));
	
	switch(bc->law) {
	case INFO_CODEC_ALAW:
		chan_misdn_log(2, bc->port, " --> Codec: Alaw\n");
		break;
	case INFO_CODEC_ULAW:
		chan_misdn_log(2, bc->port, " --> Codec: Ulaw\n");
		break;
	}
}
/*************** Helpers END *************/

static inline int _misdn_tasks_add_variable (int timeout, cw_sched_cb callback, void *data, int variable)
{
	if (!misdn_tasks)
		misdn_tasks = sched_context_create(1);
	return cw_sched_add_variable(misdn_tasks, timeout, callback, data, variable);
}

static int misdn_tasks_add (int timeout, cw_sched_cb callback, void *data)
{
	return _misdn_tasks_add_variable(timeout, callback, data, 0);
}

static int misdn_tasks_add_variable (int timeout, cw_sched_cb callback, void *data)
{
	return _misdn_tasks_add_variable(timeout, callback, data, 1);
}

static void misdn_tasks_remove (int task_id)
{
	cw_sched_del(misdn_tasks, task_id);
}

static int misdn_l1_task (void *data)
{
	misdn_lib_isdn_l1watcher((int)data);
	chan_misdn_log(5, (int)data, "L1watcher timeout\n");
	return 1;
}

static int misdn_overlap_dial_task (void *data)
{
	struct timeval tv_end, tv_now;
	int diff;
	struct chan_list *ch = (struct chan_list *)data;

	chan_misdn_log(4, ch->bc->port, "overlap dial task, chan_state: %d\n", ch->state);

	if (ch->state != MISDN_WAITING4DIGS) {
		ch->overlap_dial_task = -1;
		return 0;
	}
	
	cw_mutex_lock(&ch->overlap_tv_lock);
	tv_end = ch->overlap_tv;
	cw_mutex_unlock(&ch->overlap_tv_lock);
	
	tv_end.tv_sec += ch->overlap_dial;
	tv_now = cw_tvnow();

	diff = cw_tvdiff_ms(tv_end, tv_now);

	if (diff <= 100) {
		/* if we are 100ms near the timeout, we are satisfied.. */
		stop_indicate(ch);
		if (cw_exists_extension(ch->cw, ch->context, ch->bc->dad, 1, ch->bc->oad)) {
			ch->state=MISDN_DIALING;
			if (pbx_start_chan(ch) < 0) {
				chan_misdn_log(-1, ch->bc->port, "cw_pbx_start returned < 0 in misdn_overlap_dial_task\n");
				goto misdn_overlap_dial_task_disconnect;
			}
		} else {
misdn_overlap_dial_task_disconnect:
			hanguptone_indicate(ch);
			if (ch->bc->nt)
				misdn_lib_send_event(ch->bc, EVENT_RELEASE_COMPLETE );
			else
				misdn_lib_send_event(ch->bc, EVENT_RELEASE);
		}
		ch->overlap_dial_task = -1;
		return 0;
	} else
		return diff;
}

static void send_digit_to_chan(struct chan_list *cl, char digit )
{
	static const char *dtmf_tones[] =
    {
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
	struct cw_channel *chan=cl->cw; 
  
	if (digit >= '0' && digit <='9')
		cw_playtones_start(chan,0,dtmf_tones[digit-'0'], 0);
	else if (digit >= 'A' && digit <= 'D')
		cw_playtones_start(chan,0,dtmf_tones[digit-'A'+10], 0);
	else if (digit == '*')
		cw_playtones_start(chan,0,dtmf_tones[14], 0);
	else if (digit == '#')
		cw_playtones_start(chan,0,dtmf_tones[15], 0);
	else {
		/* not handled */
		cw_log(CW_LOG_DEBUG, "Unable to handle DTMF tone '%c' for '%s'\n", digit, chan->name);
    
    
	}
}

/*** CLI HANDLING ***/
static int misdn_set_debug(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	if (argc != 4 && argc != 5 && argc != 6 && argc != 7)
		return RESULT_SHOWUSAGE; 

	int level = atoi(argv[3]);

	switch (argc) {
		case 4:	
		case 5: {
					int only = 0;
					if (argc == 5) {
						if (strncasecmp(argv[4], "only", strlen(argv[4])))
							return RESULT_SHOWUSAGE;
						else
							only = 1;
					}
					int i;
					for (i=0; i<=max_ports; i++) {
						misdn_debug[i] = level;
						misdn_debug_only[i] = only;
					}
					cw_dynstr_printf(ds_p, "changing debug level for all ports to %d%s\n",misdn_debug[0], only?" (only)":"");
				}
				break;
		case 6: 
		case 7: {
					if (strncasecmp(argv[4], "port", strlen(argv[4])))
						return RESULT_SHOWUSAGE;
					int port = atoi(argv[5]);
					if (port <= 0 || port > max_ports) {
						switch (max_ports) {
							case 0:
								cw_dynstr_printf(ds_p, "port number not valid! no ports available so you won't get lucky with any number here...\n");
								break;
							case 1:
								cw_dynstr_printf(ds_p, "port number not valid! only port 1 is availble.\n");
								break;
							default:
								cw_dynstr_printf(ds_p, "port number not valid! only ports 1 to %d are available.\n", max_ports);
							}
							return 0;
					}
					if (argc == 7) {
						if (strncasecmp(argv[6], "only", strlen(argv[6])))
							return RESULT_SHOWUSAGE;
						else
							misdn_debug_only[port] = 1;
					} else
						misdn_debug_only[port] = 0;
					misdn_debug[port] = level;
					cw_dynstr_printf(ds_p, "changing debug level to %d%s for port %d\n", misdn_debug[port], misdn_debug_only[port]?" (only)":"", port);
				}
	}
	return 0;
}

static int misdn_set_crypt_debug(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	if (argc != 5) return RESULT_SHOWUSAGE; 

	return 0;
}


static int misdn_port_block(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int port;
  
	if (argc != 4)
		return RESULT_SHOWUSAGE;
  
	port = atoi(argv[3]);

	misdn_lib_port_block(port);

	return 0;
}

static int misdn_port_unblock(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int port;
  
	if (argc != 4)
		return RESULT_SHOWUSAGE;
  
	port = atoi(argv[3]);

	misdn_lib_port_unblock(port);

	return 0;
}


static int misdn_restart_port (struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int port;
  
	if (argc != 4)
		return RESULT_SHOWUSAGE;
  
	port = atoi(argv[3]);

	misdn_lib_port_restart(port);

	return 0;
}

static int misdn_port_up (struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int port;
	
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	
	port = atoi(argv[3]);
	
	misdn_lib_get_port_up(port);
  
	return 0;
}

static int misdn_port_down (struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int port;
	
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	
	port = atoi(argv[3]);
	
	misdn_lib_get_port_down(port);
  
	return 0;
}

static inline void show_config_description (struct cw_dynstr *ds_p, enum misdn_cfg_elements elem)
{
	char section[BUFFERSIZE];
	char name[BUFFERSIZE];
	char desc[BUFFERSIZE];
	char def[BUFFERSIZE];
	char tmp[BUFFERSIZE];

	misdn_cfg_get_name(elem, name, sizeof(tmp));
	misdn_cfg_get_desc(elem, desc, sizeof(desc), def, sizeof(def));

	if (elem < MISDN_CFG_LAST)
		cw_copy_string(section, "PORTS SECTION", sizeof(section));
	else
		cw_copy_string(section, "GENERAL SECTION", sizeof(section));

	if (*def)
		cw_dynstr_printf(ds_p, "[%s] %s   (Default: %s)\n\t%s\n", section, name, def, desc);
	else
		cw_dynstr_printf(ds_p, "[%s] %s\n\t%s\n", section, name, desc);
}

static int misdn_show_config (struct cw_dynstr *ds_p, int argc, char *argv[])
{
	char buffer[BUFFERSIZE];
	enum misdn_cfg_elements elem;
	int linebreak;
	int onlyport = -1;
	int ok = 0;

	if (argc >= 4) {
		if (!strcmp(argv[3], "description")) {
			if (argc == 5) {
				elem = misdn_cfg_get_elem (argv[4]);
				if (elem == MISDN_CFG_FIRST)
					cw_dynstr_printf(ds_p, "Unknown element: %s\n", argv[4]);
				else
					show_config_description(ds_p, elem);
				return 0;
			}
			return RESULT_SHOWUSAGE;
		}
		if (!strcmp(argv[3], "descriptions")) {
			if ((argc == 4) || ((argc == 5) && !strcmp(argv[4], "general"))) {
				for (elem = MISDN_GEN_FIRST + 1; elem < MISDN_GEN_LAST; ++elem) {
					show_config_description(ds_p, elem);
					cw_dynstr_printf(ds_p, "\n");
				}
				ok = 1;
			}
			if ((argc == 4) || ((argc == 5) && !strcmp(argv[4], "ports"))) {
				for (elem = MISDN_CFG_FIRST + 1; elem < MISDN_CFG_LAST - 1 /* the ptp hack, remove the -1 when ptp is gone */; ++elem) {
					show_config_description(ds_p, elem);
					cw_dynstr_printf(ds_p, "\n");
				}
				ok = 1;
			}
			return ok ? 0 : RESULT_SHOWUSAGE;
		}
		if (!sscanf(argv[3], "%d", &onlyport) || onlyport < 0) {
			cw_dynstr_printf(ds_p, "Unknown option: %s\n", argv[3]);
			return RESULT_SHOWUSAGE;
		}
	}
	
	if (argc == 3 || onlyport == 0) {
		cw_dynstr_printf(ds_p,
			"Misdn General-Config: \n"
			" -> Version: chan_misdn-" CHAN_MISDN_VERSION "\n"
		);
		for (elem = MISDN_GEN_FIRST + 1, linebreak = 1; elem < MISDN_GEN_LAST; elem++, linebreak++) {
			misdn_cfg_get_config_string( 0, elem, buffer, BUFFERSIZE);
			cw_dynstr_printf(ds_p, "%-36s%s", buffer, !(linebreak % 2) ? "\n" : "");
		}
		cw_dynstr_printf(ds_p, "\n");
	}

	if (onlyport < 0) {
		int port = misdn_cfg_get_next_port(0);
		for (; port > 0; port = misdn_cfg_get_next_port(port)) {
			cw_dynstr_printf(ds_p, "\n[PORT %d]\n", port);
			for (elem = MISDN_CFG_FIRST + 1, linebreak = 1; elem < MISDN_CFG_LAST; elem++, linebreak++) {
				misdn_cfg_get_config_string( port, elem, buffer, BUFFERSIZE);
				cw_dynstr_printf(ds_p, "%-36s%s", buffer, !(linebreak % 2) ? "\n" : "");
			}	
			cw_dynstr_printf(ds_p, "\n");
		}
	}
	
	if (onlyport > 0) {
		if (misdn_cfg_is_port_valid(onlyport)) {
			cw_dynstr_printf(ds_p, "[PORT %d]\n", onlyport);
			for (elem = MISDN_CFG_FIRST + 1, linebreak = 1; elem < MISDN_CFG_LAST; elem++, linebreak++) {
				misdn_cfg_get_config_string(onlyport, elem, buffer, BUFFERSIZE);
				cw_dynstr_printf(ds_p, "%-36s%s", buffer, !(linebreak % 2) ? "\n" : "");
			}	
			cw_dynstr_printf(ds_p, "\n");
		} else {
			cw_dynstr_printf(ds_p, "Port %d is not active!\n", onlyport);
		}
	}
	return 0;
}

struct state_struct {
	enum misdn_chan_state state;
	char txt[255] ;
} ;

static struct state_struct state_array[] = {
	{MISDN_NOTHING,"NOTHING"}, /* at beginning */
	{MISDN_WAITING4DIGS,"WAITING4DIGS"}, /*  when waiting for infos */
	{MISDN_EXTCANTMATCH,"EXTCANTMATCH"}, /*  when callweaver couldnt match our ext */
	{MISDN_DIALING,"DIALING"}, /*  when pbx_start */
	{MISDN_PROGRESS,"PROGRESS"}, /*  when pbx_start */
	{MISDN_PROCEEDING,"PROCEEDING"}, /*  when pbx_start */
	{MISDN_CALLING,"CALLING"}, /*  when misdn_call is called */
	{MISDN_CALLING_ACKNOWLEDGE,"CALLING_ACKNOWLEDGE"}, /*  when misdn_call is called */
	{MISDN_ALERTING,"ALERTING"}, /*  when Alerting */
	{MISDN_BUSY,"BUSY"}, /*  when BUSY */
	{MISDN_CONNECTED,"CONNECTED"}, /*  when connected */
	{MISDN_PRECONNECTED,"PRECONNECTED"}, /*  when connected */
	{MISDN_DISCONNECTED,"DISCONNECTED"}, /*  when connected */
	{MISDN_RELEASED,"RELEASED"}, /*  when connected */
	{MISDN_BRIDGED,"BRIDGED"}, /*  when bridged */
	{MISDN_CLEANING,"CLEANING"}, /* when hangup from * but we were connected before */
	{MISDN_HUNGUP_FROM_MISDN,"HUNGUP_FROM_MISDN"}, /* when DISCONNECT/RELEASE/REL_COMP  cam from misdn */
	{MISDN_HOLDED,"HOLDED"}, /* when DISCONNECT/RELEASE/REL_COMP  cam from misdn */
	{MISDN_HOLD_DISCONNECT,"HOLD_DISCONNECT"}, /* when DISCONNECT/RELEASE/REL_COMP  cam from misdn */
	{MISDN_HUNGUP_FROM_CW,"HUNGUP_FROM_CW"} /* when DISCONNECT/RELEASE/REL_COMP came out of */
	/* misdn_hangup */
};

static char *misdn_get_ch_state(struct chan_list *p) 
{
	int i;
	static char state[8];
	
	if( !p) return NULL;
  
	for (i=0; i< sizeof(state_array)/sizeof(struct state_struct); i++) {
		if ( state_array[i].state == p->state) return state_array[i].txt; 
	}

 	sprintf(state,"%d",p->state) ;

	return state;
}



static void reload_config(void)
{
	int i, cfg_debug;
	
	free_robin_list();
	misdn_cfg_reload();
	misdn_cfg_update_ptp();
	misdn_cfg_get( 0, MISDN_GEN_TRACEFILE, global_tracefile, BUFFERSIZE);
	misdn_cfg_get( 0, MISDN_GEN_DEBUG, &cfg_debug, sizeof(int));

	for (i = 0;  i <= max_ports; i++) {
		misdn_debug[i] = cfg_debug;
		misdn_debug_only[i] = 0;
	}
}

static int misdn_reload (struct cw_dynstr *ds_p, int argc, char *argv[])
{
	cw_dynstr_printf(ds_p, "Reloading mISDN Config\n");
	reload_config();
	return 0;
}

static void print_bc_info (struct cw_dynstr *ds_p, struct chan_list* help, struct misdn_bchannel* bc)
{
	struct cw_channel *cw=help->cw;
	cw_dynstr_printf(ds_p,
		"* Pid:%d Prt:%d Ch:%d Mode:%s Org:%s dad:%s oad:%s rad:%s ctx:%s state:%s\n",

		bc->pid, bc->port, bc->channel,
		bc->nt?"NT":"TE",
		help->orginator == ORG_CW?"*":"I",
		cw?cw->exten:NULL,
		cw?CW_CID_P(cw):NULL,
		bc->rad,
		cw?cw->context:NULL,
		misdn_get_ch_state(help)
		);
	if (misdn_debug[bc->port] > 0)
		cw_dynstr_printf(ds_p,
			"  --> cwname: %s\n"
			"  --> ch_l3id: %x\n"
			"  --> ch_addr: %x\n"
			"  --> bc_addr: %x\n"
			"  --> bc_l3id: %x\n"
			"  --> display: %s\n"
			"  --> activated: %d\n"
			"  --> state: %s\n"
			"  --> capability: %s\n"
			"  --> echo_cancel: %d\n"
#ifdef WITH_BEROEC
			"  --> bnec_tail: %d\n"
			"  --> bnec_nlp: %d\n"
			"  --> bnec_ah: %d\n"
			"  --> bnec_td: %d\n"
			"  --> bnec_zerocoeff: %d\n"
#endif
			"  --> notone : rx %d tx:%d\n"
			"  --> bc_hold: %d holded_bc :%d\n",
			help->cw->name,
			help->l3id,
			help->addr,
			bc->addr,
			bc?bc->l3_id:-1,
			bc->display,
			
			bc->active,
			bc_state2str(bc->bc_state),
			bearer2str(bc->capability),
			bc->ec_enable,

#ifdef WITH_BEROEC
			bc->bnec_tail,
			bc->bnec_nlp,
			bc->bnec_ah,
			bc->bnec_td,
			bc->bnec_zero,
#endif
			help->norxtone,help->notxtone,
			bc->holded, help->holded_bc?1:0
			);
  
}

static int misdn_show_cls (struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct chan_list *help=cl_te;
  
	cw_dynstr_printf(ds_p,"Chan List: %p\n",cl_te);
  
	for (;help; help=help->next) {
		struct misdn_bchannel *bc=help->bc;   
		struct cw_channel *cw=help->cw;
		if (misdn_debug[0] > 2) cw_dynstr_printf(ds_p, "Bc:%p Opbx:%p\n", bc, cw);
		if (bc) {
			print_bc_info(ds_p, help, bc);
		} else if ( (bc=help->holded_bc) ) {
			chan_misdn_log(0, 0, "ITS A HOLDED BC:\n");
			print_bc_info(ds_p, help,  bc);
		} else {
			cw_dynstr_printf(ds_p,"* Channel in unknown STATE !!! Exten:%s, Callerid:%s\n", cw->exten, CW_CID_P(cw));
		}
	}
  
  
	return 0;
}

static int misdn_show_cl (struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct chan_list *help=cl_te;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
  
	for (;help; help=help->next) {
		struct misdn_bchannel *bc=help->bc;   
		struct cw_channel *cw=help->cw;
    
		if (bc && cw) {
			if (!strcasecmp(cw->name,argv[3])) {
				print_bc_info(ds_p, help, bc);
				break; 
			}
		} 
	}
  
  
	return 0;
}

cw_mutex_t lock;
int MAXTICS=8;

static int misdn_set_tics (struct cw_dynstr *ds_p, int argc, char *argv[])
{
	if (argc != 4)
		return RESULT_SHOWUSAGE;
  
	MAXTICS=atoi(argv[3]);
  
	return 0;
}

static int misdn_show_stacks (struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int port;

	cw_dynstr_printf(ds_p, "BEGIN STACK_LIST:\n");

	for (port=misdn_cfg_get_next_port(0); port > 0;
	     port=misdn_cfg_get_next_port(port)) {
		char buf[128];
		get_show_stack_details(port,buf);
		cw_dynstr_printf(ds_p,"  %s  Debug:%d%s\n", buf, misdn_debug[port], misdn_debug_only[port]?"(only)":"");
	}
		
	return 0;
}


static int misdn_show_ports_stats (struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int port;

	cw_dynstr_printf(ds_p, "Port\tin_calls\tout_calls\n");
	
	for (port=misdn_cfg_get_next_port(0); port > 0;
	     port=misdn_cfg_get_next_port(port)) {
		cw_dynstr_printf(ds_p,"%d\t%d\t\t%d\n",port,misdn_in_calls[port],misdn_out_calls[port]);
	}
	cw_dynstr_printf(ds_p,"\n");
	
	return 0;

}


static int misdn_show_port (struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int port;
	
	if (argc != 4)
		return RESULT_SHOWUSAGE;
  
	port = atoi(argv[3]);
  
	cw_dynstr_printf(ds_p, "BEGIN STACK_LIST:\n");

	char buf[128];
	get_show_stack_details(port,buf);
	cw_dynstr_printf(ds_p,"  %s  Debug:%d%s\n",buf, misdn_debug[port], misdn_debug_only[port]?"(only)":"");

	
	return 0;
}

static int misdn_send_cd (struct cw_dynstr *ds_p, int argc, char *argv[])
{
	char *channame; 
	char *nr; 
  
	if (argc != 5)
		return RESULT_SHOWUSAGE;
  
	channame = argv[3];
	nr = argv[4];
	
	cw_dynstr_printf(ds_p, "Sending Calldeflection (%s) to %s\n",nr, channame);
	
	{
		struct chan_list *tmp=get_chan_by_cw_name(channame);
		
		if (!tmp) {
			cw_dynstr_printf(ds_p, "Sending CD with nr %s to %s failed Channel does not exist\n",nr, channame);
			return 0; 
		} else {
			
			misdn_lib_send_facility(tmp->bc, FACILITY_CALLDEFLECT, nr);
		}
	}
  
	return 0; 
}

static int misdn_send_digit (struct cw_dynstr *ds_p, int argc, char *argv[])
{
	char *channame; 
	char *msg; 
  
	if (argc != 5)
		return RESULT_SHOWUSAGE;
  
	channame = argv[3];
	msg = argv[4];

	cw_dynstr_printf(ds_p, "Sending %s to %s\n",msg, channame);
  
	{
		struct chan_list *tmp=get_chan_by_cw_name(channame);
    
		if (!tmp) {
			cw_dynstr_printf(ds_p, "Sending %s to %s failed Channel does not exist\n",msg, channame);
			return 0; 
		} else {
#if 1
			int i;
			int msglen = strlen(msg);
			for (i=0; i<msglen; i++) {
				cw_dynstr_printf(ds_p, "Sending: %c\n",msg[i]);
				send_digit_to_chan(tmp, msg[i]);
				/* res = cw_safe_sleep(tmp->cw, 250); */
				usleep(250000);
				/* res = cw_waitfor(tmp->cw,100); */
			}
#else
			int res;
			res = cw_dtmf_stream(tmp->cw,NULL,msg,250);
#endif
		}
	}
  
	return 0; 
}

static int misdn_toggle_echocancel (struct cw_dynstr *ds_p, int argc, char *argv[])
{
	char *channame; 

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	
	channame = argv[3];
  
	cw_dynstr_printf(ds_p, "Toggling EchoCancel on %s\n", channame);
  
	{
		struct chan_list *tmp=get_chan_by_cw_name(channame);
    
		if (!tmp) {
			cw_dynstr_printf(ds_p, "Toggling EchoCancel %s failed Channel does not exist\n", channame);
			return 0; 
		} else {
			
			tmp->toggle_ec=tmp->toggle_ec?0:1;

			if (tmp->toggle_ec) {
				update_ec_config(tmp->bc);
				manager_ec_enable(tmp->bc);
			} else {
				manager_ec_disable(tmp->bc);
			}
		}
	}
  
	return 0; 
}

static int misdn_send_display (struct cw_dynstr *ds_p, int argc, char *argv[])
{
	char *channame; 
	char *msg; 
  
	if (argc != 5)
		return RESULT_SHOWUSAGE;
  
	channame = argv[3];
	msg = argv[4];

	cw_dynstr_printf(ds_p, "Sending %s to %s\n",msg, channame);
	{
		struct chan_list *tmp;
		tmp=get_chan_by_cw_name(channame);
    
		if (tmp && tmp->bc) {
			cw_copy_string(tmp->bc->display, msg, sizeof(tmp->bc->display));
			misdn_lib_send_event(tmp->bc, EVENT_INFORMATION);
		} else {
			cw_dynstr_printf(ds_p,"No such channel %s\n",channame);
			return RESULT_FAILURE;
		}
	}

	return RESULT_SUCCESS ;
}

static void complete_ch(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	if (lastarg == 3)
		cw_complete_channel(ds_p, argv[3], lastarg_len);
}

static void complete_debug_port(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	switch (lastarg) {
		case 4:
			if (!strncmp(argv[4], "port", lastarg_len))
				cw_dynstr_printf(ds_p, "port\n");
			if (!strncmp(argv[4], "only", lastarg_len))
				cw_dynstr_printf(ds_p, "only\n");
			break;
		case 6:
			if (!strncmp(argv[6], "only", lastarg_len))
				cw_dynstr_printf(ds_p, "only\n");
			break;
	}
}

static void complete_show_config(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	char buffer[BUFFERSIZE];
	enum misdn_cfg_elements elem;
	int port = 0;

	switch (lastarg) {
		case 3:
			if ((!strncmp(argv[3], "description", lastarg_len)))
				cw_dynstr_printf(ds_p, "description\n");

			if ((!strncmp(argv[3], "descriptions", lastarg_len)))
				cw_dynstr_printf(ds_p, "descriptions\n");

			if ((!strncmp(argv[3], "0", lastarg_len)))
				cw_dynstr_printf(ds_p, "0\n");

			while ((port = misdn_cfg_get_next_port(port)) != -1) {
				snprintf(buffer, sizeof(buffer), "%d", port);
				if ((!strncmp(argv[3], buffer, lastarg_len)))
					cw_dynstr_printf(ds_p, "%s\n", buffer);
			}
			break;

		case 4:
			if (!strcmp(argv[3], "description")) {
				for (elem = MISDN_CFG_FIRST + 1; elem < MISDN_GEN_LAST; ++elem) {
					if ((elem == MISDN_CFG_LAST) || (elem == MISDN_GEN_FIRST))
						continue;

					misdn_cfg_get_name(elem, buffer, BUFFERSIZE);
					if (!strncmp(argv[4], buffer, lastarg_len))
						cw_dynstr_printf(ds_p, "%s\n", buffer);
				}
			} else if (!strcmp(argv[3], "descriptions")) {
				if (!strncmp(argv[4], "general", lastarg_len))
					cw_dynstr_printf(ds_p, "general\n");
				if (!strncmp(argv[4], "ports", lastarg_len))
					cw_dynstr_printf(ds_p, "ports\n");
			}
			break;
	}
}

static struct cw_clicmd cli_send_cd = {
	.cmda = {"misdn","send","calldeflect", NULL},
	.handler = misdn_send_cd,
	.summary = "Sends CallDeflection to mISDN Channel", 
	.usage = "Usage: misdn send calldeflect <channel> \"<nr>\" \n",
	.generator = complete_ch
};

static struct cw_clicmd cli_send_digit = {
	.cmda = {"misdn","send","digit", NULL},
	.handler = misdn_send_digit,
	.summary = "Sends DTMF Digit to mISDN Channel", 
	.usage = "Usage: misdn send digit <channel> \"<msg>\" \n"
	"       Send <digit> to <channel> as DTMF Tone\n"
	"       when channel is a mISDN channel\n",
	.generator = complete_ch
};

static struct cw_clicmd cli_toggle_echocancel = {
	.cmda = {"misdn","toggle","echocancel", NULL},
	.handler = misdn_toggle_echocancel,
	.summary = "Toggles EchoCancel on mISDN Channel", 
	.usage = "Usage: misdn toggle echocancel <channel>\n", 
	.generator = complete_ch
};

static struct cw_clicmd cli_send_display = {
	.cmda = {"misdn","send","display", NULL},
	.handler = misdn_send_display,
	.summary = "Sends Text to mISDN Channel", 
	.usage = "Usage: misdn send display <channel> \"<msg>\" \n"
	"       Send <msg> to <channel> as Display Message\n"
	"       when channel is a mISDN channel\n",
	.generator = complete_ch
};

static struct cw_clicmd cli_show_config = {
	.cmda = {"misdn","show","config", NULL},
	.handler = misdn_show_config,
	.summary = "Shows internal mISDN config, read from cfg-file", 
	.usage = "Usage: misdn show config [<port> | description <config element> | descriptions [general|ports]]\n"
	"       Use 0 for <port> to only print the general config.\n",
	.generator = complete_show_config
};
 
static struct cw_clicmd cli_reload = {
	.cmda = {"misdn","reload", NULL},
	.handler = misdn_reload,
	.summary = "Reloads internal mISDN config, read from cfg-file", 
	.usage = "Usage: misdn reload\n"
};

static struct cw_clicmd cli_set_tics = {
	.cmda = {"misdn","set","tics", NULL},
	.handler = misdn_set_tics,
	.summary = "", 
	"\n"
};

static struct cw_clicmd cli_show_cls = {
	.cmda = {"misdn","show","channels", NULL},
	.handler = misdn_show_cls,
	.summary = "Shows internal mISDN chan_list", 
	.usage = "Usage: misdn show channels\n"
};

static struct cw_clicmd cli_show_cl = {
	.cmda = {"misdn","show","channel", NULL},
	.handler = misdn_show_cl,
	.summary = "Shows internal mISDN chan_list", 
	.usage = "Usage: misdn show channels\n",
	.generator = complete_ch
};

static struct cw_clicmd cli_port_block = {
	.cmda = {"misdn","port","block", NULL},
	.handler = misdn_port_block,
	.summary = "Blocks the given port", 
	.usage = "Usage: misdn port block\n"
};

static struct cw_clicmd cli_port_unblock = {
	.cmda = {"misdn","port","unblock", NULL},
	.handler = misdn_port_unblock,
	.summary = "Unblocks the given port", 
	.usage = "Usage: misdn port unblock\n"
};


static struct cw_clicmd cli_restart_port = {
	.cmda = {"misdn","restart","port", NULL},
	.handler = misdn_restart_port,
	.summary = "Restarts the given port", 
	.usage = "Usage: misdn restart port\n"
};

static struct cw_clicmd cli_port_up = {
	.cmda = {"misdn","port","up", NULL},
	.handler = misdn_port_up,
	.summary = "Tries to establish L1 on the given port", 
	.usage = "Usage: misdn port up <port>\n"
};

static struct cw_clicmd cli_port_down = {
	.cmda = {"misdn","port","down", NULL},
	.handler = misdn_port_down,
	.summary = "Tries to deacivate the L1 on the given port", 
	.usage = "Usage: misdn port down <port>\n"
};



static struct cw_clicmd cli_show_stacks = {
	.cmda = {"misdn","show","stacks", NULL},
	.handler = misdn_show_stacks,
	.summary = "Shows internal mISDN stack_list", 
	.usage = "Usage: misdn show stacks\n"
};

static struct cw_clicmd cli_show_ports_stats = {
	.cmda = {"misdn","show","ports","stats", NULL},
	.handler = misdn_show_ports_stats,
	.summary = "Shows chan_misdns call statistics per port", 
	.usage = "Usage: misdn show port stats\n"
};


static struct cw_clicmd cli_show_port = {
	.cmda = {"misdn","show","port", NULL},
	.handler = misdn_show_port,
	.summary = "Shows detailed information for given port", 
	.usage = "Usage: misdn show port <port>\n"
};

static struct cw_clicmd cli_set_debug = {
	.cmda = {"misdn","set","debug", NULL},
	.handler = misdn_set_debug,
	.summary = "Sets Debuglevel of chan_misdn",
	.usage = "Usage: misdn set debug <level> [only] | [port <port> [only]]\n",
	.generator = complete_debug_port
};

static struct cw_clicmd cli_set_crypt_debug = {
	.cmda = {"misdn","set","crypt","debug", NULL},
	.handler = misdn_set_crypt_debug,
	.summary = "Sets CryptDebuglevel of chan_misdn, at the moment, level={1,2}", 
	.usage = "Usage: misdn set crypt debug <level>\n"
};
/*** CLI END ***/


static int update_config (struct chan_list *ch, int orig) 
{
	if (!ch) {
		cw_log(CW_LOG_WARNING, "Cannot configure without chanlist\n");
		return -1;
	}
	
	struct cw_channel *cw=ch->cw;
	struct misdn_bchannel *bc=ch->bc;
	if (! cw || ! bc ) {
		cw_log(CW_LOG_WARNING, "Cannot configure without cw || bc\n");
		return -1;
	}
	
	int port=bc->port;
	
	chan_misdn_log(1,port,"update_config: Getting Config\n");


	int hdlc=0;
	misdn_cfg_get( port, MISDN_CFG_HDLC, &hdlc, sizeof(int));
	
	if (hdlc) {
		switch (bc->capability) {
		case INFO_CAPABILITY_DIGITAL_UNRESTRICTED:
		case INFO_CAPABILITY_DIGITAL_RESTRICTED:
			chan_misdn_log(1,bc->port," --> CONF HDLC\n");
			bc->hdlc=1;
			break;
		}
		
	}
	
	
	int pres, screen;
			
	misdn_cfg_get( port, MISDN_CFG_PRES, &pres, sizeof(int));
	misdn_cfg_get( port, MISDN_CFG_SCREEN, &screen, sizeof(int));
	chan_misdn_log(2,port," --> pres: %d screen: %d\n",pres, screen);
		
	if ( (pres + screen) < 0 ) {

		chan_misdn_log(2,port," --> pres: %x\n", cw->cid.cid_pres);
			
		switch (cw->cid.cid_pres & 0x60){
				
		case CW_PRES_RESTRICTED:
			bc->pres=1;
			chan_misdn_log(2, port, " --> PRES: Restricted (0x1)\n");
			break;
				
				
		case CW_PRES_UNAVAILABLE:
			bc->pres=2;
			chan_misdn_log(2, port, " --> PRES: Unavailable (0x2)\n");
			break;
				
		default:
			bc->pres=0;
			chan_misdn_log(2, port, " --> PRES: Allowed (0x0)\n");
		}
			
		switch (cw->cid.cid_pres & 0x3){
				
		case CW_PRES_USER_NUMBER_UNSCREENED:
			bc->screen=0;
			chan_misdn_log(2, port, " --> SCREEN: Unscreened (0x0)\n");
			break;

		case CW_PRES_USER_NUMBER_PASSED_SCREEN:
			bc->screen=1;
			chan_misdn_log(2, port, " --> SCREEN: Passed Screen (0x1)\n");
			break;
		case CW_PRES_USER_NUMBER_FAILED_SCREEN:
			bc->screen=2;
			chan_misdn_log(2, port, " --> SCREEN: Failed Screen (0x2)\n");
			break;
				
		case CW_PRES_NETWORK_NUMBER:
			bc->screen=3;
			chan_misdn_log(2, port, " --> SCREEN: Network Nr. (0x3)\n");
			break;
				
		default:
			bc->screen=0;
			chan_misdn_log(2, port, " --> SCREEN: Unscreened (0x0)\n");
		}

			
	} else {
		bc->screen=screen;
		bc->pres=pres;
	}

	return 0;
	
}




static void config_jitterbuffer(struct chan_list *ch)
{
	struct misdn_bchannel *bc=ch->bc;
	int len=ch->jb_len, threshold=ch->jb_upper_threshold;
	
	chan_misdn_log(5,bc->port, "config_jb: Called\n");
	
	if ( ! len ) {
		chan_misdn_log(1,bc->port, "config_jb: Deactivating Jitterbuffer\n");
		bc->nojitter=1;
	} else {
		
		if (len <=100 || len > 8000) {
			chan_misdn_log(0,bc->port,"config_jb: Jitterbuffer out of Bounds, setting to 1000\n");
			len=1000;
		}
		
		if ( threshold > len ) {
			chan_misdn_log(0,bc->port,"config_jb: Jitterbuffer Threshold > Jitterbuffer setting to Jitterbuffer -1\n");
		}
		
		if ( ch->jb) {
			cb_log(0,bc->port,"config_jb: We've got a Jitterbuffer Already on this port.\n");
			misdn_jb_destroy(ch->jb);
			ch->jb=NULL;
		}
		
		ch->jb=misdn_jb_init(len, threshold);
		ch->jb_rx=misdn_jb_init(len, threshold);
		//ch->jb_rx=misdn_jb_init(len, threshold);

		if (!ch->jb  || !ch->jb_rx) 
			bc->nojitter=1;
	}
}


static void debug_numplan(int port, int numplan, char *type)
{
	switch (numplan) {
	case NUMPLAN_INTERNATIONAL:
		chan_misdn_log(2, port, " --> %s: International\n",type);
		break;
	case NUMPLAN_NATIONAL:
		chan_misdn_log(2, port, " --> %s: National\n",type);
		break;
	case NUMPLAN_SUBSCRIBER:
		chan_misdn_log(2, port, " --> %s: Subscriber\n",type);
		break;
	case NUMPLAN_UNKNOWN:
		chan_misdn_log(2, port, " --> %s: Unknown\n",type);
		break;
		/* Maybe we should cut off the prefix if present ? */
	default:
		chan_misdn_log(0, port, " --> !!!! Wrong dialplan setting, please see the misdn.conf sample file\n ");
		break;
	}
}




static int update_ec_config(struct misdn_bchannel *bc)
{
	int ec;
	int port=bc->port;
		
	misdn_cfg_get( port, MISDN_CFG_ECHOCANCEL, &ec, sizeof(int));
	
	if (ec == 1 ) {
		bc->ec_enable=1;
	} else if ( ec > 1 ) {
		bc->ec_enable=1;
		bc->ec_deftaps=ec;
	}
#ifdef WITH_ECHOTRAINING 
	int ectr;
	misdn_cfg_get( port, MISDN_CFG_ECHOTRAINING, &ectr, sizeof(int));
	
	if ( ectr >= 0 ) {
		bc->ec_training=ectr;
	}
#endif

#ifdef WITH_BEROEC
	misdn_cfg_get(port, MISDN_CFG_BNECHOCANCEL,&bc->bnec_tail, sizeof(int));
	misdn_cfg_get(port, MISDN_CFG_BNEC_ANTIHOWL, &bc->bnec_ah, sizeof(int));
	misdn_cfg_get(port, MISDN_CFG_BNEC_NLP, &bc->bnec_nlp, sizeof(int));
	misdn_cfg_get(port, MISDN_CFG_BNEC_TD, &bc->bnec_td, sizeof(int));
	misdn_cfg_get(port, MISDN_CFG_BNEC_ADAPT, &bc->bnec_adapt, sizeof(int));
	misdn_cfg_get(port, MISDN_CFG_BNEC_ZEROCOEFF, &bc->bnec_zero, sizeof(int));

	if (bc->bnec_tail && bc->ec_enable) {
		cw_log(CW_LOG_WARNING,"Are you sure you wan't to mix BNEC with Zapec ? This might cause bad audio quality!\n");
		bc->ec_enable=0;
	}
#endif
	return 0;
}


static int read_config(struct chan_list *ch, int orig) {

	if (!ch) {
		cw_log(CW_LOG_WARNING, "Cannot configure without chanlist\n");
		return -1;
	}

	struct cw_channel *cw=ch->cw;
	struct misdn_bchannel *bc=ch->bc;
	if (! cw || ! bc ) {
		cw_log(CW_LOG_WARNING, "Cannot configure without cw || bc\n");
		return -1;
	}
	
	int port=bc->port;
	
	chan_misdn_log(5,port,"read_config: Getting Config\n");
	
	char lang[BUFFERSIZE+1];

	misdn_cfg_get( port, MISDN_CFG_LANGUAGE, lang, BUFFERSIZE);
	cw_copy_string(cw->language, lang, sizeof(cw->language));
	
	char musicclass[BUFFERSIZE+1];
	
	misdn_cfg_get( port, MISDN_CFG_MUSICCLASS, musicclass, BUFFERSIZE);
	cw_copy_string(cw->musicclass, musicclass, sizeof(cw->musicclass));
	
	misdn_cfg_get( port, MISDN_CFG_TXGAIN, &bc->txgain, sizeof(int));
	misdn_cfg_get( port, MISDN_CFG_RXGAIN, &bc->rxgain, sizeof(int));
	
	misdn_cfg_get( port, MISDN_CFG_INCOMING_EARLY_AUDIO, &ch->incoming_early_audio, sizeof(int));
	
	misdn_cfg_get( port, MISDN_CFG_SENDDTMF, &bc->send_dtmf, sizeof(int));

	misdn_cfg_get( port, MISDN_CFG_NEED_MORE_INFOS, &bc->need_more_infos, sizeof(int));
	
	misdn_cfg_get( port, MISDN_CFG_FAR_ALERTING, &ch->far_alerting, sizeof(int));

	misdn_cfg_get( port, MISDN_CFG_ALLOWED_BEARERS, &ch->allowed_bearers, BUFFERSIZE);

	char faxdetect[BUFFERSIZE+1];
	misdn_cfg_get( port, MISDN_CFG_FAXDETECT, faxdetect, BUFFERSIZE);

	int hdlc=0;
	misdn_cfg_get( port, MISDN_CFG_HDLC, &hdlc, sizeof(int));
	
	if (hdlc) {
		switch (bc->capability) {
		case INFO_CAPABILITY_DIGITAL_UNRESTRICTED:
		case INFO_CAPABILITY_DIGITAL_RESTRICTED:
			chan_misdn_log(1,bc->port," --> CONF HDLC\n");
			bc->hdlc=1;
			break;
		}
		
	}
	/*Initialize new Jitterbuffer*/
	{
		misdn_cfg_get( port, MISDN_CFG_JITTERBUFFER, &ch->jb_len, sizeof(int));
		misdn_cfg_get( port, MISDN_CFG_JITTERBUFFER_UPPER_THRESHOLD, &ch->jb_upper_threshold, sizeof(int));
		
		config_jitterbuffer(ch);
	}
	
	misdn_cfg_get( bc->port, MISDN_CFG_CONTEXT, ch->context, sizeof(ch->context));
	
	cw_copy_string (cw->context,ch->context,sizeof(cw->context));	

	update_ec_config(bc);

	{
		int eb3;
		
		misdn_cfg_get( bc->port, MISDN_CFG_EARLY_BCONNECT, &eb3, sizeof(int));
		bc->early_bconnect=eb3;
	}
	
	port=bc->port;
	
	{
		char buf[256];
		cw_group_t pg,cg;
		
		misdn_cfg_get(port, MISDN_CFG_PICKUPGROUP, &pg, sizeof(pg));
		misdn_cfg_get(port, MISDN_CFG_CALLGROUP, &cg, sizeof(cg));
		
		chan_misdn_log(5, port, " --> * CallGrp:%s PickupGrp:%s\n",cw_print_group(buf,sizeof(buf),cg),cw_print_group(buf,sizeof(buf),pg));
		cw->pickupgroup=pg;
		cw->callgroup=cg;
	}
	
	if ( orig  == ORG_CW) {
		misdn_cfg_get( port, MISDN_CFG_TE_CHOOSE_CHANNEL, &(bc->te_choose_channel), sizeof(int));

		if (strstr(faxdetect, "outgoing") || strstr(faxdetect, "both")) {
			if (strstr(faxdetect, "nojump"))
				ch->faxdetect=2;
			else
				ch->faxdetect=1;
		}
		
		{
			char callerid[BUFFERSIZE+1];
			misdn_cfg_get( port, MISDN_CFG_CALLERID, callerid, BUFFERSIZE);
			if ( ! cw_strlen_zero(callerid) ) {
				chan_misdn_log(1, port, " --> * Setting Cid to %s\n", callerid);
				{
					int l = sizeof(bc->oad);
					strncpy(bc->oad,callerid, l);
					bc->oad[l-1] = 0;
				}

			}

			
			misdn_cfg_get( port, MISDN_CFG_DIALPLAN, &bc->dnumplan, sizeof(int));
			misdn_cfg_get( port, MISDN_CFG_LOCALDIALPLAN, &bc->onumplan, sizeof(int));
			misdn_cfg_get( port, MISDN_CFG_CPNDIALPLAN, &bc->cpnnumplan, sizeof(int));
			debug_numplan(port, bc->dnumplan,"TON");
			debug_numplan(port, bc->onumplan,"LTON");
			debug_numplan(port, bc->cpnnumplan,"CTON");
		}

		ch->overlap_dial = 0;
	} else { /** ORIGINATOR MISDN **/
		if (strstr(faxdetect, "incoming") || strstr(faxdetect, "both")) {
			if (strstr(faxdetect, "nojump"))
				ch->faxdetect=2;
			else
				ch->faxdetect=1;
		}
	
		misdn_cfg_get( port, MISDN_CFG_CPNDIALPLAN, &bc->cpnnumplan, sizeof(int));
		debug_numplan(port, bc->cpnnumplan,"CTON");
		
		char prefix[BUFFERSIZE+1]="";
		switch( bc->onumplan ) {
		case NUMPLAN_INTERNATIONAL:
			misdn_cfg_get( bc->port, MISDN_CFG_INTERNATPREFIX, prefix, BUFFERSIZE);
			break;
			
		case NUMPLAN_NATIONAL:
			misdn_cfg_get( bc->port, MISDN_CFG_NATPREFIX, prefix, BUFFERSIZE);
			break;
		default:
			break;
		}
		
		{
			int l = strlen(prefix) + strlen(bc->oad);
			char tmp[l+1];
			strcpy(tmp,prefix);
			strcat(tmp,bc->oad);
			strcpy(bc->oad,tmp);
		}
		
		if (!cw_strlen_zero(bc->dad)) {
			cw_copy_string(bc->orig_dad,bc->dad, sizeof(bc->orig_dad));
		}
		
		if ( cw_strlen_zero(bc->dad) && !cw_strlen_zero(bc->keypad)) {
			cw_copy_string(bc->dad,bc->keypad, sizeof(bc->dad));
		}

		prefix[0] = 0;
		
		switch( bc->dnumplan ) {
		case NUMPLAN_INTERNATIONAL:
			misdn_cfg_get( bc->port, MISDN_CFG_INTERNATPREFIX, prefix, BUFFERSIZE);
			break;
		case NUMPLAN_NATIONAL:
			misdn_cfg_get( bc->port, MISDN_CFG_NATPREFIX, prefix, BUFFERSIZE);
			break;
		default:
			break;
		}
		
		{
			int l = strlen(prefix) + strlen(bc->dad);
			char tmp[l+1];
			strcpy(tmp,prefix);
			strcat(tmp,bc->dad);
			strcpy(bc->dad,tmp);
		}
		
		if ( strcmp(bc->dad,cw->exten)) {
			cw_copy_string(cw->exten, bc->dad, sizeof(cw->exten));
		}
		
		cw_set_callerid(cw, bc->oad, NULL, bc->oad);
		
		if ( !cw_strlen_zero(bc->rad) ) 
			cw->cid.cid_rdnis=strdup(bc->rad);
		
		misdn_cfg_get(bc->port, MISDN_CFG_OVERLAP_DIAL, &ch->overlap_dial, sizeof(ch->overlap_dial));
		cw_mutex_init(&ch->overlap_tv_lock);
	} /* ORIG MISDN END */

	ch->overlap_dial_task = -1;
	
	if (ch->faxdetect) {
		misdn_cfg_get( port, MISDN_CFG_FAXDETECT_TIMEOUT, &ch->faxdetect_timeout, sizeof(ch->faxdetect_timeout));
		if (!ch->dsp)
			ch->dsp = cw_dsp_new();
		if (ch->dsp)
			cw_dsp_set_features(ch->dsp, DSP_FEATURE_DTMF_DETECT | DSP_FEATURE_FAX_CNG_DETECT);
		if (!ch->trans)
			ch->trans=cw_translator_build_path(CW_FORMAT_SLINEAR, CW_FORMAT_ALAW);
	}

	return 0;
}


/*****************************/
/*** CW Indications Start ***/
/*****************************/

static int misdn_call(struct cw_channel *cw, const char *dest)
{
	int port=0;
	int r;
	struct chan_list *ch=MISDN_CALLWEAVER_TECH_PVT(cw);
	struct misdn_bchannel *newbc;
	char *opts=NULL, *ext,*tokb;
	char dest_cp[256];
	
	{
		strncpy(dest_cp,dest,sizeof(dest_cp)-1);
		dest_cp[sizeof(dest_cp)]=0;
		
		ext=strtok_r(dest_cp,"/",&tokb);
		
		if (ext) {
			ext=strtok_r(NULL,"/",&tokb);
			if (ext) {
				opts=strtok_r(NULL,"/",&tokb);
			} else {
				chan_misdn_log(0,0,"misdn_call: No Extension given!\n");
				return -1;
			}
		}
	}

	if (!cw) {
		cw_log(CW_LOG_WARNING, " --> ! misdn_call called on cw_channel *cw where cw == NULL\n");
		return -1;
	}

	if (((cw->_state != CW_STATE_DOWN) && (cw->_state != CW_STATE_RESERVED)) || !dest  ) {
		cw_log(CW_LOG_WARNING, " --> ! misdn_call called on %s, neither down nor reserved (or dest==NULL)\n", cw->name);
		cw->hangupcause=41;
		cw_setstate(cw, CW_STATE_DOWN);
		return -1;
	}

	if (!ch) {
		cw_log(CW_LOG_WARNING, " --> ! misdn_call called on %s, neither down nor reserved (or dest==NULL)\n", cw->name);
		cw->hangupcause=41;
		cw_setstate(cw, CW_STATE_DOWN);
		return -1;
	}
	
	newbc=ch->bc;
	
	if (!newbc) {
		cw_log(CW_LOG_WARNING, " --> ! misdn_call called on %s, neither down nor reserved (or dest==NULL)\n", cw->name);
		cw->hangupcause=41;
		cw_setstate(cw, CW_STATE_DOWN);
		return -1;
	}
	
	port=newbc->port;
	strncpy(newbc->dad,ext,sizeof( newbc->dad));
	strncpy(cw->exten,ext,sizeof(cw->exten));

	int exceed;
	if ((exceed=add_out_calls(port))) {
		char tmp[16];
		sprintf(tmp,"%d",exceed);
		pbx_builtin_setvar_helper(cw,"MAX_OVERFLOW",tmp);
		return -1;
	}
	
	chan_misdn_log(1, port, "* CALL: %s\n",dest);
	
	chan_misdn_log(1, port, " --> * dad:%s tech:%s ctx:%s\n",cw->exten,cw->name, cw->context);
	
	chan_misdn_log(3, port, " --> * adding2newbc ext %s\n",cw->exten);
	if (cw->exten) {
		int l = sizeof(newbc->dad);
		strncpy(newbc->dad,cw->exten, l);
		newbc->dad[l-1] = 0;
	}
	newbc->rad[0]=0;
	chan_misdn_log(3, port, " --> * adding2newbc callerid %s\n",CW_CID_P(cw));
	if (cw_strlen_zero(newbc->oad) && CW_CID_P(cw) ) {

		if (CW_CID_P(cw)) {
			int l = sizeof(newbc->oad);
			strncpy(newbc->oad,CW_CID_P(cw), l);
			newbc->oad[l-1] = 0;
		}
	}
	
	{
		struct chan_list *ch=MISDN_CALLWEAVER_TECH_PVT(cw);
		if (!ch) { cw_verbose("No chan_list in misdn_call"); return -1;}
		
		newbc->capability=cw->transfercapability;
		pbx_builtin_setvar_helper(cw,"TRANSFERCAPABILITY",cw_transfercapability2str(newbc->capability));
		if ( cw->transfercapability == INFO_CAPABILITY_DIGITAL_UNRESTRICTED) {
			chan_misdn_log(2, port, " --> * Call with flag Digital\n");
		}
		

		/* update screening and presentation */ 
		update_config(ch,ORG_CW);
		
		/* fill in some ies from channel vary*/
		import_ch(cw, newbc, ch);
		
		/* Finally The Options Override Everything */
		if (opts)
			misdn_set_opt_exec(cw, 1, &opts, NULL);
		else
			chan_misdn_log(2,port,"NO OPTS GIVEN\n");
		
		ch->state=MISDN_CALLING;
		
		r=misdn_lib_send_event( newbc, EVENT_SETUP );
		
		/** we should have l3id after sending setup **/
		ch->l3id=newbc->l3_id;
	}
	
	if ( r == -ENOCHAN  ) {
		chan_misdn_log(0, port, " --> * Theres no Channel at the moment .. !\n");
		chan_misdn_log(1, port, " --> * SEND: State Down pid:%d\n",newbc?newbc->pid:-1);
		cw->hangupcause=34;
		cw_setstate(cw, CW_STATE_DOWN);
		return -1;
	}
	
	chan_misdn_log(1, port, " --> * SEND: State Dialing pid:%d\n",newbc?newbc->pid:1);

	cw_setstate(cw, CW_STATE_DIALING);
	cw->hangupcause=16;
	
	if (newbc->nt) stop_bc_tones(ch);
	
	return 0; 
}


static int misdn_answer(struct cw_channel *cw)
{
	struct chan_list *p;
	struct cw_var_t *var;
	
	if (!cw || ! (p=MISDN_CALLWEAVER_TECH_PVT(cw)) ) return -1;
	
	chan_misdn_log(1, p? (p->bc? p->bc->port : 0) : 0, "* ANSWER:\n");
	
	if (!p) {
		cw_log(CW_LOG_WARNING, " --> Channel not connected ??\n");
		cw_queue_hangup(cw);
	}

	if (!p->bc) {
		chan_misdn_log(1, 0, " --> Got Answer, but theres no bc obj ??\n");

		cw_queue_hangup(cw);
	}

	if ((var = pbx_builtin_getvar_helper(p->cw, CW_KEYWORD_CRYPT_KEY, "CRYPT_KEY"))) {
		chan_misdn_log(1, p->bc->port, " --> Connection will be BF crypted\n");
		{
			int l = sizeof(p->bc->crypt_key);
			strncpy(p->bc->crypt_key, var->value, l);
			p->bc->crypt_key[l-1] = 0;
		}
	} else {
		chan_misdn_log(3, p->bc->port, " --> Connection is without BF encryption\n");
	}

	if ((var = pbx_builtin_getvar_helper(cw, CW_KEYWORD_MISDN_DIGITAL_TRANS, "MISDN_DIGITAL_TRANS"))) {
		cw_object_put(var);
		chan_misdn_log(1, p->bc->port, " --> Connection is transparent digital\n");
		p->bc->nodsp=1;
		p->bc->hdlc=0;
		p->bc->nojitter=1;
	}

	p->state = MISDN_CONNECTED;
	misdn_lib_echo(p->bc,0);
	stop_indicate(p);

	if ( cw_strlen_zero(p->bc->cad) ) {
		chan_misdn_log(2,p->bc->port," --> empty cad using dad\n");
		cw_copy_string(p->bc->cad,p->bc->dad,sizeof(p->bc->cad));
	}

	misdn_lib_send_event( p->bc, EVENT_CONNECT);
	start_bc_tones(p);
	
	return 0;
}

static int misdn_digit(struct cw_channel *cw, char digit )
{
	struct chan_list *p;
	
	if (!cw || ! (p=MISDN_CALLWEAVER_TECH_PVT(cw))) return -1;

	struct misdn_bchannel *bc=p->bc;
	chan_misdn_log(1, bc?bc->port:0, "* IND : Digit %c\n",digit);
	
	if (!bc) {
		cw_log(CW_LOG_WARNING, " --> !! Got Digit Event withut having bchannel Object\n");
		return -1;
	}
	
	switch (p->state ) {
		case MISDN_CALLING:
		{
			
			char buf[8];
			buf[0]=digit;
			buf[1]=0;
			
			int l = sizeof(bc->infos_pending);
			strncat(bc->infos_pending,buf,l);
			bc->infos_pending[l-1] = 0;
		}
		break;
		case MISDN_CALLING_ACKNOWLEDGE:
		{
			bc->info_dad[0]=digit;
			bc->info_dad[1]=0;
			
			{
				int l = sizeof(bc->dad);
				strncat(bc->dad,bc->info_dad, l - strlen(bc->dad));
				bc->dad[l-1] = 0;
		}
			{
				int l = sizeof(p->cw->exten);
				strncpy(p->cw->exten, bc->dad, l);
				p->cw->exten[l-1] = 0;
			}
			
			misdn_lib_send_event( bc, EVENT_INFORMATION);
		}
		break;
		
		default:
			if ( bc->send_dtmf ) {
				send_digit_to_chan(p,digit);
			}
		break;
	}
	
	return 0;
}


static int misdn_fixup(struct cw_channel *oldcw, struct cw_channel *cw)
{
	struct chan_list *p;
	
	if (!cw || ! (p=MISDN_CALLWEAVER_TECH_PVT(cw) )) return -1;
	
	chan_misdn_log(1, p->bc?p->bc->port:0, "* IND: Got Fixup State:%s L3id:%x\n", misdn_get_ch_state(p), p->l3id);
	
	p->cw = cw ;
	p->state=MISDN_CONNECTED;
  
	return 0;
}



static int misdn_indication(struct cw_channel *cw, int cond)
{
	struct chan_list *p;

  
	if (!cw || ! (p=MISDN_CALLWEAVER_TECH_PVT(cw))) {
		cw_log(CW_LOG_WARNING, "Returnded -1 in misdn_indication\n");
		return -1;
	}
	
	if (!p->bc ) {
		chan_misdn_log(1, 0, "* IND : Indication from %s\n",cw->exten);
		cw_log(CW_LOG_WARNING, "Private Pointer but no bc ?\n");
		return -1;
	}
	
	chan_misdn_log(1, p->bc->port, "* IND : Indication [%d] from %s\n",cond, cw->exten);
	
	switch (cond) {
	case CW_CONTROL_BUSY:
		chan_misdn_log(1, p->bc->port, "* IND :\tbusy\n");
		chan_misdn_log(1, p->bc->port, " --> * SEND: State Busy pid:%d\n",p->bc?p->bc->pid:-1);
		cw_setstate(cw,CW_STATE_BUSY);

		p->bc->out_cause=17;
		if (p->state != MISDN_CONNECTED) {
			start_bc_tones(p);
			misdn_lib_send_event( p->bc, EVENT_DISCONNECT);
		} else {
			chan_misdn_log(-1, p->bc->port, " --> !! Got Busy in Connected State !?! cw:%s\n", cw->name);
		}
		return -1;
		break;
	case CW_CONTROL_RING:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tring pid:%d\n",p->bc?p->bc->pid:-1);
		return -1;
		break;
		
	case CW_CONTROL_RINGING:
		switch (p->state) {
			case MISDN_ALERTING:
				chan_misdn_log(1, p->bc->port, " --> * IND :\tringing pid:%d but I was Ringing before, so ignoreing it\n",p->bc?p->bc->pid:-1);
				break;
			case MISDN_CONNECTED:
				chan_misdn_log(1, p->bc->port, " --> * IND :\tringing pid:%d but Connected, so just send TONE_ALERTING without state changes \n",p->bc?p->bc->pid:-1);
				return -1;
				break;
			default:
				p->state=MISDN_ALERTING;
				chan_misdn_log(1, p->bc->port, " --> * IND :\tringing pid:%d\n",p->bc?p->bc->pid:-1);
				misdn_lib_send_event( p->bc, EVENT_ALERTING);
			
				if (p->other_ch && p->other_ch->bc) {
					if (misdn_inband_avail(p->other_ch->bc)) {
						chan_misdn_log(1,p->bc->port, " --> other End is mISDN and has inband info available\n");
						break;
					}

					if (!p->other_ch->bc->nt) {
						chan_misdn_log(1,p->bc->port, " --> other End is mISDN TE so it has inband info for sure (?)\n");
						break;
					}
				}

				chan_misdn_log(1, p->bc->port, " --> * SEND: State Ring pid:%d\n",p->bc?p->bc->pid:-1);
				cw_setstate(cw,CW_STATE_RINGING);
			
				if ( !p->bc->nt && (p->orginator==ORG_MISDN) && !p->incoming_early_audio ) 
					chan_misdn_log(1,p->bc->port, " --> incoming_early_audio off\n");
				else 
					return -1;
		}
		break;
	case CW_CONTROL_ANSWER:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tanswer pid:%d\n",p->bc?p->bc->pid:-1);
		start_bc_tones(p);
		break;
	case CW_CONTROL_TAKEOFFHOOK:
		chan_misdn_log(1, p->bc->port, " --> *\ttakeoffhook pid:%d\n",p->bc?p->bc->pid:-1);
		return -1;
		break;
	case CW_CONTROL_OFFHOOK:
		chan_misdn_log(1, p->bc->port, " --> *\toffhook pid:%d\n",p->bc?p->bc->pid:-1);
		return -1;
		break; 
	case CW_CONTROL_FLASH:
		chan_misdn_log(1, p->bc->port, " --> *\tflash pid:%d\n",p->bc?p->bc->pid:-1);
		break;
	case CW_CONTROL_PROGRESS:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tprogress pid:%d\n",p->bc?p->bc->pid:-1);
		misdn_lib_send_event( p->bc, EVENT_PROGRESS);
		break;
	case CW_CONTROL_PROCEEDING:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tproceeding pid:%d\n",p->bc?p->bc->pid:-1);
		misdn_lib_send_event( p->bc, EVENT_PROCEEDING);
		break;
	case CW_CONTROL_CONGESTION:
		chan_misdn_log(1, p->bc->port, " --> * IND :\tcongestion pid:%d\n",p->bc?p->bc->pid:-1);

		p->bc->out_cause=42;
		if (p->state != MISDN_CONNECTED) {
			start_bc_tones(p);
			misdn_lib_send_event( p->bc, EVENT_RELEASE);
		} else {
			misdn_lib_send_event( p->bc, EVENT_DISCONNECT);
		}

		if (p->bc->nt) {
			hanguptone_indicate(p);
		}
		break;
	case -1 :
		chan_misdn_log(1, p->bc->port, " --> * IND :\t-1! (stop indication) pid:%d\n",p->bc?p->bc->pid:-1);
		
		stop_indicate(p);

		if (p->state == MISDN_CONNECTED) 
			start_bc_tones(p);

		break;

	case CW_CONTROL_HOLD:
		chan_misdn_log(1, p->bc->port, " --> *\tHOLD pid:%d\n",p->bc?p->bc->pid:-1);
		break;
	case CW_CONTROL_UNHOLD:
		chan_misdn_log(1, p->bc->port, " --> *\tUNHOLD pid:%d\n",p->bc?p->bc->pid:-1);
		break;
	default:
		cw_log(CW_LOG_NOTICE, " --> * Unknown Indication:%d pid:%d\n",cond,p->bc?p->bc->pid:-1);
	}
  
	return 0;
}

static int misdn_hangup(struct cw_channel *cw)
{
	struct chan_list *p;
	struct misdn_bchannel *bc=NULL;
	struct cw_var_t *var;

	if (!cw || ! (p=MISDN_CALLWEAVER_TECH_PVT(cw) ) ) return -1;
	
	cw_log(CW_LOG_DEBUG, "misdn_hangup(%s)\n", cw->name);
	
	if (!p) {
		chan_misdn_log(3, 0, "misdn_hangup called, without chan_list obj.\n");
		return 0 ;
	}
	
	bc=p->bc;

	if (!bc) {
		cw_log(CW_LOG_WARNING,"Hangup with private but no bc ?\n");
		return 0;
	}

	
	MISDN_CALLWEAVER_TECH_PVT(cw)=NULL;
	p->cw=NULL;

	bc=p->bc;
	
	if (cw->_state == CW_STATE_RESERVED) {
		/* between request and call */
		MISDN_CALLWEAVER_TECH_PVT(cw)=NULL;
		
		cl_dequeue_chan(&cl_te, p);
		free(p);
		
		if (bc)
			misdn_lib_release(bc);
		
		return 0;
	}

	p->need_hangup=0;
	p->need_queue_hangup=0;


	if (!p->bc->nt) 
		stop_bc_tones(p);

	bc->out_cause = (cw->hangupcause ? cw->hangupcause : 16);

	if ( (var = pbx_builtin_getvar_helper(cw, CW_KEYWORD_HANGUPCAUSE, "HANGUPCAUSE")) ||
	     (var = pbx_builtin_getvar_helper(cw, CW_KEYWORD_PRI_CAUSE, "PRI_CAUSE"))) {
		int tmpcause = atoi(var->value);
		bc->out_cause = tmpcause ? tmpcause : 16;
		cw_object_put(var);
	}

	chan_misdn_log(1, bc->port, "* IND : HANGUP\tpid:%d ctx:%s dad:%s oad:%s State:%s\n",p->bc?p->bc->pid:-1, cw->context, cw->exten, CW_CID_P(cw), misdn_get_ch_state(p));
	chan_misdn_log(2, bc->port, " --> l3id:%x\n",p->l3id);
	chan_misdn_log(1, bc->port, " --> cause:%d\n",bc->cause);
	chan_misdn_log(1, bc->port, " --> out_cause:%d\n",bc->out_cause);
	chan_misdn_log(1, bc->port, " --> state:%s\n", misdn_get_ch_state(p));
	
	switch (p->state) {
	case MISDN_CALLING:
		p->state=MISDN_CLEANING;
		misdn_lib_send_event( bc, EVENT_RELEASE_COMPLETE);
		break;
	case MISDN_HOLDED:
	case MISDN_DIALING:
		start_bc_tones(p);
		hanguptone_indicate(p);

		if (bc->need_disconnect)
			misdn_lib_send_event( bc, EVENT_DISCONNECT);
		break;

	case MISDN_CALLING_ACKNOWLEDGE:
		start_bc_tones(p);
		hanguptone_indicate(p);

		if (bc->need_disconnect)
			misdn_lib_send_event( bc, EVENT_DISCONNECT);
		break;

	case MISDN_ALERTING:
	case MISDN_PROGRESS:
	case MISDN_PROCEEDING:
		if (p->orginator != ORG_CW)
			hanguptone_indicate(p);

		/*p->state=MISDN_CLEANING;*/
		if (bc->need_disconnect)
			misdn_lib_send_event( bc, EVENT_DISCONNECT);
		break;
	case MISDN_CONNECTED:
	case MISDN_PRECONNECTED:
		/*  Alerting or Disconect */
		if (p->bc->nt) {
			start_bc_tones(p);
			hanguptone_indicate(p);
			p->bc->progress_indicator=8;
		}
		if (bc->need_disconnect)
			misdn_lib_send_event( bc, EVENT_DISCONNECT);

		/*p->state=MISDN_CLEANING;*/
		break;
	case MISDN_DISCONNECTED:
		misdn_lib_send_event( bc, EVENT_RELEASE);
		p->state=MISDN_CLEANING; /* MISDN_HUNGUP_FROM_CW; */
		break;

	case MISDN_RELEASED:
	case MISDN_CLEANING:
		p->state=MISDN_CLEANING;
		break;

	case MISDN_BUSY:
		break;

	case MISDN_HOLD_DISCONNECT:
		/* need to send release here */
		chan_misdn_log(1, bc->port, " --> cause %d\n",bc->cause);
		chan_misdn_log(1, bc->port, " --> out_cause %d\n",bc->out_cause);

		bc->out_cause=-1;
		misdn_lib_send_event(bc,EVENT_RELEASE);
		p->state=MISDN_CLEANING;
		break;
	default:
		if (bc->nt) {
			bc->out_cause=-1;
			misdn_lib_send_event(bc, EVENT_RELEASE);
			p->state=MISDN_CLEANING; 
		} else {
			if (bc->need_disconnect)
				misdn_lib_send_event(bc, EVENT_DISCONNECT);
		}
	}

	p->state=MISDN_CLEANING;

	chan_misdn_log(1, bc->port, "Channel: %s hanguped new state:%s\n",cw->name,misdn_get_ch_state(p));

	return 0;
}

static struct cw_frame  *misdn_read(struct cw_channel *cw)
{
	struct chan_list *tmp;
	int len;
	
	if (!cw) {
		chan_misdn_log(1,0,"misdn_read called without cw\n");
		return NULL;
	}
	if (! (tmp=MISDN_CALLWEAVER_TECH_PVT(cw)) ) {
		chan_misdn_log(1,0,"misdn_read called without cw->pvt\n");
		return NULL;
	}
	if (!tmp->bc) {
		chan_misdn_log(1,0,"misdn_read called without bc\n");
		return NULL;
	}

	/* Make sure we have a complete 20ms (160byte) frame */       
  	len=read(tmp->pipe[0],tmp->framedata + tmp->framepos, 
		 160 - tmp->framepos);
	tmp->framepos += len;
	if (len<=0) {
		/* we hangup here, since our pipe is closed */
		chan_misdn_log(2,tmp->bc->port,"misdn_read: Pipe closed, hanging up\n");
		return NULL;
	} else if (tmp->framepos < 160) {
		/* Not a complete frame, so we send a null-frame */
		return &cw_null_frame;
	}

	/* We have got a complete frame 
	 * tmp->framepos now has the length of the frame */
	tmp->frame.frametype  = CW_FRAME_VOICE;
	tmp->frame.subclass = CW_FORMAT_ALAW;
	tmp->frame.datalen = tmp->framepos;
	tmp->frame.samples = tmp->framepos;
	tmp->frame.mallocd = 0;
	tmp->frame.offset = 0;
	tmp->frame.data = tmp->framedata;

	tmp->framepos = 0; 

	if (tmp->faxdetect && !tmp->faxhandled) {
		if (tmp->faxdetect_timeout) {
			if (cw_tvzero(tmp->faxdetect_tv)) {
				tmp->faxdetect_tv = cw_tvnow();
				chan_misdn_log(2,tmp->bc->port,"faxdetect: starting detection with timeout: %ds ...\n", tmp->faxdetect_timeout);
				return process_cw_dsp(tmp, &tmp->frame);
			} else {
				struct timeval tv_now = cw_tvnow();
				int diff = cw_tvdiff_ms(tv_now, tmp->faxdetect_tv);
				if (diff <= (tmp->faxdetect_timeout * 1000)) {
					chan_misdn_log(5,tmp->bc->port,"faxdetect: detecting ...\n");
					return process_cw_dsp(tmp, &tmp->frame);
				} else {
					chan_misdn_log(2,tmp->bc->port,"faxdetect: stopping detection (time ran out) ...\n");
					tmp->faxdetect = 0;
					return &tmp->frame;
				}
			}
		} else {
			chan_misdn_log(5,tmp->bc->port,"faxdetect: detecting ... (no timeout)\n");
			return process_cw_dsp(tmp, &tmp->frame);
		}
	} else {
		if (tmp->cw_dsp)
			return process_cw_dsp(tmp, &tmp->frame);
		else
			return &tmp->frame;
	}
}


static int misdn_write(struct cw_channel *cw, struct cw_frame *frame)
{
	struct chan_list *ch;
	int i  = 0;
	
	if (!cw || ! (ch=MISDN_CALLWEAVER_TECH_PVT(cw)) ) return -1;
	
	if (!ch->bc ) {
		cw_log(CW_LOG_WARNING, "private but no bc\n");
		return -1;
	}

	switch (frame->frametype) {
		case CW_FRAME_CONTROL:
			switch (frame->subclass) {
				case CW_CONTROL_OPTION: {
					int *arg = (int *)frame->data;
					switch (arg[0]) {
						case CW_OPTION_ECHOCANCEL:
							ch->bc->ec_enable = arg[1];
							chan_misdn_log(1, ch->bc->port, " echo cancellation set to %d\n", ch->bc->ec_enable);
							isdn_lib_update_ec(ch->bc);
							return 0;
					}
				}
			}
			break;

		case CW_FRAME_TEXT:
			cw_copy_string(ch->bc->display, frame->data, sizeof(ch->bc->display));
			misdn_lib_send_event(ch->bc, EVENT_INFORMATION);
			return 0;
	}

	if (ch->state == MISDN_HOLDED) {
		chan_misdn_log(8, ch->bc->port, "misdn_write: Returning because holded\n");
		return 0;
	}
	
	if (ch->notxtone) {
		chan_misdn_log(9, ch->bc->port, "misdn_write: Returning because notxone\n");
		return 0;
	}


	if ( !frame->subclass) {
		chan_misdn_log(4, ch->bc->port, "misdn_write: * prods us\n");
		return 0;
	}
	
	if ( !(frame->subclass & prefformat)) {
		
		chan_misdn_log(-1, ch->bc->port, "Got Unsupported Frame with Format:%d\n", frame->subclass);
		return 0;
	}
	

	if ( !frame->samples ) {
		chan_misdn_log(4, ch->bc->port, "misdn_write: zero write\n");
		return 0;
	}

	if ( ! ch->bc->addr ) {
		chan_misdn_log(8, ch->bc->port, "misdn_write: no addr for bc dropping:%d\n", frame->samples);
		return 0;
	}
	
#ifdef MISDN_DEBUG
	{
		int i, max=5>frame->samples?frame->samples:5;
		
		printf("write2mISDN %p %d bytes: ", p, frame->samples);
		
		for (i=0; i<  max ; i++) printf("%2.2x ",((char *) frame->data)[i]);
		printf ("\n");
	}
#endif


	switch (ch->bc->bc_state) {
		case BCHAN_ACTIVATED:
		case BCHAN_BRIDGED:
			break;
		default:
		if (!ch->dropped_frame_cnt)
			chan_misdn_log(5, ch->bc->port, "BC not active (nor bridged) droping: %d frames addr:%x exten:%s cid:%s ch->state:%s bc_state:%d\n",frame->samples,ch->bc->addr, cw->exten, cw->cid.cid_num,misdn_get_ch_state( ch), ch->bc->bc_state);
		
		ch->dropped_frame_cnt++;
		if (ch->dropped_frame_cnt > 100) {
			ch->dropped_frame_cnt=0;
			chan_misdn_log(5, ch->bc->port, "BC not active (nor bridged) droping: %d frames addr:%x  dropped > 100 frames!\n",frame->samples,ch->bc->addr);

		}

		return 0;
	}

	chan_misdn_log(9, ch->bc->port, "Sending :%d bytes 2 MISDN\n",frame->samples);
	if ( !ch->bc->nojitter && misdn_cap_is_speech(ch->bc->capability) ) {
		/* Buffered Transmit (triggert by read from isdn side)*/
		if (misdn_jb_fill(ch->jb,frame->data,frame->samples) < 0) {
			if (ch->bc->active)
				cb_log(0,ch->bc->port,"Misdn Jitterbuffer Overflow.\n");
		}
		
	} else {
		/*transmit without jitterbuffer*/
		i=misdn_lib_tx2misdn_frm(ch->bc, frame->data, frame->samples);
	}
	
	return 0;
}




static enum cw_bridge_result  misdn_bridge (struct cw_channel *c0,
				      struct cw_channel *c1, int flags,
				      struct cw_frame **fo,
				      struct cw_channel **rc,
				      int timeoutms)

{
	struct chan_list *ch1,*ch2;
	struct cw_channel *carr[2], *who;
	int to=-1;
	struct cw_frame *f;
  
	ch1=get_chan_by_cw(c0);
	ch2=get_chan_by_cw(c1);

	carr[0]=c0;
	carr[1]=c1;
  
  
	if (ch1 && ch2 ) ;
	else
		return -1;
  

	int bridging;
	misdn_cfg_get( 0, MISDN_GEN_BRIDGING, &bridging, sizeof(int));
	if (bridging) {
		int ecwb, ec;
		misdn_cfg_get( ch1->bc->port, MISDN_CFG_ECHOCANCELWHENBRIDGED, &ecwb, sizeof(int));
		misdn_cfg_get( ch1->bc->port, MISDN_CFG_ECHOCANCEL, &ec, sizeof(int));
		if ( !ecwb && ec ) {
			chan_misdn_log(2, ch1->bc->port, "Disabling Echo Cancellor when Bridged\n");
			ch1->bc->ec_enable=0;
			manager_ec_disable(ch1->bc);
		}
		misdn_cfg_get( ch2->bc->port, MISDN_CFG_ECHOCANCELWHENBRIDGED, &ecwb, sizeof(int));
		misdn_cfg_get( ch2->bc->port, MISDN_CFG_ECHOCANCEL, &ec, sizeof(int));
		if ( !ecwb && ec) {
			chan_misdn_log(2, ch2->bc->port, "Disabling Echo Cancellor when Bridged\n");
			ch2->bc->ec_enable=0;
			manager_ec_disable(ch2->bc); 
		}
		
		/* trying to make a mISDN_dsp conference */
		chan_misdn_log(1, ch1->bc->port, "I SEND: Making conference with Number:%d\n", ch1->bc->pid +1);
		
		misdn_lib_bridge(ch1->bc,ch2->bc);
	}
	
	chan_misdn_log(1, ch1->bc->port, "* Making Native Bridge between %s and %s\n", ch1->bc->oad, ch2->bc->oad);


	if (! (flags&CW_BRIDGE_DTMF_CHANNEL_0) )
		ch1->ignore_dtmf=1;
	
	if (! (flags&CW_BRIDGE_DTMF_CHANNEL_1) )
		ch2->ignore_dtmf=1;
	
	
	while(1) {
		to=-1;
		who = cw_waitfor_n(carr, 2, &to);

		if (!who) {
			cw_log(CW_LOG_NOTICE,"misdn_bridge: empty read, breaking out\n");
			break;
		}
		f = cw_read(who);
    
		if (!f || f->frametype == CW_FRAME_CONTROL) {
			/* got hangup .. */

			if (!f) 
				chan_misdn_log(1,ch1->bc->port,"Read Null Frame\n");
			else
				chan_misdn_log(1,ch1->bc->port,"Read Frame Controll class:%d\n",f->subclass);
			
			*fo=f;
			*rc=who;
      
			break;
		}
		
		if ( f->frametype == CW_FRAME_DTMF ) {
			chan_misdn_log(1,0,"Read DTMF %d from %s\n",f->subclass, who->exten);

			*fo=f;
			*rc=who;
			break;
		}
		
		
		if (who == c0) {
			cw_write(c1, &f);
		}
		else {
			cw_write(c0, &f);
		}
		cw_fr_free(f);
	}
	
	chan_misdn_log(1, ch1->bc->port, "I SEND: Splitting conference with Number:%d\n", ch1->bc->pid +1);
	
	misdn_lib_split_bridge(ch1->bc,ch2->bc);
	
	
	return CW_BRIDGE_COMPLETE;
}

/** CW INDICATIONS END **/

static int dialtone_indicate(struct chan_list *cl)
{
	const struct tone_zone_sound *ts= NULL;
	struct cw_channel *cw=cl->cw;


	int nd=0;
	misdn_cfg_get( cl->bc->port, MISDN_CFG_NODIALTONE, &nd, sizeof(nd));

	if (nd) {
		chan_misdn_log(1,cl->bc->port,"Not sending Dialtone, because config wants it\n");
		return 0;
	}
	
	chan_misdn_log(3,cl->bc->port," --> Dial\n");
	ts=cw_get_indication_tone(cw->zone,"dial");
	cl->ts=ts;	
	
	if (ts) {
		cl->notxtone=0;
		cl->norxtone=0;
		cw_playtones_start(cw,0, ts->data, 0);
	}

	return 0;
}

static int hanguptone_indicate(struct chan_list *cl)
{
	misdn_lib_send_tone(cl->bc,TONE_HANGUP);
	return 0;
}

static int stop_indicate(struct chan_list *cl)
{
	struct cw_channel *cw=cl->cw;
	chan_misdn_log(3,cl->bc->port," --> None\n");
	cw_playtones_stop(cw);
	/*cw_deactivate_generator(cw);*/
	
	return 0;
}


static int start_bc_tones(struct chan_list* cl)
{
	cl->notxtone=0;
	cl->norxtone=0;
	return 0;
}

static int stop_bc_tones(struct chan_list *cl)
{
	if (!cl) return -1;

	cl->notxtone=1;
	cl->norxtone=1;
	
	return 0;
}


static struct chan_list *init_chan_list(int orig)
{
	struct chan_list *cl=malloc(sizeof(struct chan_list));
	
	if (!cl) {
		chan_misdn_log(-1, 0, "misdn_request: malloc failed!");
		return NULL;
	}
	
	memset(cl,0,sizeof(struct chan_list));

	cl->orginator=orig;
	cl->need_queue_hangup=1;
	cl->need_hangup=1;
	cl->need_busy=1;
	
	return cl;
	
}

static struct cw_channel *misdn_request(const char *type, int format, void *data, int *cause)

{
	struct cw_channel *tmp = NULL;
	char group[BUFFERSIZE+1]="";
	char buf[128];
	char buf2[128], *ext=NULL, *port_str;
	char *tokb=NULL, *p=NULL;
	int channel=0, port=0;
	struct misdn_bchannel *newbc = NULL;
	
	struct chan_list *cl=init_chan_list(ORG_CW);
	
	sprintf(buf,"%s/%s",misdn_type,(char *) data);
	cw_copy_string(buf2,data, 128);
	
	port_str=strtok_r(buf2,"/", &tokb);

	ext=strtok_r(NULL,"/", &tokb);

	if (port_str) {
		if (port_str[0]=='g' && port_str[1]==':' ) {
			/* We make a group call lets checkout which ports are in my group */
			port_str += 2;
			strncpy(group, port_str, BUFFERSIZE);
			group[127] = 0;
			chan_misdn_log(2, 0, " --> Group Call group: %s\n",group);
		} 
		else if ((p = strchr(port_str, ':'))) {
			/* we have a preselected channel */
			*p = 0;
			channel = atoi(++p);
			port = atoi(port_str);
			chan_misdn_log(2, port, " --> Call on preselected Channel (%d).\n", channel);
		}
		else {
			port = atoi(port_str);
		}
		
		
	} else {
		cw_log(CW_LOG_WARNING, " --> ! IND : CALL dad:%s WITHOUT PORT/Group, check extension.conf\n",ext);
		return NULL;
	}

	if (!cw_strlen_zero(group)) {
	
		char cfg_group[BUFFERSIZE+1];
		struct robin_list *rr = NULL;

		if (misdn_cfg_is_group_method(group, METHOD_ROUND_ROBIN)) {
			chan_misdn_log(4, port, " --> STARTING ROUND ROBIN...");
			rr = get_robin_position(group);
		}
		
		if (rr) {
			int robin_channel = rr->channel;
			int port_start;
			int next_chan = 1;

			do {
				port_start = 0;
				for (port = misdn_cfg_get_next_port_spin(rr->port); port > 0 && port != port_start;
					 port = misdn_cfg_get_next_port_spin(port)) {

					if (!port_start)
						port_start = port;

					if (port >= port_start)
						next_chan = 1;
					
					if (port <= port_start && next_chan) {
						int maxbchans=misdn_lib_get_maxchans(port);
						if (++robin_channel >= maxbchans) {
							robin_channel = 1;
						}
						next_chan = 0;
					}

					misdn_cfg_get(port, MISDN_CFG_GROUPNAME, cfg_group, BUFFERSIZE);
					
					if (!strcasecmp(cfg_group, group)) {
						int port_up;
						int check;
						misdn_cfg_get(port, MISDN_CFG_PMP_L1_CHECK, &check, sizeof(int));
						port_up = misdn_lib_port_up(port, check);

						if (check && !port_up) 
							chan_misdn_log(1,port,"L1 is not Up on this Port\n");
						
						if (check && port_up<0) {
							cw_log(CW_LOG_WARNING,"This port (%d) is blocked\n", port);
						}
						
						
						if ( port_up>0 )	{
							newbc = misdn_lib_get_free_bc(port, robin_channel);
							if (newbc) {
								chan_misdn_log(4, port, " Success! Found port:%d channel:%d\n", newbc->port, newbc->channel);
								if (port_up)
									chan_misdn_log(4, port, "portup:%d\n",  port_up);
								rr->port = newbc->port;
								rr->channel = newbc->channel;
								break;
							}
						}
					}
				}
			} while (!newbc && robin_channel != rr->channel);
			
			if (!newbc)
				chan_misdn_log(-1, port, " Failed! No free channel in group %d!", group);
		}
		
		else {		
			for (port=misdn_cfg_get_next_port(0); port > 0;
				 port=misdn_cfg_get_next_port(port)) {
				
				misdn_cfg_get( port, MISDN_CFG_GROUPNAME, cfg_group, BUFFERSIZE);

				chan_misdn_log(3,port, "Group [%s] Port [%d]\n", group, port);
				if (!strcasecmp(cfg_group, group)) {
					int port_up;
					int check;
					misdn_cfg_get(port, MISDN_CFG_PMP_L1_CHECK, &check, sizeof(int));
					port_up = misdn_lib_port_up(port, check);
					
					chan_misdn_log(4, port, "portup:%d\n", port_up);
					
					if ( port_up>0 ) {
						newbc = misdn_lib_get_free_bc(port, 0);
						if (newbc)
							break;
					}
				}
			}
		}
		
	} else {
		if (channel)
			chan_misdn_log(1, port," --> preselected_channel: %d\n",channel);
		newbc = misdn_lib_get_free_bc(port, channel);
	}
	
	if (!newbc) {
		chan_misdn_log(-1, 0, "Could not create channel on port:%d with extensions:%s\n",port,ext);
		return NULL;
	}

	/* create cw_channel and link all the objects together */
	cl->bc=newbc;
	
	tmp = misdn_new(cl, CW_STATE_RESERVED, ext, NULL, format, port, channel);
	cl->cw=tmp;
	
	/* register chan in local list */
	cl_queue_chan(&cl_te, cl) ;
	
	/* fill in the config into the objects */
	read_config(cl, ORG_CW);

	/* important */
	cl->need_hangup=0;
	
	return tmp;
}


static struct cw_channel_tech misdn_tech = {
	.type="mISDN",
	.description="Channel driver for mISDN Support (Bri/Pri)",
	.capabilities= CW_FORMAT_ALAW ,
	.requester=misdn_request,
	.send_digit=misdn_digit,
	.call=misdn_call,
	.bridge=misdn_bridge, 
	.hangup=misdn_hangup,
	.answer=misdn_answer,
	.read=misdn_read,
	.write=misdn_write,
	.indicate=misdn_indication,
	.fixup=misdn_fixup,
	.properties=0
};

static struct cw_channel_tech misdn_tech_wo_bridge = {
	.type="mISDN",
	.description="Channel driver for mISDN Support (Bri/Pri)",
	.capabilities=CW_FORMAT_ALAW ,
	.requester=misdn_request,
	.send_digit=misdn_digit,
	.call=misdn_call,
	.hangup=misdn_hangup,
	.answer=misdn_answer,
	.read=misdn_read,
	.write=misdn_write,
	.indicate=misdn_indication,
	.fixup=misdn_fixup,
	.properties=0
};


static int glob_channel=0;

static int update_name(int port, int c)
{
	int chan_offset=0;
	int tmp_port = misdn_cfg_get_next_port(0);
	for (; tmp_port > 0; tmp_port=misdn_cfg_get_next_port(tmp_port)) {
		if (tmp_port == port) break;
		chan_offset+=misdn_lib_port_is_pri(tmp_port)?30:2;
	}
	
	if (c<0) c=0;

	return chan_offset + c;
}

static struct cw_channel *misdn_new(struct chan_list *chlist, int state,  char *exten, char *callerid, int format, int port, int c)
{
	struct cw_channel *tmp;

	tmp = cw_channel_alloc(1, "%s/%d-u%d", misdn_type, update_name(port, c), ++glob_channel);

	if (tmp) {
		chan_misdn_log(2, 0, " --> * NEW CHANNEL dad:%s oad:%s\n",exten,callerid);

		tmp->type = misdn_type;

		tmp->nativeformats = prefformat;

		tmp->readformat = format;
		tmp->rawreadformat = format;
		tmp->writeformat = format;
		tmp->rawwriteformat = format;
    
		tmp->tech_pvt = chlist;
		
		int bridging;
		misdn_cfg_get( 0, MISDN_GEN_BRIDGING, &bridging, sizeof(int));
		if (bridging)
			tmp->tech = &misdn_tech;
		else
			tmp->tech = &misdn_tech_wo_bridge;
		
		tmp->writeformat = format;
		tmp->readformat = format;
		tmp->priority=1;
		
		if (exten) 
			cw_copy_string(tmp->exten, exten,  sizeof(tmp->exten));
		else
			chan_misdn_log(1,0,"misdn_new: no exten given.\n");
		
		if (callerid) {
			char *cid_name, *cid_num;
      
			cw_callerid_parse(callerid, &cid_name, &cid_num);

			if (!cw_strlen_zero(cid_num)) {
				tmp->cid.cid_num = strdup(cid_num);
				tmp->cid.cid_ani = strdup(cid_num);
			}
			if (!cw_strlen_zero(cid_name))
				tmp->cid.cid_name = strdup(cid_name);
		}

		{
			if (pipe(chlist->pipe)<0)
				perror("Pipe failed\n");
			
			tmp->fds[0]=chlist->pipe[0];
			
		}
		
		cw_setstate(tmp, state);
	} else {
		chan_misdn_log(-1,0,"Unable to allocate channel structure\n");
	}
	
	return tmp;
}


static struct cw_frame *process_cw_dsp(struct chan_list *tmp, struct cw_frame *frame)
{
	struct cw_frame *f,*f2;

	if (tmp->trans) {
		f2 = cw_translate(tmp->trans, frame, 0);
		f = cw_dsp_process(tmp->cw, tmp->dsp, f2);
	} else {
		chan_misdn_log(0, tmp->bc->port, "No T-Path found\n");
		return NULL;
	}

	if (!f || (f->frametype != CW_FRAME_DTMF))
		return frame;

	cw_log(CW_LOG_DEBUG, "Detected inband DTMF digit: %c", f->subclass);

	if (tmp->faxdetect && (f->subclass == 'f')) {
		/* Fax tone -- Handle and return NULL */
		if (!tmp->faxhandled) {
			struct cw_channel *cw = tmp->cw;
			tmp->faxhandled++;
			chan_misdn_log(0, tmp->bc->port, "Fax detected, preparing %s for fax transfer.\n", cw->name);
			isdn_lib_stop_dtmf(tmp->bc);
			switch (tmp->faxdetect) {
			case 1:
				if (strcmp(cw->exten, "fax")) {
					char *context;
					char context_tmp[BUFFERSIZE];
					misdn_cfg_get(tmp->bc->port, MISDN_CFG_FAXDETECT_CONTEXT, &context_tmp, sizeof(context_tmp));
					context = cw_strlen_zero(context_tmp) ? (cw_strlen_zero(cw->proc_context) ? cw->context : cw->proc_context) : context_tmp;
					if (cw_exists_extension(cw, context, "fax", 1, CW_CID_P(cw))) {
						if (option_verbose > 2)
							cw_verbose(VERBOSE_PREFIX_3 "Redirecting %s to fax extension (context:%s)\n", cw->name, context);
						/* Save the DID/DNIS when we transfer the fax call to a "fax" extension */
						pbx_builtin_setvar_helper(cw,"FAXEXTEN",cw->exten);
						if (cw_async_goto_n(cw, context, "fax", 1))
							cw_log(CW_LOG_WARNING, "Failed to async goto '%s' into fax of '%s'\n", cw->name, context);
					} else
						cw_log(CW_LOG_NOTICE, "Fax detected, but no fax extension ctx:%s exten:%s\n", context, cw->exten);
				} else 
					cw_log(CW_LOG_DEBUG, "Already in a fax extension, not redirecting\n");
				break;
			case 2:
				cw_verbose(VERBOSE_PREFIX_3 "Not redirecting %s to fax extension, nojump is set.\n", cw->name);
				break;
			}
		} else
			cw_log(CW_LOG_DEBUG, "Fax already handled\n");
	}
	
	if (tmp->cw_dsp && (f->subclass != 'f')) {
		chan_misdn_log(2, tmp->bc->port, " --> * SEND: DTMF (CW_DSP) :%c\n", f->subclass);
	}

	return frame;
}


static struct chan_list *find_chan_by_bc(struct chan_list *list, struct misdn_bchannel *bc)
{
	struct chan_list *help=list;
	for (;help; help=help->next) {
		if (help->bc == bc) return help;
	}
  
	chan_misdn_log(6, bc->port, "$$$ find_chan: No channel found for oad:%s dad:%s\n",bc->oad,bc->dad);
  
	return NULL;
}

static struct chan_list *find_chan_by_pid(struct chan_list *list, int pid)
{
	struct chan_list *help=list;
	for (;help; help=help->next) {
		if (help->bc->pid == pid) return help;
	}
  
	chan_misdn_log(6, 0, "$$$ find_chan: No channel found for pid:%d\n",pid);
  
	return NULL;
}

static struct chan_list *find_holded(struct chan_list *list, struct misdn_bchannel *bc)
{
	struct chan_list *help=list;
	
	chan_misdn_log(6, bc->port, "$$$ find_holded: channel:%d oad:%s dad:%s\n",bc->channel, bc->oad,bc->dad);
	for (;help; help=help->next) {
		chan_misdn_log(4, bc->port, "$$$ find_holded: --> holded:%d channel:%d\n",help->bc->holded, help->bc->channel);
		if (help->bc->port == bc->port
		    && help->bc->holded ) return help;
	}
	
	chan_misdn_log(6, bc->port, "$$$ find_chan: No channel found for oad:%s dad:%s\n",bc->oad,bc->dad);
  
	return NULL;
}

static void cl_queue_chan(struct chan_list **list, struct chan_list *chan)
{
	chan_misdn_log(4, chan->bc? chan->bc->port : 0, "* Queuing chan %p\n",chan);
  
	cw_mutex_lock(&cl_te_lock);
	if (!*list) {
		*list = chan;
	} else {
		struct chan_list *help=*list;
		for (;help->next; help=help->next); 
		help->next=chan;
	}
	chan->next=NULL;
	cw_mutex_unlock(&cl_te_lock);
}

static void cl_dequeue_chan(struct chan_list **list, struct chan_list *chan) 
{
	if (chan->dsp) 
		cw_dsp_free(chan->dsp);
	if (chan->trans)
		cw_translator_free_path(chan->trans);

	

	cw_mutex_lock(&cl_te_lock);
	if (!*list) {
		cw_mutex_unlock(&cl_te_lock);
		return;
	}
  
	if (*list == chan) {
		*list=(*list)->next;
		cw_mutex_unlock(&cl_te_lock);
		return ;
	}
  
	{
		struct chan_list *help=*list;
		for (;help->next; help=help->next) {
			if (help->next == chan) {
				help->next=help->next->next;
				cw_mutex_unlock(&cl_te_lock);
				return;
			}
		}
	}
	
	cw_mutex_unlock(&cl_te_lock);
}

/** Channel Queue End **/


static int pbx_start_chan(struct chan_list *ch)
{
	int ret=cw_pbx_start(ch->cw);	

	if (ret>=0) 
		ch->need_hangup=0;
	else
		ch->need_hangup=1;

	return ret;
}

static void hangup_chan(struct chan_list *ch)
{
	if (!ch) {
		cb_log(1,0,"Cannot hangup chan, no ch\n");
		return;
	}

	cb_log(1,ch->bc?ch->bc->port:0,"hangup_chan\n");

	if (ch->need_hangup) 
	{
		cb_log(1,ch->bc->port,"-> hangup\n");
		send_cause2cw(ch->cw,ch->bc,ch);
		ch->need_hangup=0;
		ch->need_queue_hangup=0;
		if (ch->cw)
			cw_hangup(ch->cw);
		return;
	}

	if (!ch->need_queue_hangup) {
		cb_log(1,ch->bc->port,"No need to queue hangup\n");
	}

	ch->need_queue_hangup=0;
	if (ch->cw) {
		send_cause2cw(ch->cw,ch->bc,ch);

		if (ch->cw)
			cw_queue_hangup(ch->cw);
		cb_log(1,ch->bc->port,"-> queue_hangup\n");
	} else {
		cb_log(1,ch->bc->port,"Cannot hangup chan, no cw\n");
	}
}

/** Isdn asks us to release channel, pendant to misdn_hangup **/
static void release_chan(struct misdn_bchannel *bc) {
	struct cw_channel *cw=NULL;
	{
		struct chan_list *ch=find_chan_by_bc(cl_te, bc);
		if (!ch)  {
			chan_misdn_log(0, bc->port, "release_chan: Ch not found!\n");
			return;
		}
		
		if (ch->cw) {
			cw=ch->cw;
		} 
		
		chan_misdn_log(1, bc->port, "release_chan: bc with l3id: %x\n",bc->l3_id);
		
		/*releaseing jitterbuffer*/
		if (ch->jb ) {
			misdn_jb_destroy(ch->jb);
			misdn_jb_destroy(ch->jb_rx);
			ch->jb=NULL;
			ch->jb_rx=NULL;
		} else {
			if (!bc->nojitter)
				chan_misdn_log(5,bc->port,"Jitterbuffer already destroyed.\n");
		}

		if (ch->overlap_dial) {
			if (ch->overlap_dial_task != -1) {
				misdn_tasks_remove(ch->overlap_dial_task);
				ch->overlap_dial_task = -1;
			}
			cw_mutex_destroy(&ch->overlap_tv_lock);
		}

		if (ch->orginator == ORG_CW) {
			misdn_out_calls[bc->port]--;
		} else {
			misdn_in_calls[bc->port]--;
		}
		
		if (ch) {
			
			close(ch->pipe[0]);
			close(ch->pipe[1]);

			
			if (cw && MISDN_CALLWEAVER_TECH_PVT(cw)) {
				chan_misdn_log(1, bc->port, "* RELEASING CHANNEL pid:%d ctx:%s dad:%s oad:%s state: %s\n",bc?bc->pid:-1, cw->context, cw->exten,CW_CID_P(cw),misdn_get_ch_state(ch));
				chan_misdn_log(3, bc->port, " --> * State Down\n");
				MISDN_CALLWEAVER_TECH_PVT(cw)=NULL;
				
      
				if (cw->_state != CW_STATE_RESERVED) {
					chan_misdn_log(3, bc->port, " --> Setting CW State to down\n");
					cw_setstate(cw, CW_STATE_DOWN);
				}
			}
				
			ch->state=MISDN_CLEANING;
			cl_dequeue_chan(&cl_te, ch);
			
			free(ch);
		} else {
			/* chan is already cleaned, so exiting  */
		}
	}
}
/*** release end **/

static void misdn_transfer_bc(struct chan_list *tmp_ch, struct chan_list *holded_chan)
{
	struct cw_channel *bchan;

	chan_misdn_log(4, 0, "TRANSFERING %s to %s\n", holded_chan->cw->name, tmp_ch->cw->name);
	
	tmp_ch->state = MISDN_HOLD_DISCONNECT;
  
	if ((bchan = cw_bridged_channel(holded_chan->cw))) {
		cw_moh_stop(bchan);
		holded_chan->state = MISDN_CONNECTED;
		misdn_lib_transfer(holded_chan->bc ? holded_chan->bc : holded_chan->holded_bc);
		cw_channel_masquerade(holded_chan->cw, bchan);

		cw_object_put(bchan);
	}
}


static void do_immediate_setup(struct misdn_bchannel *bc,struct chan_list *ch , struct cw_channel *cw)
{
	char predial[256]="";
	char *p = predial;
  
	struct cw_frame fr;
  
	strncpy(predial, cw->exten, sizeof(predial) -1 );
  
	ch->state=MISDN_DIALING;

	if (bc->nt) {
		int ret; 
		ret = misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE );
	} else {
		int ret;
		if ( misdn_lib_is_ptp(bc->port)) {
			ret = misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE );
		} else {
			ret = misdn_lib_send_event(bc, EVENT_PROCEEDING );
		}
	}

	if ( !bc->nt && (ch->orginator==ORG_MISDN) && !ch->incoming_early_audio ) 
		chan_misdn_log(1,bc->port, " --> incoming_early_audio off\n");
	 else  
		dialtone_indicate(ch);
  
	chan_misdn_log(1, bc->port, "* Starting Opbx ctx:%s dad:%s oad:%s with 's' extension\n", cw->context, cw->exten, CW_CID_P(cw));
  
	strncpy(cw->exten,"s", 2);
  
	if (pbx_start_chan(ch)<0) {
		cw=NULL;
		hangup_chan(ch);
		hanguptone_indicate(ch);

		if (bc->nt)
			misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE );
		else
			misdn_lib_send_event(bc, EVENT_DISCONNECT );
	}
  
  
	while (!cw_strlen_zero(p) ) {
        cw_fr_init_ex(&fr, CW_FRAME_DTMF, *p);

		if (ch->cw && MISDN_CALLWEAVER_PVT(ch->cw) && MISDN_CALLWEAVER_TECH_PVT(ch->cw)) {
			cw_queue_frame(ch->cw, &fr);
		}
		p++;
	}
}



static void send_cause2cw(struct cw_channel *cw, struct misdn_bchannel*bc, struct chan_list *ch) {
	if (!cw) {
		chan_misdn_log(1,0,"send_cause2cw: No Opbx\n");
		return;
	}
	if (!bc) {
		chan_misdn_log(1,0,"send_cause2cw: No BC\n");
		return;
	}
	if (!ch) {
		chan_misdn_log(1,0,"send_cause2cw: No Ch\n");
		return;
	}
	
	cw->hangupcause=bc->cause;
	
	switch ( bc->cause) {
		
	case 1: /** Congestion Cases **/
	case 2:
	case 3:
 	case 4:
 	case 22:
 	case 27:
		/*
		 * Not Queueing the Congestion anymore, since we want to hear
		 * the inband message
		 *
		chan_misdn_log(1, bc?bc->port:0, " --> * SEND: Queue Congestion pid:%d\n", bc?bc->pid:-1);
		ch->state=MISDN_BUSY;
		
		cw_queue_control(cw, CW_CONTROL_CONGESTION);
		*/
		break;
		
	case 21:
	case 17: /* user busy */
	
		ch->state=MISDN_BUSY;
			
		if (!ch->need_busy) {
			chan_misdn_log(1,bc?bc->port:0, "Queued busy already\n");
			break;
		}
		
		chan_misdn_log(1,  bc?bc->port:0, " --> * SEND: Queue Busy pid:%d\n", bc?bc->pid:-1);
		
		cw_queue_control(cw, CW_CONTROL_BUSY);
		
		ch->need_busy=0;
		
		break;
	}
}

void import_ch(struct cw_channel *chan, struct misdn_bchannel *bc, struct chan_list *ch)
{
	struct cw_var_t *var;

	if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_MISDN_PID, "MISDN_PID"))) {
		ch->other_pid = atoi(var->value);
		chan_misdn_log(1,bc->port, "IMPORT_PID: importing pid:%s\n", var->value);
		cw_object_put(var);

		if (ch->other_pid > 0) {
			ch->other_ch = find_chan_by_pid(cl_te,ch->other_pid);
			if (ch->other_ch)
				ch->other_ch->other_ch = ch;
		}
	}
}
 
void export_ch(struct cw_channel *chan, struct misdn_bchannel *bc, struct chan_list *ch)
{
	char tmp[32];

	chan_misdn_log(1,bc->port,"EXPORT_PID: pid:%d\n",bc->pid);
	sprintf(tmp,"%d",bc->pid);
	pbx_builtin_setvar_helper(chan,"_MISDN_PID",tmp);
}


int add_in_calls(int port)
{
	int max_in_calls;
	
	misdn_cfg_get( port, MISDN_CFG_MAX_IN, &max_in_calls, sizeof(max_in_calls));

	misdn_in_calls[port]++;

	if (max_in_calls >=0 && max_in_calls<misdn_in_calls[port]) {
		cw_log(CW_LOG_NOTICE,"Marking Incoming Call on port[%d]\n",port);
		return misdn_in_calls[port]-max_in_calls;
	}
	
	return 0;
}

int add_out_calls(int port)
{
	int max_out_calls;
	
	misdn_cfg_get( port, MISDN_CFG_MAX_OUT, &max_out_calls, sizeof(max_out_calls));
	

	if (max_out_calls >=0 && max_out_calls<=misdn_out_calls[port]) {
		cw_log(CW_LOG_NOTICE,"Rejecting Outgoing Call on port[%d]\n",port);
		return (misdn_out_calls[port]+1)-max_out_calls;
	}

	misdn_out_calls[port]++;
	
	return 0;
}



void trigger_read(struct chan_list *ch, char *data, int len)
{
	fd_set wrfs;
	struct timeval tv;
	tv.tv_sec=0;
	tv.tv_usec=0;
	struct misdn_bchannel *bc=ch->bc;
		
	
	FD_ZERO(&wrfs);
	FD_SET(ch->pipe[1],&wrfs);
			
	int t=select(FD_SETSIZE,NULL,&wrfs,NULL,&tv);

	if (!t) {
		chan_misdn_log(9, bc->port, "Select Timed out\n");
		return;
	}
		
	if (t<0) {
		chan_misdn_log(-1, bc->port, "Select Error (err=%s)\n",strerror(errno));
		return;
	}
		
	if (FD_ISSET(ch->pipe[1],&wrfs)) {

#if 0
		chan_misdn_log(9, bc->port, "writing %d bytes 2 callweaver\n",bc->bframe_len);
		int ret=write(ch->pipe[1], bc->bframe, bc->bframe_len);
#endif
		int ret=write(ch->pipe[1], data , len);
			
		if (ret<=0) {
			chan_misdn_log(-1, bc->port, "Write returned <=0 (err=%s)\n",strerror(errno));
		}
	} else {
		chan_misdn_log(1, bc->port, "Wripe Pipe full!\n");
	}
}

/************************************************************/
/*  Receive Events from isdn_lib  here                     */
/************************************************************/
static enum event_response_e
cb_events(enum event_e event, struct misdn_bchannel *bc, void *user_data)
{
	struct chan_list *ch=find_chan_by_bc(cl_te, bc);
	
	if (event != EVENT_BCHAN_DATA) { /*  Debug Only Non-Bchan */
		int debuglevel=1;
	
		if ( event==EVENT_CLEANUP && !user_data)
			debuglevel=5;

		chan_misdn_log(debuglevel, bc->port, "I IND :%s oad:%s dad:%s pid:%d state:%s\n", manager_isdn_get_info(event), bc->oad, bc->dad, bc->pid, ch?misdn_get_ch_state(ch):"none");
		if (debuglevel==1) {
			misdn_lib_log_ies(bc);
			chan_misdn_log(4,bc->port," --> bc_state:%s\n",bc_state2str(bc->bc_state));
		}
	}
	
	if (!ch) {
		switch(event) {
			case EVENT_SETUP:
			case EVENT_FACILITY:
			case EVENT_DISCONNECT:
			case EVENT_PORT_ALARM:
			case EVENT_RETRIEVE:
			case EVENT_NEW_BC:
				/*We still handle then, even without calls*/
				break;
			case EVENT_RELEASE_COMPLETE:
				chan_misdn_log(1, bc->port, " --> no Ch, so we've already released.\n");
				break;
			case EVENT_CLEANUP:
			case EVENT_BCHAN_DATA:
				return -1;

			default:
				chan_misdn_log(1,bc->port, "Chan not existing at the moment bc->l3id:%x bc:%p event:%s port:%d channel:%d\n",bc->l3_id, bc, manager_isdn_get_info( event), bc->port,bc->channel);
				return -1;
		}
	}
	
	if (ch ) {
		switch (event) {
		case EVENT_DISCONNECT:
		case EVENT_RELEASE:
		case EVENT_RELEASE_COMPLETE:
		case EVENT_CLEANUP:
		case EVENT_TIMEOUT:
			if (!ch->cw)
				chan_misdn_log(3,bc->port,"cw_hangup already called, so we have no cw ptr anymore in event(%s)\n",manager_isdn_get_info(event));
			break;
		default:
			if ( !ch->cw  || !MISDN_CALLWEAVER_PVT(ch->cw) || !MISDN_CALLWEAVER_TECH_PVT(ch->cw)) {
				if (event!=EVENT_BCHAN_DATA)
					cw_log(CW_LOG_NOTICE, "No Opbx or No private Pointer in Event (%d:%s)\n", event, manager_isdn_get_info(event));
				return -1;
			}
		}
	}
	
	
	switch (event) {
	case EVENT_PORT_ALARM:
		{
			int boa=0;

			misdn_cfg_get( bc->port, MISDN_CFG_ALARM_BLOCK, &boa, sizeof(int));
			if (boa) {
				cb_log(1,bc->port," --> blocking\n");
				misdn_lib_port_block(bc->port);	
			}
		}
		break;

	case EVENT_BCHAN_ACTIVATED:
		break;
		
	case EVENT_NEW_CHANNEL:
		cw_change_name(ch->cw, "%s/%d-u%d", misdn_type, update_name(bc->port, bc->channel), ++glob_channel);
		break;
		
	case EVENT_NEW_L3ID:
		ch->l3id=bc->l3_id;
		ch->addr=bc->addr;
		break;

	case EVENT_NEW_BC:
		if (!ch) {
			ch=find_holded(cl_te,bc);
		}
		
		if (!ch) {
			cw_log(CW_LOG_WARNING,"NEW_BC without chan_list?\n");
			break;
		}

		if (bc)
			ch->bc=(struct misdn_bchannel*)user_data;
		break;
		
	case EVENT_DTMF_TONE:
	{
		/*  sending INFOS as DTMF-Frames :) */
		struct cw_frame fr;

        cw_fr_init_ex(&fr, CW_FRAME_DTMF, bc->dtmf);
		if (!ch->ignore_dtmf) {
			chan_misdn_log(2, bc->port, " --> DTMF:%c\n", bc->dtmf);
			cw_queue_frame(ch->cw, &fr);
		} else {
			chan_misdn_log(2, bc->port, " --> Ingoring DTMF:%c due to bridge flags\n", bc->dtmf);
		}
	}
	break;
	case EVENT_STATUS:
		break;
    
	case EVENT_INFORMATION:
	{
		int stop_tone;
		misdn_cfg_get( 0, MISDN_GEN_STOP_TONE, &stop_tone, sizeof(int));
		if ( stop_tone ) {
			stop_indicate(ch);
		}
		
		if (ch->state == MISDN_WAITING4DIGS ) {
			/*  Ok, incomplete Setup, waiting till extension exists */
			{
				int l = sizeof(bc->dad);
				strncat(bc->dad,bc->info_dad, l);
				bc->dad[l-1] = 0;
			}
			
			
			{
				int l = sizeof(ch->cw->exten);
				strncpy(ch->cw->exten, bc->dad, l);
				ch->cw->exten[l-1] = 0;
			}
/*			chan_misdn_log(5, bc->port, "Can Match Extension: dad:%s oad:%s\n",bc->dad,bc->oad);*/
			
			/* Check for Pickup Request first */
			if (!strcmp(ch->cw->exten, cw_pickup_ext())) {
				int ret;/** Sending SETUP_ACK**/
				ret = misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE );
				if (cw_pickup_call(ch->cw)) {
					hangup_chan(ch);
				} else {
					struct cw_channel *chan=ch->cw;
					ch->state = MISDN_CALLING_ACKNOWLEDGE;
					cw_setstate(chan, CW_STATE_DOWN);
					hangup_chan(ch);
					ch->cw=NULL;
					break;
				}
			}
			
			if(!cw_canmatch_extension(ch->cw, ch->context, bc->dad, 1, bc->oad)) {

				chan_misdn_log(-1, bc->port, "Extension can never match, so disconnecting\n");
				if (bc->nt)
					hanguptone_indicate(ch);
				ch->state=MISDN_EXTCANTMATCH;
				bc->out_cause=1;

				misdn_lib_send_event(bc, EVENT_DISCONNECT );

				break;
			}

			if (ch->overlap_dial) {
				cw_mutex_lock(&ch->overlap_tv_lock);
				ch->overlap_tv = cw_tvnow();
				cw_mutex_unlock(&ch->overlap_tv_lock);
				if (ch->overlap_dial_task == -1) {
					ch->overlap_dial_task = 
						misdn_tasks_add_variable(ch->overlap_dial, misdn_overlap_dial_task, ch);
				}
				break;
			}

			if (cw_exists_extension(ch->cw, ch->context, bc->dad, 1, bc->oad)) {
				ch->state=MISDN_DIALING;
	  
				stop_indicate(ch);
/*				chan_misdn_log(1, bc->port, " --> * Starting Opbx ctx:%s\n", ch->context);*/
				if (pbx_start_chan(ch)<0) {
					hangup_chan(ch);

					chan_misdn_log(-1, bc->port, "cw_pbx_start returned < 0 in INFO\n");
				 	if (bc->nt) hanguptone_indicate(ch);

					misdn_lib_send_event(bc, EVENT_DISCONNECT );
				}
			}
	
		} else {
			/*  sending INFOS as DTMF-Frames :) */
			struct cw_frame fr;
			int digits;

            cw_fr_init_ex(&fr, CW_FRAME_DTMF, bc->info_dad[0]);
			
			misdn_cfg_get( 0, MISDN_GEN_APPEND_DIGITS2EXTEN, &digits, sizeof(int));
			if (ch->state != MISDN_CONNECTED ) {
				if (digits) {
					int l = sizeof(bc->dad);
					strncat(bc->dad,bc->info_dad, l);
					bc->dad[l-1] = 0;
					l = sizeof(ch->cw->exten);
					strncpy(ch->cw->exten, bc->dad, l);
					ch->cw->exten[l-1] = 0;

					cw_cdr_update(ch->cw);
				}
				
				cw_queue_frame(ch->cw, &fr);
			}
		}
	}
	break;
	case EVENT_SETUP:
	{
		struct chan_list *ch=find_chan_by_bc(cl_te, bc);
		if (ch && ch->state != MISDN_NOTHING ) {
			chan_misdn_log(1, bc->port, " --> Ignoring Call we have already one\n");
			return RESPONSE_IGNORE_SETUP_WITHOUT_CLOSE;
		}
	}
	

	int msn_valid = misdn_cfg_is_msn_valid(bc->port, bc->dad);
	if (!bc->nt && ! msn_valid) {
		chan_misdn_log(1, bc->port, " --> Ignoring Call, its not in our MSN List\n");
		return RESPONSE_IGNORE_SETUP; /*  Ignore MSNs which are not in our List */
	}

	
	print_bearer(bc);
    
	{
		struct chan_list *ch=init_chan_list(ORG_MISDN);
		struct cw_channel *chan;
		int exceed;

		if (!ch) { chan_misdn_log(-1, bc->port, "cb_events: malloc for chan_list failed!\n"); return 0;}
		
		ch->bc = bc;
		ch->l3id=bc->l3_id;
		ch->addr=bc->addr;
		ch->orginator = ORG_MISDN;

		chan=misdn_new(ch, CW_STATE_RESERVED,bc->dad, bc->oad, CW_FORMAT_ALAW, bc->port, bc->channel);
		ch->cw = chan;

		if ((exceed=add_in_calls(bc->port))) {
			char tmp[16];
			sprintf(tmp,"%d",exceed);
			pbx_builtin_setvar_helper(chan,"MAX_OVERFLOW",tmp);
		}

		read_config(ch, ORG_MISDN);
		
		export_ch(chan, bc, ch);

		cw_setstate(ch->cw, CW_STATE_RINGING);

		int pres,screen;

		switch (bc->pres) {
			case 1:
			pres=CW_PRES_RESTRICTED; chan_misdn_log(2,bc->port," --> PRES: Restricted (1)\n");
			break;
			case 2:
			pres=CW_PRES_UNAVAILABLE; chan_misdn_log(2,bc->port," --> PRES: Restricted (2)\n");
			break;
			default:
			pres=CW_PRES_ALLOWED; chan_misdn_log(2,bc->port," --> PRES: Restricted (%d)\n", bc->pres);
		}

		switch (bc->screen) {
			case 0:
			screen=CW_PRES_USER_NUMBER_UNSCREENED;  chan_misdn_log(2,bc->port," --> SCREEN: Unscreened (0)\n");
			break;
			case 1:
			screen=CW_PRES_USER_NUMBER_PASSED_SCREEN; chan_misdn_log(2,bc->port," --> SCREEN: Passed screen (1)\n");
			break;
			case 2:
			screen=CW_PRES_USER_NUMBER_FAILED_SCREEN; chan_misdn_log(2,bc->port," --> SCREEN: failed screen (2)\n");
			break;
			case 3:
			screen=CW_PRES_NETWORK_NUMBER; chan_misdn_log(2,bc->port," --> SCREEN: Network Number (3)\n");
			break;
			default:
			screen=CW_PRES_USER_NUMBER_UNSCREENED; chan_misdn_log(2,bc->port," --> SCREEN: Unscreened (%d)\n",bc->screen);
		}

		chan->cid.cid_pres=pres+screen;

		pbx_builtin_setvar_helper(chan, "TRANSFERCAPABILITY", cw_transfercapability2str(bc->capability));
		chan->transfercapability=bc->capability;
		
		switch (bc->capability) {
		case INFO_CAPABILITY_DIGITAL_UNRESTRICTED:
			pbx_builtin_setvar_helper(chan,"CALLTYPE","DIGITAL");
			break;
		default:
			pbx_builtin_setvar_helper(chan,"CALLTYPE","SPEECH");
		}

		/** queue new chan **/
		cl_queue_chan(&cl_te, ch) ;


		if (!strstr(ch->allowed_bearers,"all")) {
			int i;
			for (i=0; i< sizeof(allowed_bearers_array)/sizeof(struct allowed_bearers); i++) {
				if (allowed_bearers_array[i].cap == bc->capability) {
					if (  !strstr( ch->allowed_bearers, allowed_bearers_array[i].name)) {
						chan_misdn_log(0,bc->port,"Bearer Not allowed\b");
						bc->out_cause=88;
						
						ch->state=MISDN_EXTCANTMATCH;
						misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE );
						return RESPONSE_OK;
					}
				}
				
			}
		}
		
		/* Check for Pickup Request first */
		if (!strcmp(chan->exten, cw_pickup_ext())) {
			int ret;/** Sending SETUP_ACK**/
			ret = misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE );
			if (cw_pickup_call(chan)) {
				hangup_chan(ch);
			} else {
				ch->state = MISDN_CALLING_ACKNOWLEDGE;
				cw_setstate(chan, CW_STATE_DOWN);
				hangup_chan(ch);
				ch->cw=NULL;
				break;
			}
		}
		
		/*
		  added support for s extension hope it will help those poor cretains
		  which haven't overlap dial.
		*/
		{
			int ai;
			misdn_cfg_get( bc->port, MISDN_CFG_ALWAYS_IMMEDIATE, &ai, sizeof(ai));
			if ( ai ) {
				do_immediate_setup(bc, ch , chan);
				break;
			}
			
			
			
		}

		/* check if we should jump into s when we have no dad */
		{
			int im;
			misdn_cfg_get( bc->port, MISDN_CFG_IMMEDIATE, &im, sizeof(im));
			if ( im && cw_strlen_zero(bc->dad) ) {
				do_immediate_setup(bc, ch , chan);
				break;
			}
		}

		
			chan_misdn_log(5,bc->port,"CONTEXT:%s\n",ch->context);
			if(!cw_canmatch_extension(ch->cw, ch->context, bc->dad, 1, bc->oad)) {
			
			chan_misdn_log(-1, bc->port, "Extension can never match, so disconnecting\n");

			if (bc->nt)
				hanguptone_indicate(ch);
			ch->state=MISDN_EXTCANTMATCH;
			bc->out_cause=1;

			if (bc->nt)
				misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE );
			else
				misdn_lib_send_event(bc, EVENT_RELEASE );
				
			break;
		}
		
		if (!ch->overlap_dial && cw_exists_extension(ch->cw, ch->context, bc->dad, 1, bc->oad)) {
			ch->state=MISDN_DIALING;
			
			if (bc->nt || (bc->need_more_infos && misdn_lib_is_ptp(bc->port)) ) {
				int ret; 
				ret = misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE );
			} else {
				int ret;
				ret= misdn_lib_send_event(bc, EVENT_PROCEEDING );
			}
	
			if (pbx_start_chan(ch)<0) {
				hangup_chan(ch);

				chan_misdn_log(-1, bc->port, "cw_pbx_start returned <0 in SETUP\n");
				chan=NULL;

				if (bc->nt) {
					hanguptone_indicate(ch);
					misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE );
				} else
					misdn_lib_send_event(bc, EVENT_RELEASE);
			}
		} else {

			if (bc->sending_complete) {
				ch->state=MISDN_EXTCANTMATCH;
				bc->out_cause=1;

				if (bc->nt)  {
					chan_misdn_log(0,bc->port," --> sending_complete so we never match ..\n");
					misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE);
				} else {
					chan_misdn_log(0,bc->port," --> sending_complete so we never match ..\n");
					misdn_lib_send_event(bc, EVENT_RELEASE);
				}

			} else {

				int ret= misdn_lib_send_event(bc, EVENT_SETUP_ACKNOWLEDGE );
				if (ret == -ENOCHAN) {
					cw_log(CW_LOG_WARNING,"Channel was catched, before we could Acknowledge\n");
					misdn_lib_send_event(bc,EVENT_RELEASE_COMPLETE);
				}
				/*  send tone to phone :) */
				
				/** ADD IGNOREPAT **/
				
				int stop_tone, dad_len;
				misdn_cfg_get( 0, MISDN_GEN_STOP_TONE, &stop_tone, sizeof(int));
				dad_len = cw_strlen_zero(bc->dad);
				if ( !dad_len && stop_tone ) 
					stop_indicate(ch);
				else {
					dialtone_indicate(ch);
				}
				
				ch->state=MISDN_WAITING4DIGS;

				if (ch->overlap_dial && !dad_len) {
					cw_mutex_lock(&ch->overlap_tv_lock);
					ch->overlap_tv = cw_tvnow();
					cw_mutex_unlock(&ch->overlap_tv_lock);
					if (ch->overlap_dial_task == -1) {
						ch->overlap_dial_task = 
							misdn_tasks_add_variable(ch->overlap_dial, misdn_overlap_dial_task, ch);
					}
				}
			}
		}
      
	}
	break;
	case EVENT_SETUP_ACKNOWLEDGE:
	{
		ch->state = MISDN_CALLING_ACKNOWLEDGE;

		if (bc->channel) 
			cw_change_name(ch->cw, "%s/%d-u%d", misdn_type, update_name(bc->port, bc->channel), ++glob_channel);

		if (!cw_strlen_zero(bc->infos_pending)) {
			/* TX Pending Infos */
			
			{
				int l = sizeof(bc->dad);
				strncat(bc->dad,bc->infos_pending, l - strlen(bc->dad));
				bc->dad[l-1] = 0;
			}	
			{
				int l = sizeof(ch->cw->exten);
				strncpy(ch->cw->exten, bc->dad, l);
				ch->cw->exten[l-1] = 0;
			}
			{
				int l = sizeof(bc->info_dad);
				strncpy(bc->info_dad, bc->infos_pending, l);
				bc->info_dad[l-1] = 0;
			}
			strncpy(bc->infos_pending,"", 1);

			misdn_lib_send_event(bc, EVENT_INFORMATION);
		}
	}
	break;
	case EVENT_PROCEEDING:
	{
		
		if ( misdn_cap_is_speech(bc->capability) &&
		     misdn_inband_avail(bc) ) {
			start_bc_tones(ch);
		}

		ch->state = MISDN_PROCEEDING;
		
		cw_queue_control(ch->cw, CW_CONTROL_PROCEEDING);
	}
	break;
	case EVENT_PROGRESS:
		if (!bc->nt ) {
			if ( misdn_cap_is_speech(bc->capability) &&
			     misdn_inband_avail(bc)
				) {
				start_bc_tones(ch);
			}
			
			cw_queue_control(ch->cw, CW_CONTROL_PROGRESS);
			
			ch->state=MISDN_PROGRESS;
		}
		break;
		
		
	case EVENT_ALERTING:
	{
		ch->state = MISDN_ALERTING;
		
		cw_queue_control(ch->cw, CW_CONTROL_RINGING);
		cw_setstate(ch->cw, CW_STATE_RINGING);
		
		cb_log(1,bc->port,"Set State Ringing\n");
		
		if ( misdn_cap_is_speech(bc->capability) && misdn_inband_avail(bc)) {
			cb_log(1,bc->port,"Starting Tones, we have inband Data\n");
			start_bc_tones(ch);
		} else {
			cb_log(1,bc->port,"We have no inband Data, the other end must create ringing\n");
			if (ch->far_alerting) {
				cb_log(1,bc->port,"The other end can not do ringing eh ?.. we must do all ourself..");
				start_bc_tones(ch);
			}
		}
	}
	break;
	case EVENT_CONNECT:
	{
		/*we answer when we've got our very new L3 ID from the NT stack */
		misdn_lib_send_event(bc, EVENT_CONNECT_ACKNOWLEDGE);
	
		struct cw_channel *bridged = cw_bridged_channel(ch->cw);

		misdn_lib_echo(bc, 0);
		stop_indicate(ch);

		if (bridged) {
			if (!strcasecmp(bridged->tech->type, "mISDN")) {
				struct chan_list *bridged_ch = MISDN_CALLWEAVER_TECH_PVT(bridged);

				chan_misdn_log(1, bc->port, " --> copying cpndialplan:%d and cad:%s to the A-Channel\n", bc->cpnnumplan, bc->cad);
				if (bridged_ch) {
					bridged_ch->bc->cpnnumplan = bc->cpnnumplan;
					cw_copy_string(bridged_ch->bc->cad, bc->cad, sizeof(bc->cad));
				}
			}

			cw_object_put(bridged);
		}
	}
	
	
	/* notice that we don't break here!*/

	case EVENT_CONNECT_ACKNOWLEDGE:
	{
		ch->l3id=bc->l3_id;
		ch->addr=bc->addr;
		
		start_bc_tones(ch);
		
		
		ch->state = MISDN_CONNECTED;
		cw_queue_control(ch->cw, CW_CONTROL_ANSWER);
	}
	break;
	case EVENT_DISCONNECT:
	/*we might not have an ch->cw ptr here anymore*/
	if (ch) {
		struct chan_list *holded_ch=find_holded(cl_te, bc);
	
		chan_misdn_log(3,bc->port," --> org:%d nt:%d, inbandavail:%d state:%d\n", ch->orginator, bc->nt, misdn_inband_avail(bc), ch->state);
		if ( ch->orginator==ORG_CW && !bc->nt && misdn_inband_avail(bc) && ch->state != MISDN_CONNECTED) {
			/* If there's inband information available (e.g. a
			   recorded message saying what was wrong with the
			   dialled number, or perhaps even giving an
			   alternative number, then play it instead of
			   immediately releasing the call */
			chan_misdn_log(1,bc->port, " --> Inband Info Avail, not sending RELEASE\n");
		
			ch->state=MISDN_DISCONNECTED;
			start_bc_tones(ch);
			break;
		}
		
		/*Check for holded channel, to implement transfer*/
		if (holded_ch && ch->cw ) {
			cb_log(1,bc->port," --> found holded ch\n");
			if  (ch->state == MISDN_CONNECTED ) {
				misdn_transfer_bc(ch, holded_ch) ;
			}
			hangup_chan(ch);
			release_chan(bc);
			break;
		}
		
		stop_bc_tones(ch);
		hangup_chan(ch);
	}
	bc->out_cause=-1;
	if (bc->need_release) misdn_lib_send_event(bc,EVENT_RELEASE);
	break;
	
	case EVENT_RELEASE:
		{
			bc->out_cause=16;
			
			hangup_chan(ch);
			release_chan(bc);
		
			if (bc->need_release_complete) 
				misdn_lib_send_event(bc,EVENT_RELEASE_COMPLETE);
		}
		break;
	case EVENT_RELEASE_COMPLETE:
	{
		stop_bc_tones(ch);
		hangup_chan(ch);
		release_chan(bc);
		if(ch)	
			ch->state=MISDN_CLEANING;
	}
	break;
	case EVENT_CLEANUP:
	{
		stop_bc_tones(ch);
		
		switch(ch->state) {
			case MISDN_CALLING:
				bc->cause=27; /* Destination out of order */
			break;
			default:
			break;
		}
		
		hangup_chan(ch);
		release_chan(bc);
	}
	break;

	case EVENT_BCHAN_DATA:
	{
		if ( !misdn_cap_is_speech(ch->bc->capability) ) {
			struct cw_frame frame;
			/*In Data Modes we queue frames*/
			frame.frametype  = CW_FRAME_VOICE; /*we have no data frames yet*/
			frame.subclass = CW_FORMAT_ALAW;
			frame.datalen = bc->bframe_len;
			frame.samples = bc->bframe_len ;
			frame.mallocd =0 ;
			frame.offset= 0 ;
			frame.data = bc->bframe ;
			
			cw_queue_frame(ch->cw,&frame);
		} else {
 			int l=misdn_jb_fill(ch->jb_rx, bc->bframe, bc->bframe_len);
 			if (l<0) {
 				cb_log(0,bc->port,"jb_fill overflow:%d\n",l);
			}

			int len = misdn_jb_empty(ch->jb_rx, ch->cw_rd_buf,
						 bc->bframe_len);
			if (len > 0) { 
				trigger_read(ch, ch->cw_rd_buf, len);
			} else {
				cb_log(0,ch->bc->port,"jb_read underrun: %d\n",len);
			}

		}
	}
	break;
	case EVENT_TIMEOUT:
		{
		if (ch && bc)
			chan_misdn_log(1,bc->port,"--> state: %s\n",misdn_get_ch_state(ch));

		switch (ch->state) {
			case MISDN_CALLING:
			case MISDN_DIALING:
			case MISDN_PROGRESS:
			case MISDN_ALERTING:
			case MISDN_PROCEEDING:
			case MISDN_CALLING_ACKNOWLEDGE:
				if (bc->nt) {
					bc->progress_indicator=8;
					hanguptone_indicate(ch);
				}
				
				bc->out_cause=1;
				misdn_lib_send_event(bc,EVENT_DISCONNECT);
			break;

			case MISDN_WAITING4DIGS:
				if (bc->nt) {
					bc->progress_indicator=8;
					bc->out_cause=1;
					hanguptone_indicate(ch);
					misdn_lib_send_event(bc,EVENT_DISCONNECT);
				} else {
					bc->out_cause=16;
					misdn_lib_send_event(bc,EVENT_RELEASE);
				}
				
			break;


			case MISDN_CLEANING: 
				chan_misdn_log(1,bc->port," --> in state cleaning .. so ingoring, the stack should clean it for us\n");
			break;

			default:
				misdn_lib_send_event(bc,EVENT_RELEASE_COMPLETE);
			}
		}
		break;

    
	/***************************/
	/** Suplementary Services **/
	/***************************/
	case EVENT_RETRIEVE:
	{
		struct cw_channel *hold_cw;

		ch=find_holded(cl_te, bc);
		if (!ch) {
			cw_log(CW_LOG_WARNING, "Found no Holded channel, cannot Retrieve\n");
			misdn_lib_send_event(bc, EVENT_RETRIEVE_REJECT);
			break;
		}

		ch->state = MISDN_CONNECTED;

		if ((hold_cw = cw_bridged_channel(ch->cw))) {
			cw_moh_stop(hold_cw);
			cw_object_put(hold_cw);
		}

		if (misdn_lib_send_event(bc, EVENT_RETRIEVE_ACKNOWLEDGE) < 0)
			misdn_lib_send_event(bc, EVENT_RETRIEVE_REJECT);
	}
	break;
    
	case EVENT_HOLD:
	{
		int hold_allowed;
		misdn_cfg_get( bc->port, MISDN_CFG_HOLD_ALLOWED, &hold_allowed, sizeof(int));
		
		if (!hold_allowed) {

			chan_misdn_log(-1, bc->port, "Hold not allowed this port.\n");
			misdn_lib_send_event(bc, EVENT_HOLD_REJECT);
			break;
		}

#if 0
		{
			struct chan_list *holded_ch=find_holded(cl_te, bc);
			if (holded_ch) {
				misdn_lib_send_event(bc, EVENT_HOLD_REJECT);

				chan_misdn_log(-1, bc->port, "We can't use RETRIEVE at the moment due to mISDN bug!\n");
				break;
			}
		}
#endif
		struct cw_channel *bridged;

		if ((bridged = cw_bridged_channel(ch->cw))) {
			struct chan_list *bridged_ch = MISDN_CALLWEAVER_TECH_PVT(bridged);

			ch->state = MISDN_HOLDED;
			ch->l3id = bc->l3_id;
			
			bc->holded_bc = bridged_ch->bc;
			misdn_lib_send_event(bc, EVENT_HOLD_ACKNOWLEDGE);

			cw_moh_start(bridged, NULL);

			cw_object_put(bridged);
		} else {
			misdn_lib_send_event(bc, EVENT_HOLD_REJECT);
			chan_misdn_log(0, bc->port, "We aren't bridged to anybody\n");
		}
	} 
	break;
	
	case EVENT_FACILITY:
		print_facility(bc);
		
		switch (bc->fac_type) {
		case FACILITY_CALLDEFLECT:
		{
			struct cw_channel *bridged;
			struct chan_list *ch;
			
			misdn_lib_send_event(bc, EVENT_DISCONNECT);

			if ((bridged = cw_bridged_channel(ch->cw))) {
				if (MISDN_CALLWEAVER_TECH_PVT(bridged)) {
					ch = MISDN_CALLWEAVER_TECH_PVT(bridged);
					/*ch->state = MISDN_FACILITY_DEFLECTED;*/
					if (ch->bc) {
						/* todo */
					}
				}
				cw_object_put(bridged);
			}
			
		} 
		
		break;
		default:
			chan_misdn_log(0, bc->port," --> not yet handled: facility type:%p\n", bc->fac_type);
		}
		
		break;

	case EVENT_RESTART:

		stop_bc_tones(ch);
		release_chan(bc);
		
		break;
				
	default:
		cw_log(CW_LOG_NOTICE, "Got Unknown Event\n");
		break;
	}
	
	return RESPONSE_OK;
}

/** TE STUFF END **/

/******************************************
 *
 *   CallWeaver Channel Endpoint END
 *
 *
 *******************************************/


static int g_config_initialized=0;

static void *misdn_set_opt_app;
static void *misdn_facility_app;

static int load_module(void)
{
	int i, port;
	char ports[256]="";

	max_ports=misdn_lib_maxports_get();
	
	if (max_ports<=0) {
		cw_log(CW_LOG_ERROR, "Unable to initialize mISDN\n");
		return -1;
	}
	
	if (misdn_cfg_init(max_ports)) {
		cw_log(CW_LOG_ERROR, "Unable to initialize misdn_config.\n");
		return -1;
	}
	g_config_initialized=1;
	
	misdn_debug = (int *)malloc(sizeof(int) * (max_ports+1));
	misdn_cfg_get( 0, MISDN_GEN_DEBUG, &misdn_debug[0], sizeof(int));
	for (i = 1; i <= max_ports; i++)
		misdn_debug[i] = misdn_debug[0];
	misdn_debug_only = (int *)calloc(max_ports + 1, sizeof(int));

	
	{
		char tempbuf[BUFFERSIZE+1];
		misdn_cfg_get( 0, MISDN_GEN_TRACEFILE, tempbuf, BUFFERSIZE);
		if (strlen(tempbuf))
			tracing = 1;
	}
	
	misdn_in_calls = (int *)malloc(sizeof(int) * (max_ports+1));
	misdn_out_calls = (int *)malloc(sizeof(int) * (max_ports+1));

	for (i=1; i <= max_ports; i++) {
		misdn_in_calls[i]=0;
		misdn_out_calls[i]=0;
	}
	
	cw_mutex_init(&cl_te_lock);

	misdn_cfg_update_ptp();
	misdn_cfg_get_ports_string(ports);

	if (strlen(ports))
		chan_misdn_log(0, 0, "Got: %s from get_ports\n",ports);
	
	{
		struct misdn_lib_iface iface = {
			.cb_event = cb_events,
			.cb_log = chan_misdn_log,
			.cb_jb_empty = chan_misdn_jb_empty,
		};
		
		if (misdn_lib_init(ports, &iface, NULL))
			chan_misdn_log(0, 0, "No te ports initialized\n");
	
		int ntflags=0;
		char ntfile[BUFFERSIZE+1];

		misdn_cfg_get( 0, MISDN_GEN_NTDEBUGFLAGS, &ntflags, sizeof(int));
		misdn_cfg_get( 0, MISDN_GEN_NTDEBUGFILE, &ntfile, BUFFERSIZE);

		misdn_lib_nt_debug_init(ntflags,ntfile);

	}


	{
		if (cw_channel_register(&misdn_tech)) {
			cw_log(CW_LOG_ERROR, "Unable to register channel class %s\n", misdn_type);
			unload_module();
			return -1;
		}
	}
  
	cw_cli_register(&cli_send_display);
	cw_cli_register(&cli_send_cd);
	cw_cli_register(&cli_send_digit);
	cw_cli_register(&cli_toggle_echocancel);
	cw_cli_register(&cli_set_tics);

	cw_cli_register(&cli_show_cls);
	cw_cli_register(&cli_show_cl);
	cw_cli_register(&cli_show_config);
	cw_cli_register(&cli_show_port);
	cw_cli_register(&cli_show_stacks);
	cw_cli_register(&cli_show_ports_stats);

	cw_cli_register(&cli_port_block);
	cw_cli_register(&cli_port_unblock);
	cw_cli_register(&cli_restart_port);
	cw_cli_register(&cli_port_up);
	cw_cli_register(&cli_port_down);
	cw_cli_register(&cli_set_debug);
	cw_cli_register(&cli_set_crypt_debug);
	cw_cli_register(&cli_reload);

  
	misdn_set_opt_app = cw_register_function("MISDNSetOpt", misdn_set_opt_exec, "misdn_set_opt",
				 "misdn_set_opt(:<opt><optarg>:<opt><optarg>..)",
				 "Sets mISDN opts. and optargs\n"
				 "\n"
				 "The available options are:\n"
				 "    d - Send display text on called phone, text is the optparam\n"
				 "    n - don't detect dtmf tones on called channel\n"
				 "    h - make digital outgoing call\n" 
				 "    c - make crypted outgoing call, param is keyindex\n"
				 "    e - perform echo cancelation on this channel,\n"
				 "        takes taps as arguments (32,64,128,256)\n"
				 "    s - send Non Inband DTMF as inband\n"
				 "   vr - rxgain control\n"
				 "   vt - txgain control\n"
		);

	
	misdn_facility_app = cw_register_function("MISDNFacility", misdn_facility_exec, "misdn_facility",
				 "misdn_facility(<FACILITY_TYPE>|<ARG1>|..)",
				 "Sends the Facility Message FACILITY_TYPE with \n"
				 "the given Arguments to the current ISDN Channel\n"
				 "Supported Facilities are:\n"
				 "\n"
				 "type=calldeflect args=Nr where to deflect\n"
		);


	int ntflags=0;
	char ntfile[BUFFERSIZE+1];

	misdn_cfg_get( 0, MISDN_GEN_NTDEBUGFLAGS, &ntflags, sizeof(int));
	misdn_cfg_get( 0, MISDN_GEN_NTDEBUGFILE, &ntfile, BUFFERSIZE);

	misdn_lib_nt_debug_init(ntflags,ntfile);

	misdn_cfg_get( 0, MISDN_GEN_TRACEFILE, global_tracefile, BUFFERSIZE);

	/* start the l1 watchers */
	for (port = misdn_cfg_get_next_port(0); port >= 0; port = misdn_cfg_get_next_port(port)) {
		int l1timeout;
		misdn_cfg_get(port, MISDN_CFG_L1_TIMEOUT, &l1timeout, sizeof(l1timeout));
		if (l1timeout) {
			chan_misdn_log(4, 0, "Adding L1watcher task: port:%d timeout:%ds\n", port, l1timeout);
			misdn_tasks_add(l1timeout * 1000, misdn_l1_task, (void*)port);  
		}
	}
	
	reload_config();

	chan_misdn_log(0, 0, "-- mISDN Channel Driver Registred -- (" CHAN_MISDN_VERSION ")\n");

	return 0;
}



static int unload_module(void)
{
	int res = 0;

	/* First, take us out of the channel loop */
	cw_log(CW_LOG_VERBOSE, "-- Unregistering mISDN Channel Driver --\n");

	if (misdn_tasks) {
		sched_context_destroy(misdn_tasks);
		misdn_tasks = NULL;
	}
	
	if (!g_config_initialized) return 0;
	
	cw_cli_unregister(&cli_send_display);
	
	cw_cli_unregister(&cli_send_cd);
	
	cw_cli_unregister(&cli_send_digit);
	cw_cli_unregister(&cli_toggle_echocancel);
	cw_cli_unregister(&cli_set_tics);
  
	cw_cli_unregister(&cli_show_cls);
	cw_cli_unregister(&cli_show_cl);
	cw_cli_unregister(&cli_show_config);
	cw_cli_unregister(&cli_show_port);
	cw_cli_unregister(&cli_show_ports_stats);
	cw_cli_unregister(&cli_show_stacks);
	cw_cli_unregister(&cli_port_block);
	cw_cli_unregister(&cli_port_unblock);
	cw_cli_unregister(&cli_restart_port);
	cw_cli_unregister(&cli_port_up);
	cw_cli_unregister(&cli_port_down);
	cw_cli_unregister(&cli_set_debug);
	cw_cli_unregister(&cli_set_crypt_debug);
	cw_cli_unregister(&cli_reload);
	res |= cw_unregister_function(misdn_set_opt_app);
	res |= cw_unregister_function(misdn_facility_app);
  
	cw_channel_unregister(&misdn_tech);


	free_robin_list();
	misdn_cfg_destroy();
	misdn_lib_destroy();
  
	free(misdn_debug);
	free(misdn_debug_only);
	
	return res;
}

MODULE_INFO(load_module, reload_config, unload_module, NULL, desc)



/*** SOME APPS ;)***/

static int misdn_facility_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct chan_list *ch = MISDN_CALLWEAVER_TECH_PVT(chan);

	CW_UNUSED(result);

	chan_misdn_log(0,0,"TYPE: %s\n",chan->tech->type);

	if (strcasecmp(chan->tech->type,"mISDN")) {
		cw_log(CW_LOG_WARNING, "misdn_facility makes only sense with chan_misdn channels!\n");
		return -1;
	}

	if (argc < 1 || !argv[0][0]) {
		cw_log(CW_LOG_WARNING, "misdn_facility Requires arguments\n");
		return -1;
	}

	if (!strcasecmp(argv[0],"calldeflect")) {
		if (argc < 2 || !argv[1][0])
			cw_log(CW_LOG_WARNING, "Facility: Call Defl Requires arguments\n");
		else 
			misdn_lib_send_facility(ch->bc, FACILITY_CALLDEFLECT, argv[1]);
		
	} else {
		cw_log(CW_LOG_WARNING, "Unknown Facility: %s\n", argv[0]);
	}

	return 0;
}


static int misdn_set_opt_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct chan_list *ch = MISDN_CALLWEAVER_TECH_PVT(chan);
	char *tok,*tokb;
	int  keyidx=0;
	int rxgain=0;
	int txgain=0;
	int change_jitter=0;

	CW_UNUSED(result);

	if (strcasecmp(chan->tech->type,"mISDN")) {
		cw_log(CW_LOG_WARNING, "misdn_set_opt makes only sense with chan_misdn channels!\n");
		return -1;
	}

	if (argc != 1 || !argv[0][0]) {
		cw_log(CW_LOG_WARNING, "misdn_set_opt Requires arguments\n");
		return -1;
	}

	for (tok=strtok_r(argv[0], ":",&tokb);
	     tok;
	     tok=strtok_r(NULL,":",&tokb) ) {
		int neglect=0;

		if (tok[0] == '!' ) {
			neglect=1;
			tok++;
		}

		switch(tok[0]) {
			
		case 'd' :
			cw_copy_string(ch->bc->display,++tok,84);
			chan_misdn_log(1, ch->bc->port, "SETOPT: Display:%s\n",ch->bc->display);
			break;

		case 'n':
			chan_misdn_log(1, ch->bc->port, "SETOPT: No DSP\n");
			ch->bc->nodsp=1;
			break;

		case 'j':
			chan_misdn_log(1, ch->bc->port, "SETOPT: jitter\n");
			tok++;
			change_jitter=1;

			switch ( tok[0] ) {
			case 'b' :
				ch->jb_len=atoi(++tok);
				chan_misdn_log(1, ch->bc->port, " --> buffer_len:%d\n",ch->jb_len);
				break;
			case 't' :
				ch->jb_upper_threshold=atoi(++tok);
				chan_misdn_log(1, ch->bc->port, " --> upper_threshold:%d\n",ch->jb_upper_threshold);
				break;

			case 'n':
				ch->bc->nojitter=1;
				chan_misdn_log(1, ch->bc->port, " --> nojitter\n");
				break;

			default:
				ch->jb_len=4000;
				ch->jb_upper_threshold=0;
				chan_misdn_log(1, ch->bc->port, " --> buffer_len:%d (default)\n",ch->jb_len);
				chan_misdn_log(1, ch->bc->port, " --> upper_threshold:%d (default)\n",ch->jb_upper_threshold);
			}

			break;

		case 'v':
			tok++;

			switch ( tok[0] ) {
			case 'r' :
				rxgain=atoi(++tok);
				if (rxgain<-8) rxgain=-8;
				if (rxgain>8) rxgain=8;
				ch->bc->rxgain=rxgain;
				chan_misdn_log(1, ch->bc->port, "SETOPT: Volume:%d\n",rxgain);
				break;
			case 't':
				txgain=atoi(++tok);
				if (txgain<-8) txgain=-8;
				if (txgain>8) txgain=8;
				ch->bc->txgain=txgain;
				chan_misdn_log(1, ch->bc->port, "SETOPT: Volume:%d\n",txgain);
				break;
			}
			break;

		case 'c':
			keyidx=atoi(++tok);
      
			if (keyidx > misdn_key_vector_size  || keyidx < 0 ) {
				cw_log(CW_LOG_WARNING, "You entered the keyidx: %d but we have only %d keys\n",keyidx, misdn_key_vector_size );
				continue; 
			}
      
			{
				cw_copy_string(ch->bc->crypt_key,  misdn_key_vector[keyidx], sizeof(ch->bc->crypt_key));
			}
			
			chan_misdn_log(0, ch->bc->port, "SETOPT: crypt with key:%s\n",misdn_key_vector[keyidx]);
			break;

		case 'e':
			chan_misdn_log(1, ch->bc->port, "SETOPT: EchoCancel\n");
			
			if (neglect) {
				chan_misdn_log(1, ch->bc->port, " --> disabled\n");
				ch->bc->ec_enable=0;

#ifdef WITH_BEROEC
				ch->bc->bnec_tail=0;
#endif
			} else {
				ch->bc->ec_enable=1;
				ch->bc->orig=ch->orginator;
				tok++;
				if (tok) {
					ch->bc->ec_deftaps=atoi(tok);
				}
			}
			
			break;
      
		case 'h':
			chan_misdn_log(1, ch->bc->port, "SETOPT: Digital\n");
			
			if (strlen(tok) > 1 && tok[1]=='1') {
				chan_misdn_log(1, ch->bc->port, "SETOPT: HDLC \n");
				ch->bc->hdlc=1;
			}  
			ch->bc->capability=INFO_CAPABILITY_DIGITAL_UNRESTRICTED;
			break;
            
		case 's':
			chan_misdn_log(1, ch->bc->port, "SETOPT: Send DTMF\n");
			ch->bc->send_dtmf=1;
			break;
			
		case 'f':
			chan_misdn_log(1, ch->bc->port, "SETOPT: Faxdetect\n");
			ch->faxdetect=1;
			misdn_cfg_get(ch->bc->port, MISDN_CFG_FAXDETECT_TIMEOUT, &ch->faxdetect_timeout, sizeof(ch->faxdetect_timeout));
			break;

		case 'a':
			chan_misdn_log(1, ch->bc->port, "SETOPT: CW_DSP (for DTMF)\n");
			ch->cw_dsp=1;
			break;

		case 'p':
			chan_misdn_log(1, ch->bc->port, "SETOPT: callerpres: %s\n",&tok[1]);
			/* CRICH: callingpres!!! */
			if (strstr(tok,"allowed") ) {
				ch->bc->pres=0;
			} else if (strstr(tok,"not_screened")) {
				ch->bc->pres=1;
			}
			
			
			break;
      
      
		default:
			break;
		}
	}

	if (change_jitter)
		config_jitterbuffer(ch);
	
	
	if (ch->faxdetect || ch->cw_dsp)
    {
		if (!ch->dsp)
            ch->dsp = cw_dsp_new();
		if (ch->dsp)
            cw_dsp_set_features(ch->dsp, DSP_FEATURE_DTMF_DETECT | DSP_FEATURE_FAX_CNG_DETECT);
		if (!ch->trans)
            ch->trans=cw_translator_build_path(CW_FORMAT_SLINEAR, CW_FORMAT_ALAW);
	}

	if (ch->cw_dsp) {
		chan_misdn_log(1,ch->bc->port,"SETOPT: with CW_DSP we deactivate mISDN_dsp\n");
		ch->bc->nodsp=1;
		ch->bc->nojitter=1;
	}
	
	return 0;
}


int chan_misdn_jb_empty ( struct misdn_bchannel *bc, char *buf, int len) 
{
	struct chan_list *ch=find_chan_by_bc(cl_te, bc);
	
	if (ch && ch->jb) {
		return misdn_jb_empty(ch->jb, buf, len);
	}
	
	return -1;
}



/*******************************************************/
/***************** JITTERBUFFER ************************/
/*******************************************************/


/* allocates the jb-structure and initialise the elements*/
struct misdn_jb *misdn_jb_init(int size, int upper_threshold)
{
    int i;
    struct misdn_jb *jb = (struct misdn_jb*) malloc(sizeof(struct misdn_jb));
    jb->size = size;
    jb->upper_threshold = upper_threshold;
    jb->wp = 0;
    jb->rp = 0;
    jb->state_full = 0;
    jb->state_empty = 0;
    jb->bytes_wrote = 0;
    jb->samples = (char *)malloc(size*sizeof(char));

    if (!jb->samples) {
	    chan_misdn_log(-1,0,"No free Mem for jb->samples\n");
	    return NULL;
    }
    
    jb->ok = (char *)malloc(size*sizeof(char));

    if (!jb->ok) {
	    chan_misdn_log(-1,0,"No free Mem for jb->ok\n");
	    return NULL;
    }

    for(i=0; i<size; i++)
 	jb->ok[i]=0;

    cw_mutex_init(&jb->mutexjb);

    return jb;
}

/* frees the data and destroys the given jitterbuffer struct */
void misdn_jb_destroy(struct misdn_jb *jb)
{
	cw_mutex_destroy(&jb->mutexjb);
	
	free(jb->samples);
	free(jb);
}

/* fills the jitterbuffer with len data returns < 0 if there was an
   error (bufferoverflow). */
int misdn_jb_fill(struct misdn_jb *jb, const char *data, int len)
{
    int i, j, rp, wp;

    if (!jb || ! data) return 0;

    cw_mutex_lock (&jb->mutexjb);
    
    wp=jb->wp;
    rp=jb->rp;
	
    for(i=0; i<len; i++)
    {
	jb->samples[wp]=data[i];
	jb->ok[wp]=1;
	wp = (wp!=jb->size-1 ? wp+1 : 0);

	if(wp==jb->rp)
	    jb->state_full=1;
    }
    
    if(wp>=rp)
      jb->state_buffer=wp-rp;
    else
      jb->state_buffer= jb->size-rp+wp;
    chan_misdn_log(9,0,"misdn_jb_fill: written:%d | Bufferstatus:%d p:%x\n",len,jb->state_buffer,jb);
    
    if(jb->state_full)
    {
	jb->wp=wp;

	rp=wp;
	for(j=0; j<jb->upper_threshold; j++)
	    rp = (rp!=0 ? rp-1 : jb->size-1);
	jb->rp=rp;
	jb->state_full=0;
	jb->state_empty=1;

	cw_mutex_unlock (&jb->mutexjb);
	
	return -1;
    }

    if(!jb->state_empty)
    {
	jb->bytes_wrote+=len;
	if(jb->bytes_wrote>=jb->upper_threshold)
	{
	    jb->state_empty=1;
	    jb->bytes_wrote=0;
	}
    }
    jb->wp=wp;

    cw_mutex_unlock (&jb->mutexjb);
    
    return 0;
}

/* gets len bytes out of the jitterbuffer if available, else only the
available data is returned and the return value indicates the number
of data. */
int misdn_jb_empty(struct misdn_jb *jb, char *data, int len)
{
    int i;
    int wp;
    int rp;
    int read = 0;

    cw_mutex_lock (&jb->mutexjb);

    rp=jb->rp;
    wp=jb->wp;

    if(jb->state_empty)
    {	
	for(i=0; i<len; i++)
	{
	    if(wp==rp)
	    {
		jb->rp=rp;
		jb->state_empty=0;

		cw_mutex_unlock (&jb->mutexjb);
		
		return read;
	    }
	    else
	    {
		if (jb->ok[rp] == 1)
		{
		    data[i]=jb->samples[rp];
		    jb->ok[rp]=0;
		    rp=(rp!=jb->size-1 ? rp+1 : 0);
		    read+=1;
		}
	    }
	}

	if(wp >= rp)
		jb->state_buffer=wp-rp;
	else
		jb->state_buffer= jb->size-rp+wp;
	chan_misdn_log(9,0,"misdn_jb_empty: read:%d | Bufferstatus:%d p:%x\n",len,jb->state_buffer,jb);
	
	jb->rp=rp;
    }
    else
	    chan_misdn_log(9,0,"misdn_jb_empty: Wait...requested:%d p:%x\n",len,jb);
    
    cw_mutex_unlock (&jb->mutexjb);

    return read;
}

int misdn_jb_get_level(struct misdn_jb *jb)
{
	return jb->state_buffer;
}

/*******************************************************/
/*************** JITTERBUFFER  END *********************/
/*******************************************************/

void chan_misdn_log(int level, int port, char *tmpl, ...)
{
	if (! ((0 <= port) && (port <= max_ports))) {
		cw_log(CW_LOG_WARNING, "cb_log called with out-of-range port number! (%d)\n", port);
		port=0;
		level=-1;
	}
		
	va_list ap;
	char buf[1024];
	char port_buf[8];
	sprintf(port_buf,"P[%2d] ",port);
	
	va_start(ap, tmpl);
	vsnprintf( buf, 1023, tmpl, ap );
	va_end(ap);

	if (level == -1)
		cw_log(CW_LOG_WARNING, buf);

	else if (misdn_debug_only[port] ? 
			(level==1 && misdn_debug[port]) || (level==misdn_debug[port]) 
		 : level <= misdn_debug[port]) {

		cw_log(CW_LOG_DEBUG, "%s%s", port_buf, buf);
	}
	
	if ((level <= misdn_debug[0]) && !cw_strlen_zero(global_tracefile) ) {
		time_t tm = time(NULL);
		char *tmp=ctime(&tm),*p;
		
		FILE *fp= fopen(global_tracefile, "a+");
		
		p=strchr(tmp,'\n');
		if (p) *p=':';

		if (!fp) {
			cw_log(CW_LOG_DEBUG, "Error opening Tracefile: [ %s ] %s\n", global_tracefile, strerror(errno));
			return ;
		}
		
		fputs(tmp,fp);
		fputs(" ", fp);
		fputs(port_buf,fp);
		fputs(" ", fp);
		fputs(buf, fp);

		fclose(fp);
	}
}
