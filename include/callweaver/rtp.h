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
 *
 * $HeadURL$
 * $Revision$
 */

/*
 * \file rtp.h
 * \brief Supports RTP and RTCP with Symmetric RTP support for NAT traversal.
 *
 * RTP is defined in RFC 3550.
 *
 */

#ifndef _CALLWEAVER_RTP_H
#define _CALLWEAVER_RTP_H

#include <netinet/in.h>

#include <callweaver/udp.h>
#include "callweaver/frame.h"
#include "callweaver/channel.h"


/* Codes for RTP-specific data - not defined by our CW_FORMAT codes */
/*! DTMF (RFC2833) */
#define CW_RTP_DTMF            (1 << 0)
/*! 'Comfort Noise' (RFC3389) */
#define CW_RTP_CN              (1 << 1)
/*! DTMF (Cisco Proprietary) */
#define CW_RTP_CISCO_DTMF      (1 << 2)
/*! Maximum RTP-specific code */
#define CW_RTP_MAX             CW_RTP_CISCO_DTMF

#define MAX_RTP_PT 256

struct cw_rtp_protocol
{
	/* Get RTP struct, or NULL if unwilling to transfer */
	struct cw_rtp *(* const get_rtp_info)(struct cw_channel *chan);
	/* Get RTP struct, or NULL if unwilling to transfer */
	struct cw_rtp *(* const get_vrtp_info)(struct cw_channel *chan);
	/* Set RTP peer */
	int (* const set_rtp_peer)(struct cw_channel *chan, struct cw_rtp *peer, struct cw_rtp *vpeer, int codecs, int nat_active);
	int (* const get_codec)(struct cw_channel *chan);
	const char * const type;
	struct cw_rtp_protocol *next;
};

/* The value of each payload format mapping: */
struct rtpPayloadType
{
	int is_cw_format; 	/* whether the following code is an CW_FORMAT */
	int code;
};

struct cw_srtp;

struct cw_rtp
{
	udp_state_t sock_info[2];
	struct cw_frame f;
	uint8_t rawdata[8192 + CW_FRIENDLY_OFFSET];
	uint32_t ssrc;
	uint32_t lastts;
	uint32_t lastrxts;
	uint32_t lastividtimestamp;
	uint32_t lastovidtimestamp;
	uint32_t lastevent_seqno;
	uint32_t lastevent_startts;
	uint16_t lastevent_duration;
	char lastevent_code;
	int lasttxformat;
	int lastrxformat;
	int sendevent;
	uint32_t sendevent_startts;
	uint32_t sendevent_rtphdr;
	uint32_t sendevent_payload;
	uint32_t sendevent_duration;
	uint16_t sendevent_seqno;
	int nat;
	unsigned int bug_sonus:1;
	unsigned int warn_3389:1;
	unsigned int nat_state:3;
	int framems;
	int rtplen;
	struct timeval rxcore;
	struct timeval txcore;
	struct timeval dtmfmute;
	struct cw_smoother *smoother;
	uint16_t seqno;
	uint16_t rxseqno;
	struct rtpPayloadType current_RTP_PT[MAX_RTP_PT];
	int rtp_lookup_code_cache_is_cw_format;	/* a cache for the result of rtp_lookup_code(): */
	int rtp_lookup_code_cache_code;
	int rtp_lookup_code_cache_result;
	int rtp_offered_from_local;
#ifdef ENABLE_SRTP
	struct cw_srtp *srtp;
#endif
};


extern CW_API_PUBLIC int cw_rtp_fd(struct cw_rtp *rtp) __attribute__ (( __nonnull__ (1) ));

extern CW_API_PUBLIC int cw_rtcp_fd(struct cw_rtp *rtp) __attribute__ (( __nonnull__ (1) ));

extern CW_API_PUBLIC struct sockaddr *cw_rtp_get_peer(struct cw_rtp *rtp) __attribute__ (( __nonnull__ (1) ));

extern CW_API_PUBLIC struct cw_rtp *cw_rtp_new_with_bindaddr(struct sockaddr *addr);

extern CW_API_PUBLIC void cw_rtp_set_peer(struct cw_rtp *rtp, struct sockaddr *them);

extern CW_API_PUBLIC struct sockaddr *cw_rtp_get_us(struct cw_rtp *rtp);

extern CW_API_PUBLIC void cw_rtp_destroy(struct cw_rtp *rtp);

extern CW_API_PUBLIC void cw_rtp_reset(struct cw_rtp *rtp);

