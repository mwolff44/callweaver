/*
 * vISDN channel driver for Asterisk
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _CHAN_VISDN_H
#define _CHAN_VISDN_H

#include <callweaver/channel.h>
#include <libq931/list.h>

#include "intf.h"

#ifndef CW_CONTROL_INBAND_INFO
#define CW_CONTROL_INBAND_INFO 42
#endif

#ifndef CW_CONTROL_DISCONNECT
#define CW_CONTROL_DISCONNECT 43
#endif

#define VISDN_DESCRIPTION "VISDN Channel Driver for CallWeaver"
#define VISDN_CHAN_TYPE "VISDN"
#define VISDN_CONFIG_FILE "chan_visdn.conf"

enum poll_info_type
{
	POLL_INFO_TYPE_MGMT,
	POLL_INFO_TYPE_ACCEPT,
	POLL_INFO_TYPE_BC_DLC,
	POLL_INFO_TYPE_DLC,
	POLL_INFO_TYPE_NETLINK,
	POLL_INFO_TYPE_CCB_Q931,
	POLL_INFO_TYPE_Q931_CCB,
};

struct poll_info
{
	enum poll_info_type type;
	struct visdn_intf *intf;
	struct q931_dlc *dlc;
};

struct visdn_suspended_call
{
	struct list_head node;

	struct cw_channel *cw_chan;
	struct q931_channel *q931_chan;

	char call_identity[10];
	int call_identity_len;

	time_t old_when_to_hangup;
};

struct visdn_chan {
	struct cw_channel *cw_chan;
	struct q931_call *q931_call;
	struct visdn_suspended_call *suspended_call;

	__u8 buf[512];

	int is_voice;
	int handle_stream;

	int sp_fd;
	int ec_fd;

	int sp_channel_id;
	int ec_ne_channel_id;
	int ec_fe_channel_id;
	int bearer_channel_id;

	int sp_pipeline_id;
	int bearer_pipeline_id;

	int sending_complete;

	int channel_has_been_connected;
	int inband_info;

	char number[32];
	int sent_digits;

	char options[16];

	char dtmf_queue[20];
	int dtmf_deferred;

	struct cw_dsp *dsp;

	struct visdn_ic *ic;

	struct visdn_huntgroup *huntgroup;
	struct visdn_intf *hg_first_intf;
};

struct visdn_state
{
	cw_mutex_t lock;
	cw_mutex_t usecnt_lock;

	int have_to_exit;

	struct list_head ccb_q931_queue;
	cw_mutex_t ccb_q931_queue_lock;
	int ccb_q931_queue_pipe_read;
	int ccb_q931_queue_pipe_write;

	struct list_head q931_ccb_queue;
	cw_mutex_t q931_ccb_queue_lock;
	int q931_ccb_queue_pipe_read;
	int q931_ccb_queue_pipe_write;

	struct list_head ifs;
	struct list_head huntgroups_list;

	struct pollfd polls[100];
	struct poll_info poll_infos[100];
	int npolls;

	int open_pending;
	int open_pending_nextcheck;

	int usecnt;
	int netlink_socket;

	int router_control_fd;

	int debug;
	int debug_q931;
	int debug_q921;

	struct visdn_ic *default_ic;
};

extern struct visdn_state visdn;

void refresh_polls_list();

static inline struct visdn_chan *to_visdn_chan(struct cw_channel *cw_chan)
{
	return cw_chan->tech_pvt;
}

static inline struct cw_channel *callpvt_to_cwchan(
	struct q931_call *call)
{
	return (struct cw_channel *)call->pvt;
}

#endif
