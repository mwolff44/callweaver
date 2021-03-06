/*
 * (SCCP*)
 *
 * An implementation of Skinny Client Control Protocol (SCCP)
 *
 * Sergio Chersovani (mlists@c-net.it)
 *
 * Reworked, but based on chan_sccp code.
 * The original chan_sccp driver that was made by Zozo which itself was derived from the chan_skinny driver.
 * Modified by Jan Czmok and Julien Goodwin
 *
 * This program is free software and may be modified and
 * distributed under the terms of the GNU Public License.
 */
#include "chan_sccp.h"
#include "sccp_cli.h"
#include "sccp_indicate.h"
#include "sccp_utils.h"
#include "sccp_device.h"

#include "callweaver/utils.h"
#include "callweaver/cli.h"
#include "callweaver/callweaver_db.h"

/* ------------------------------------------------------------ */

static int sccp_reset_restart(struct cw_dynstr *ds_p, int argc, char * argv[]) {
  sccp_moo_t * r;
  sccp_device_t * d;

  if (argc != 3)
	return RESULT_SHOWUSAGE;

  d = sccp_device_find_byid(argv[2]);

  if (!d) {
	cw_dynstr_printf(ds_p, "Can't find device %s\n", argv[2]);
	return RESULT_SUCCESS;
  }

  REQ(r, Reset);
  r->msg.Reset.lel_resetType = htolel((!strcasecmp(argv[1], "reset")) ? SKINNY_DEVICE_RESET : SKINNY_DEVICE_RESTART);
  sccp_dev_send(d, r);

  cw_dynstr_printf(ds_p, "%s: Reset request sent to the device\n", argv[2]);
  return RESULT_SUCCESS;

}

/* ------------------------------------------------------------ */