extern CW_API_PUBLIC int cw_rtp_write(struct cw_rtp *rtp, struct cw_frame *f);

extern CW_API_PUBLIC struct cw_frame *cw_rtp_read(struct cw_rtp *rtp);

extern CW_API_PUBLIC struct cw_frame *cw_rtcp_read(struct cw_channel *chan, struct cw_rtp *rtp);

extern CW_API_PUBLIC int cw_rtcp_fd(struct cw_rtp *rtp);

extern CW_API_PUBLIC int cw_rtp_sendevent(struct cw_rtp *const rtp, char event, uint16_t duration);

extern CW_API_PUBLIC int cw_rtp_sendcng(struct cw_rtp *rtp, int level);

extern CW_API_PUBLIC int cw_rtp_settos(struct cw_rtp *rtp, int tos);

/*  Setting RTP payload types from lines in a SDP description: */
extern CW_API_PUBLIC void cw_rtp_pt_clear(struct cw_rtp* rtp);
/* Set payload types to defaults */
extern CW_API_PUBLIC void cw_rtp_pt_default(struct cw_rtp* rtp);
extern CW_API_PUBLIC void cw_rtp_set_m_type(struct cw_rtp* rtp, int pt);
extern CW_API_PUBLIC void cw_rtp_set_rtpmap_type(struct cw_rtp* rtp, int pt, const char *mimeType, const char *mimeSubtype);

/*  Mapping between RTP payload format codes and CallWeaver codes: */
extern CW_API_PUBLIC struct rtpPayloadType cw_rtp_lookup_pt(struct cw_rtp* rtp, int pt);
extern CW_API_PUBLIC int cw_rtp_lookup_code(struct cw_rtp* rtp, int is_cw_format, int code);
extern CW_API_PUBLIC void cw_rtp_offered_from_local(struct cw_rtp* rtp, int local);

extern CW_API_PUBLIC void cw_rtp_get_current_formats(struct cw_rtp* rtp,
			     int* cw_formats, int* non_cw_formats);

/*  Mapping an CallWeaver code into a MIME subtype (string): */
extern CW_API_PUBLIC const char* cw_rtp_lookup_mime_subtype(int is_cw_format, int code);

/* Build a string of MIME subtype names from a capability list */
extern CW_API_PUBLIC void cw_rtp_lookup_mime_multiple(struct cw_dynstr *result, const int capability, const int is_cw_format);

extern CW_API_PUBLIC void cw_rtp_setnat(struct cw_rtp *rtp, int nat);

extern CW_API_PUBLIC enum cw_bridge_result cw_rtp_bridge(struct cw_channel *c0, struct cw_channel *c1, int flags, struct cw_frame **fo, struct cw_channel **rc, int timeoutms);

extern CW_API_PUBLIC int cw_rtp_proto_register(struct cw_rtp_protocol *proto);

extern CW_API_PUBLIC void cw_rtp_proto_unregister(struct cw_rtp_protocol *proto);

extern CW_API_PUBLIC void cw_rtp_stop(struct cw_rtp *rtp);

int cw_rtp_init(void);

void cw_rtp_reload(void);

extern CW_API_PUBLIC int cw_rtp_set_framems(struct cw_rtp *rtp, int ms);


struct cw_srtp_policy;

struct cw_srtp_cb {
	int (*no_ctx)(struct cw_rtp *rtp, unsigned long ssrc, void *data);
};

struct cw_srtp_res {
	int (*create)(struct cw_srtp **srtp, struct cw_rtp *rtp,
		      struct cw_srtp_policy *policy);
	void (*destroy)(struct cw_srtp *srtp);
	int (*add_stream)(struct cw_srtp *srtp, struct cw_srtp_policy *policy);
	void (*set_cb)(struct cw_srtp *srtp,
		       const struct cw_srtp_cb *cb, void *data);
	int (*unprotect)(struct cw_srtp *srtp, void *buf, int *size);
	int (*protect)(struct cw_srtp *srtp, void **buf, size_t *size);
	int (*get_random)(unsigned char *key, size_t len);
};

/* Crypto suites */
enum cw_srtp_suite {
	CW_AES_CM_128_HMAC_SHA1_80 = 1,
	CW_AES_CM_128_HMAC_SHA1_32 = 2,
	CW_F8_128_HMAC_SHA1_80     = 3
};

enum cw_srtp_ealg {
	CW_MIKEY_SRTP_EALG_NULL    = 0,
	CW_MIKEY_SRTP_EALG_AESCM   = 1
};

enum cw_srtp_aalg {
	CW_MIKEY_SRTP_AALG_NULL     = 0,
	CW_MIKEY_SRTP_AALG_SHA1HMAC = 1
};

struct cw_srtp_policy_res {
	struct cw_srtp_policy *(*alloc)(void);
	void (*destroy)(struct cw_srtp_policy *policy);
	int (*set_suite)(struct cw_srtp_policy *policy,
			 enum cw_srtp_suite suite);
	int (*set_master_key)(struct cw_srtp_policy *policy,
			      const unsigned char *key, size_t key_len,
			      const unsigned char *salt, size_t salt_len);
	int (*set_encr_alg)(struct cw_srtp_policy *policy,
			    enum cw_srtp_ealg  ealg);
	int (*set_auth_alg)(struct cw_srtp_policy *policy,
			    enum cw_srtp_aalg aalg);
	void (*set_encr_keylen)(struct cw_srtp_policy *policy, int ekeyl);
	void (*set_auth_keylen)(struct cw_srtp_policy *policy, int akeyl);
	void (*set_srtp_auth_taglen)(struct cw_srtp_policy *policy, int autht);
	void (*set_srtp_encr_enable)(struct cw_srtp_policy *policy, int enable);
	void (*set_srtcp_encr_enable)(struct cw_srtp_policy *policy, int enable);
	void (*set_srtp_auth_enable)(struct cw_srtp_policy *policy, int enable);
	void (*set_ssrc)(struct cw_srtp_policy *policy, unsigned long ssrc,
			 int inbound);
};


#ifdef ENABLE_SRTP
extern CW_API_PUBLIC int cw_rtp_register_srtp(struct cw_srtp_res *srtp_res, struct cw_srtp_policy_res *policy_res);

extern CW_API_PUBLIC int cw_rtp_unregister_srtp(struct cw_srtp_res *srtp_res, struct cw_srtp_policy_res *policy_res);

extern CW_API_PUBLIC int cw_srtp_is_registered(void);

extern CW_API_PUBLIC unsigned int cw_rtp_get_ssrc(struct cw_rtp *rtp);
extern CW_API_PUBLIC void cw_rtp_set_srtp_cb(struct cw_rtp *rtp, const struct cw_srtp_cb *cb, void *data);
extern CW_API_PUBLIC int cw_rtp_add_srtp_policy(struct cw_rtp *rtp, struct cw_srtp_policy *policy);
extern CW_API_PUBLIC struct cw_srtp_policy *cw_srtp_policy_alloc(void);
extern CW_API_PUBLIC int cw_srtp_policy_set_suite(struct cw_srtp_policy *policy, enum cw_srtp_suite suite);
extern CW_API_PUBLIC int cw_srtp_policy_set_master_key(struct cw_srtp_policy *policy, const unsigned char *key, size_t key_len, const unsigned char *salt, size_t salt_len);
extern CW_API_PUBLIC int cw_srtp_policy_set_encr_alg(struct cw_srtp_policy *policy, enum cw_srtp_ealg ealg);
extern CW_API_PUBLIC int cw_srtp_policy_set_auth_alg(struct cw_srtp_policy *policy, enum cw_srtp_aalg aalg);
extern CW_API_PUBLIC void cw_srtp_policy_set_encr_keylen(struct cw_srtp_policy *policy, int ekeyl);
extern CW_API_PUBLIC void cw_srtp_policy_set_auth_keylen(struct cw_srtp_policy *policy, int akeyl);
extern CW_API_PUBLIC void cw_srtp_policy_set_srtp_auth_taglen(struct cw_srtp_policy *policy, int autht);
extern CW_API_PUBLIC void cw_srtp_policy_set_srtp_encr_enable(struct cw_srtp_policy *policy, int enable);
extern CW_API_PUBLIC void cw_srtp_policy_set_srtcp_encr_enable(struct cw_srtp_policy *policy, int enable);
extern CW_API_PUBLIC void cw_srtp_policy_set_srtp_auth_enable(struct cw_srtp_policy *policy, int enable);
extern CW_API_PUBLIC void cw_srtp_policy_set_ssrc(struct cw_srtp_policy *policy, unsigned long ssrc, int inbound);

extern CW_API_PUBLIC void cw_srtp_policy_destroy(struct cw_srtp_policy *policy);
extern CW_API_PUBLIC int cw_srtp_get_random(unsigned char *key, size_t len);
#endif

#endif /* _CALLWEAVER_RTP_H */