static char *sccp_print_group(char *buf, int buflen, cw_group_t group) {
	unsigned int i;
	int first=1;
	char num[3];
	uint8_t max = (sizeof(cw_group_t) * 8) - 1;

	buf[0] = '\0';
	
	if (!group)
		return(buf);

	for (i=0; i<=max; i++) {
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

static int sccp_show_globals(struct cw_dynstr *ds_p, int argc, char * argv[])
{
	char pref_buf[128];
	char buf[256];
	char iabuf[INET_ADDRSTRLEN];

	CW_UNUSED(argc);
	CW_UNUSED(argv);

	cw_mutex_lock(&GLOB(lock));
	cw_codec_pref_string(&GLOB(global_codecs), pref_buf, sizeof(pref_buf) - 1);

	cw_dynstr_tprintf(ds_p, 20,
		cw_fmtval("SCCP channel driver global settings\n"),
		cw_fmtval("------------------------------------\n\n"),
#if SCCP_PLATFORM_BYTE_ORDER == SCCP_LITTLE_ENDIAN
		cw_fmtval("Platform byte order   : LITTLE ENDIAN\n"),
#else
		cw_fmtval("Platform byte order   : BIG ENDIAN\n"),
#endif
		cw_fmtval("Protocol Version      : %d\n", GLOB(protocolversion)),
		cw_fmtval("Server Name           : %s\n", GLOB(servername)),
		cw_fmtval("Bind Address          : %s:%d\n", cw_inet_ntoa(iabuf, sizeof(iabuf), GLOB(bindaddr.sin_addr)), ntohs(GLOB(bindaddr.sin_port))),
		cw_fmtval("Keepalive             : %d\n", GLOB(keepalive)),
		cw_fmtval("Debug level           : %d\n", GLOB(debug)),
		cw_fmtval("Date format           : %s\n", GLOB(date_format)),
		cw_fmtval("First digit timeout   : %d\n", GLOB(firstdigittimeout)),
		cw_fmtval("Digit timeout         : %d\n", GLOB(digittimeout)),
		cw_fmtval("RTP tos               : %d\n", GLOB(rtptos)),
		cw_fmtval("Context               : %s\n", GLOB(context)),
		cw_fmtval("Language              : %s\n", GLOB(language)),
		cw_fmtval("Accountcode           : %s\n", GLOB(accountcode)),
		cw_fmtval("Musicclass            : %s\n", GLOB(musicclass)),
		cw_fmtval("AMA flags             : %d - %s\n", GLOB(amaflags), cw_cdr_flags2str(GLOB(amaflags))),
		cw_fmtval("Callgroup             : %s\n", sccp_print_group(buf, sizeof(buf), GLOB(callgroup))),
#ifdef CS_SCCP_PICKUP
		cw_fmtval("Pickupgroup           : %s\n", sccp_print_group(buf, sizeof(buf), GLOB(pickupgroup))),
#else
		cw_fmtval(""),
#endif
		cw_fmtval("Capabilities          : ")
	);
	cw_getformatname_multiple(ds_p, GLOB(global_capability));
	cw_dynstr_tprintf(ds_p, 14,
		cw_fmtval("\n"),
		cw_fmtval("Codecs preference     : %s\n", pref_buf),
		cw_fmtval("DND                   : %s\n", GLOB(dndmode) ? sccp_dndmode2str(GLOB(dndmode)) : "Disabled"),
#ifdef CS_SCCP_PARK
		cw_fmtval("Park                  : Enabled\n"),
#else
		cw_fmtval("Park                  : Disabled\n"),
#endif
		cw_fmtval("Private softkey       : %s\n", GLOB(private) ? "Enabled" : "Disabled"),
		cw_fmtval("Echo cancel           : %s\n", GLOB(echocancel) ? "Enabled" : "Disabled"),
		cw_fmtval("Silence suppression   : %s\n", GLOB(silencesuppression) ? "Enabled" : "Disabled"),
		cw_fmtval("Trust phone ip        : %s\n", GLOB(trustphoneip) ? "Yes" : "No"),
		cw_fmtval("Early RTP             : %s\n", GLOB(earlyrtp) ? "Yes" : "No"),
		cw_fmtval("AutoAnswer ringtime   : %d\n", GLOB(autoanswer_ring_time)),
		cw_fmtval("AutoAnswer tone       : %d\n", GLOB(autoanswer_tone)),
		cw_fmtval("RemoteHangup tone     : %d\n", GLOB(remotehangup_tone)),
		cw_fmtval("Transfer tone         : %d\n", GLOB(transfer_tone)),
		cw_fmtval("CallWaiting tone      : %d\n", GLOB(callwaiting_tone))
	);
	cw_mutex_unlock(&GLOB(lock));
	return RESULT_SUCCESS;
}

static struct cw_clicmd cli_show_globals = {
	.cmda = { "sccp", "show", "globals", NULL },
	.handler = sccp_show_globals,
	.summary = "Show SCCP global settings",
	.usage = "Usage: sccp show globals\n",
};

/* ------------------------------------------------------------ */

static int sccp_show_device(struct cw_dynstr *ds_p, int argc, char * argv[]) {
	sccp_device_t * d;
	sccp_speed_t * k;
	sccp_line_t * l;
	char pref_buf[128];

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	d = sccp_device_find_byid(argv[3]);
	if (!d) {
		cw_dynstr_printf(ds_p, "Can't find settings for device %s\n", argv[3]);
		return RESULT_SUCCESS;
	}
	cw_mutex_lock(&d->lock);
	cw_codec_pref_string(&d->codecs, pref_buf, sizeof(pref_buf) - 1);

	cw_dynstr_tprintf(ds_p, 15,
		cw_fmtval("Current settings for selected Device\n"),
		cw_fmtval("------------------------------------\n\n"),
		cw_fmtval("MAC-Address        : %s\n", d->id),
		cw_fmtval("Protocol Version   : phone=%d, channel=%d\n", d->protocolversion, GLOB(protocolversion)),
		cw_fmtval("Registration state : %s(%d)\n", skinny_registrationstate2str(d->registrationState), d->registrationState),
		cw_fmtval("State              : %s(%d)\n", skinny_devicestate2str(d->state), d->state),
		cw_fmtval("MWI handset light  : %s\n", (d->mwilight ? "ON" : "OFF")),
		cw_fmtval("Description        : %s\n", d->description),
		cw_fmtval("Config Phone Type  : %s\n", d->config_type),
		cw_fmtval("Skinny Phone Type  : %s(%d)\n", skinny_devicetype2str(d->skinny_type), d->skinny_type),
		cw_fmtval("Softkey support    : %s\n", (d->softkeysupport ? "Yes" : "No")),
		cw_fmtval("Autologin          : %s\n", d->autologin),
		cw_fmtval("Image Version      : %s\n", d->imageversion),
		cw_fmtval("Timezone Offset    : %d\n", d->tz_offset),
		cw_fmtval("Capabilities       : ")
	);
	cw_getformatname_multiple(ds_p, d->capability);

	cw_dynstr_tprintf(ds_p, 11,
		cw_fmtval("\n"),
		cw_fmtval("Codecs preference  : %s\n", pref_buf),
		cw_fmtval("Can DND            : %s\n", (d->dndmode ? sccp_dndmode2str(d->dndmode) : "Disabled")),
		cw_fmtval("Can Transfer       : %s\n", (d->transfer ? "Yes" : "No")),
		cw_fmtval("Can Park           : %s\n", (d->park ? "Yes" : "No")),
		cw_fmtval("Private softkey    : %s\n", (d->private ? "Enabled" : "Disabled")),
		cw_fmtval("Can CFWDALL        : %s\n", (d->cfwdall ? "Yes" : "No")),
		cw_fmtval("Can CFWBUSY        : %s\n", (d->cfwdbusy ? "Yes" : "No")),
		cw_fmtval("Dtmf mode          : %s\n", (d->dtmfmode ? "Out-of-Band" : "In-Band")),
		cw_fmtval("Trust phone ip     : %s\n", (d->trustphoneip ? "Yes" : "No")),
		cw_fmtval("Early RTP          : %s\n", (d->earlyrtp ? "Yes" : "No"))
	);

	l = d->lines;
	if (l) {
		cw_dynstr_printf(ds_p,
			"\nLines\n"
			"%-4s: %-20s %-20s\n", "id", "name" , "label"
			"------------------------------------\n"
		);
		while (l) {
			cw_dynstr_printf(ds_p, "%4d: %-20s %-20s\n", l->instance, l->name , l->label);
			l = l->next_on_device;
		}
	}
	k = d->speed_dials;
	if (k) {
		cw_dynstr_printf(ds_p,
			"\nSpeedials\n"
			"%-4s: %-20s %-20s\n", "id", "name" , "number"
			"------------------------------------\n"
		);
		while (k) {
			cw_dynstr_printf(ds_p, "%4d: %-20s %-20s\n", k->instance, k->name , k->ext);
			k = k->next;
		}
	}
	cw_mutex_unlock(&d->lock);
	return RESULT_SUCCESS;
}

static struct cw_clicmd cli_show_device = {
	.cmda = { "sccp", "show", "device", NULL },
	.handler = sccp_show_device,
	.summary = "Show SCCP Device Information",
	.usage = "Usage: sccp show device <deviceId>\n",
};

/* ------------------------------------------------------------ */


static struct cw_clicmd cli_reset = {
	.cmda = { "sccp", "reset", NULL },
	.handler = sccp_reset_restart,
	.summary = "Reset an SCCP device",
	.usage = "Usage: sccp reset <deviceId>\n",
};

static struct cw_clicmd cli_restart = {
	.cmda = { "sccp", "restart", NULL },
	.handler = sccp_reset_restart,
	.summary = "Reset an SCCP device",
	.usage = "Usage: sccp restart <deviceId>\n",
};

/* ------------------------------------------------------------ */

static int sccp_show_channels(struct cw_dynstr *ds_p, int argc, char * argv[])
{
	sccp_channel_t * c;

	CW_UNUSED(argc);
	CW_UNUSED(argv);

	cw_dynstr_printf(ds_p,
		"\n%-5s %-10s %-16s %-16s %-16s %-10s\n", "ID","LINE","DEVICE","AST STATE","SCCP STATE","CALLED"
		"===== ========== ================ ================ ================ ========== \n"
	);

	cw_mutex_lock(&GLOB(channels_lock));
	c = GLOB(channels);
	while(c) {
		cw_dynstr_printf(ds_p, "%.5d %-10s %-16s %-16s %-16s %-10s\n",
			c->callid,
			c->line->name,
			c->line->device->description,
			(c->owner) ? cw_state2str(c->owner->_state) : "No channel",
			sccp_indicate2str(c->state),
			c->calledPartyNumber);
		c = c->next;
	}
	cw_mutex_unlock(&GLOB(channels_lock));
	return RESULT_SUCCESS;
}

static struct cw_clicmd cli_show_channels = {
	.cmda = { "sccp", "show", "channels", NULL },
	.handler = sccp_show_channels,
	.summary = "Show all SCCP channels",
	.usage = "Usage: sccp show channel\n",
};

/* ------------------------------------------------------------ */

static int sccp_show_devices(struct cw_dynstr *ds_p, int argc, char * argv[])
{
	char iabuf[INET_ADDRSTRLEN];
	sccp_device_t * d;

	CW_UNUSED(argc);
	CW_UNUSED(argv);

	cw_dynstr_printf(ds_p,
		"\n%-16s %-15s %-16s %-10s\n", "NAME","ADDRESS","MAC","Reg. State"
		"================ =============== ================ ==========\n"
	);

	cw_mutex_lock(&GLOB(devices_lock));
	d = GLOB(devices);
	while (d) {
		cw_dynstr_printf(ds_p, "%-16s %-15s %-16s %-10s\n",// %-10s %-16s %c%c %-10s\n",
			d->description,
			(d->session) ? cw_inet_ntoa(iabuf, sizeof(iabuf), d->session->sin.sin_addr) : "--",
			d->id,
			skinny_registrationstate2str(d->registrationState)
		);
		d = d->next;
	}
	cw_mutex_unlock(&GLOB(devices_lock));
	return RESULT_SUCCESS;
}

static struct cw_clicmd cli_show_devices = {
	.cmda = { "sccp", "show", "devices", NULL },
	.handler = sccp_show_devices,
	.summary = "Show all SCCP Devices",
	.usage = "Usage: sccp show devices\n",
};

static int sccp_message_devices(struct cw_dynstr *ds_p, int argc, char * argv[])
{
	sccp_device_t * d;
	int msgtimeout=10;

	CW_UNUSED(ds_p);

	if (argc < 4)
		return RESULT_SHOWUSAGE;
		
	if (cw_strlen_zero(argv[3]))
		return RESULT_SHOWUSAGE;
		
	if (argc == 5 && sscanf(argv[4], "%d", &msgtimeout) != 1) {
		msgtimeout=10;
	}

	cw_mutex_lock(&GLOB(devices_lock));
	d = GLOB(devices);
	while (d) {
		sccp_dev_displaynotify(d,argv[3],msgtimeout);
		d = d->next;
	}
	cw_mutex_unlock(&GLOB(devices_lock));
	return RESULT_SUCCESS;
}

static struct cw_clicmd cli_message_devices = {
  .cmda = { "sccp", "message", "devices", NULL },
  .handler = sccp_message_devices,
  .summary = "Send a message to all SCCP Devices",
  .usage = "Usage: sccp messages devices <message text> <timeout>\n",
};



/* ------------------------------------------------------------ */

static int sccp_show_lines(struct cw_dynstr *ds_p, int argc, char * argv[])
{
	struct cw_dynstr caps = CW_DYNSTR_INIT;
	sccp_line_t * l = NULL;
	sccp_channel_t * c = NULL;
	sccp_device_t * d = NULL;

	CW_UNUSED(argc);
	CW_UNUSED(argv);

	cw_dynstr_printf(ds_p,
		"\n%-16s %-16s %-4s %-4s %-16s\n", "NAME","DEVICE","MWI","Chs","Active Channel"
		"================ ================ ==== ==== =================================================\n"
	);

	cw_mutex_lock(&GLOB(lines_lock));
	l = GLOB(lines);

	while (l) {
		cw_mutex_lock(&l->lock);
		c = NULL;
		d = l->device;
		if (d) {
			cw_mutex_lock(&d->lock);
			c = d->active_channel;
			cw_mutex_unlock(&d->lock);
		}

		if (!c || (c->line != l))
			c = NULL;
		if (c && c->owner)
			cw_getformatname_multiple(&caps,  c->owner->nativeformats);

		cw_dynstr_printf(ds_p, "%-16s %-16s %-4s %-4d %-10s %-10s %-16s %-10s\n",
			l->name,
			(l->device) ? l->device->id : "--",
			(l->mwilight) ? "ON" : "OFF",
			l->channelCount,
			(c) ? sccp_indicate2str(c->state) : "--",
			(c) ? skinny_calltype2str(c->calltype) : "",
			(c) ? ( (c->calltype == SKINNY_CALLTYPE_OUTBOUND) ? c->calledPartyName : c->callingPartyName ) : "",
			(caps.used ? caps.data : ""));

		cw_mutex_unlock(&l->lock);
		l = l->next;
	}

	cw_mutex_unlock(&GLOB(lines_lock));
	cw_dynstr_free(&caps);
	return RESULT_SUCCESS;
}

static struct cw_clicmd cli_show_lines = {
  .cmda = { "sccp", "show", "lines", NULL },
  .handler = sccp_show_lines,
  .summary = "Show All SCCP Lines",
  .usage = "Usage: sccp show lines\n",
};

/* ------------------------------------------------------------ */

static int sccp_show_sessions(struct cw_dynstr *ds_p, int argc, char * argv[])
{
	char iabuf[INET_ADDRSTRLEN];
	sccp_session_t * s = NULL;
	sccp_device_t * d = NULL;

	CW_UNUSED(argc);
	CW_UNUSED(argv);

	cw_dynstr_printf(ds_p,
		"%-10s %-15s %-4s %-15s %-15s %-15s\n", "Socket", "IP", "KA", "DEVICE", "STATE", "TYPE"
		"========== =============== ==== =============== =============== ===============\n"
	);

	cw_mutex_lock(&GLOB(sessions_lock));
	s = GLOB(sessions);

	while (s) {
		cw_mutex_lock(&s->lock);
		d = s->device;
		if (d)
			cw_mutex_lock(&d->lock);
		cw_dynstr_printf(ds_p, "%-10d %-15s %-4d %-15s %-15s %-15s\n",
			s->fd,
			cw_inet_ntoa(iabuf, sizeof(iabuf), s->sin.sin_addr),
			(uint32_t)(time(0) - s->lastKeepAlive),
			(d) ? d->id : "--",
			(d) ? skinny_devicestate2str(d->state) : "--",
			(d) ? skinny_devicetype2str(d->skinny_type) : "--");
		if (d)
			cw_mutex_unlock(&d->lock);
		cw_mutex_unlock(&s->lock);
		s = s->next;
	}
	cw_mutex_unlock(&GLOB(sessions_lock));
	return RESULT_SUCCESS;
}

static struct cw_clicmd cli_show_sessions = {
  .cmda = { "sccp", "show", "sessions", NULL },
  .handler = sccp_show_sessions,
  .summary = "Show All SCCP Sessions",
  .usage = "Usage: sccp show sessions\n"
};

/* ------------------------------------------------------------ */
static int sccp_system_message(struct cw_dynstr *ds_p, int argc, char * argv[]) {
	int res;
	int timeout = 0;
	if ((argc < 3) || (argc > 5))
		return RESULT_SHOWUSAGE;

	if (argc == 3) {
		res = cw_db_deltree("SCCP", "message");
		if (res) {
			cw_dynstr_printf(ds_p, "Failed to delete the SCCP system message!\n");
			return RESULT_FAILURE;
		}
		cw_dynstr_printf(ds_p, "SCCP system message deleted!\n");
		return RESULT_SUCCESS;
	}

	if (cw_strlen_zero(argv[3]))
		return RESULT_SHOWUSAGE;

	res = cw_db_put("SCCP/message", "text", argv[3]);
	if (res) {
		cw_dynstr_printf(ds_p, "Failed to store the SCCP system message text\n");
	} else {
		cw_dynstr_printf(ds_p, "SCCP system message text stored successfully\n");
	}
	if (argc == 5) {
		if (sscanf(argv[4], "%d", &timeout) != 1)
			return RESULT_SHOWUSAGE;
		res = cw_db_put("SCCP/message", "timeout", argv[4]);
		if (res) {
			cw_dynstr_printf(ds_p, "Failed to store the SCCP system message timeout\n");
		} else {
			cw_dynstr_printf(ds_p, "SCCP system message timeout stored successfully\n");
		}
	} else {
		cw_db_del("SCCP/message", "timeout");
	}
	return RESULT_SUCCESS;
}

static struct cw_clicmd cli_system_message = {
   .cmda = { "sccp", "system", "message", NULL },
   .handler = sccp_system_message,
   .summary = "Set the SCCP system message",
   .usage = "Usage: sccp system message \"<message text>\" <timeout>\nThe default optional timeout is 0 (forever)\nExample: sccp system message \"The boss is gone. Let's have some fun!\"  10\n"
};

/* ------------------------------------------------------------ */

static const char debug_usage[] =
"Usage: SCCP debug <level>\n"
"		Set the debug level of the sccp protocol from none (0) to high (10)\n";

static const char no_debug_usage[] =
"Usage: SCCP no debug\n"
"		Disables dumping of SCCP packets for debugging purposes\n";

static int sccp_do_debug(struct cw_dynstr *ds_p, int argc, char *argv[]) {
	int new_debug = 10;

	if ((argc < 2) || (argc > 3))
		return RESULT_SHOWUSAGE;

	if (argc == 3) {
		if (sscanf(argv[2], "%d", &new_debug) != 1)
			return RESULT_SHOWUSAGE;
		new_debug = (new_debug > 10) ? 10 : new_debug;
		new_debug = (new_debug < 0) ? 0 : new_debug;
	}

	cw_dynstr_printf(ds_p, "SCCP debug level was %d now %d\n", GLOB(debug), new_debug);
	GLOB(debug) = new_debug;
	return RESULT_SUCCESS;
}

static struct cw_clicmd cli_do_debug = {
  .cmda = { "sccp", "debug", NULL },
  .handler = sccp_do_debug,
  .summary = "Enable SCCP debugging",
  .usage = debug_usage,
};

static int sccp_no_debug(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	CW_UNUSED(argv);

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	GLOB(debug) = 0;
	cw_dynstr_printf(ds_p, "SCCP Debugging Disabled\n");
	return RESULT_SUCCESS;
}

static struct cw_clicmd cli_no_debug = {
  .cmda = { "sccp", "no", "debug", NULL },
  .handler = sccp_no_debug,
  .summary = "Disable SCCP debugging",
  .usage = no_debug_usage,
};

static int sccp_do_reload(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	cw_dynstr_printf(ds_p, "SCCP configuration reload not implemented yet! use unload and load.\n");
	return RESULT_SUCCESS;
}

static const char reload_usage[] =
"Usage: sccp reload\n"
"		Reloads SCCP configuration from sccp.conf (It will close all active connections)\n";

static struct cw_clicmd cli_reload = {
  .cmda = { "sccp", "reload", NULL },
  .handler = sccp_do_reload,
  .summary = "SCCP module reload",
  .usage = reload_usage,
};

static const char version_usage[] =
"Usage: SCCP show version\n"
"		Show the SCCP channel version\n";

static int sccp_show_version(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	cw_dynstr_printf(ds_p, "SCCP channel version: %s\n", SCCP_VERSION);
	return RESULT_SUCCESS;
}

static struct cw_clicmd cli_show_version = {
  .cmda = { "sccp", "show", "version", NULL },
  .handler = sccp_show_version,
  .summary = "SCCP show version",
  .usage = version_usage,
};

void sccp_register_cli(void) {
  cw_cli_register(&cli_show_channels);
  cw_cli_register(&cli_show_devices);
  cw_cli_register(&cli_show_lines);
  cw_cli_register(&cli_show_sessions);
  cw_cli_register(&cli_show_device);
  cw_cli_register(&cli_show_version);
  cw_cli_register(&cli_reload);
  cw_cli_register(&cli_restart);
  cw_cli_register(&cli_reset);
  cw_cli_register(&cli_do_debug);
  cw_cli_register(&cli_no_debug);
  cw_cli_register(&cli_system_message);
  cw_cli_register(&cli_show_globals);
  cw_cli_register(&cli_message_devices);

}

void sccp_unregister_cli(void) {
  cw_cli_unregister(&cli_show_channels);
  cw_cli_unregister(&cli_show_devices);
  cw_cli_unregister(&cli_show_lines);
  cw_cli_unregister(&cli_show_sessions);
  cw_cli_unregister(&cli_show_device);
  cw_cli_unregister(&cli_show_version);
  cw_cli_unregister(&cli_reload);
  cw_cli_unregister(&cli_restart);
  cw_cli_unregister(&cli_reset);
  cw_cli_unregister(&cli_do_debug);
  cw_cli_unregister(&cli_no_debug);
  cw_cli_unregister(&cli_system_message);
  cw_cli_unregister(&cli_show_globals);
  cw_cli_unregister(&cli_message_devices);
}
