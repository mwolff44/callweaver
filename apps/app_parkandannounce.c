/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Author: Ben Miller <bgmiller@dccinc.com>
 *    With TONS of help from Mark!
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
 * ParkAndAnnounce application for OpenPBX
 * 
 */

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision$")

#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/module.h"
#include "openpbx/features.h"
#include "openpbx/options.h"
#include "openpbx/logger.h"
#include "openpbx/say.h"
#include "openpbx/lock.h"

static char *tdesc = "Call Parking and Announce Application";

static char *app = "ParkAndAnnounce";

static char *synopsis = "Park and Announce";

static char *descrip =
"  ParkAndAnnounce(announce:template|timeout|dial|[return_context]):\n"
"Park a call into the parkinglot and announce the call over the console.\n"
"announce template: colon separated list of files to announce, the word PARKED\n"
"                   will be replaced by a say_digits of the ext the call is parked in\n"
"timeout: time in seconds before the call returns into the return context.\n"
"dial: The app_dial style resource to call to make the announcement. Console/dsp calls the console.\n"
"return_context: the goto style label to jump the call back into after timeout. default=prio+1\n";


STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int parkandannounce_exec(struct opbx_channel *chan, void *data)
{
	int res=0;
	char *return_context;
	int l, lot, timeout = 0, dres;
	char *working, *context, *exten, *priority, *dial, *dialtech, *dialstr;
	char *template, *tpl_working, *tpl_current;
	char *tmp[100];
	int looptemp=0,i=0;
	char *s,*orig_s;

	struct opbx_channel *dchan;
	int outstate;

	struct localuser *u;
	if(!data || (data && !strlen(data))) {
		opbx_log(LOG_WARNING, "ParkAndAnnounce requires arguments: (announce:template|timeout|dial|[return_context])\n");
		return -1;
	}
  
	l=strlen(data)+2;
	orig_s=malloc(l);
	if(!orig_s) {
		opbx_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	s=orig_s;
	strncpy(s,data,l);

	template=strsep(&s,"|");
	if(! template) {
		opbx_log(LOG_WARNING, "PARK: An announce template must be defined\n");
		free(orig_s);
		return -1;
	}
  
	if(s) {
		timeout = atoi(strsep(&s, "|"));
		timeout *= 1000;
	}
	dial=strsep(&s, "|");
	if(!dial) {
		opbx_log(LOG_WARNING, "PARK: A dial resource must be specified i.e: Console/dsp or Zap/g1/5551212\n");
		free(orig_s);
		return -1;
	} else {
		dialtech=strsep(&dial, "/");
		dialstr=dial;
		opbx_verbose( VERBOSE_PREFIX_3 "Dial Tech,String: (%s,%s)\n", dialtech,dialstr);
	}

	return_context = s;
  
	if(return_context != NULL) {
		/* set the return context. Code borrowed from the Goto builtin */
    
		working = return_context;
		context = strsep(&working, "|");
		exten = strsep(&working, "|");
		if(!exten) {
			/* Only a priority in this one */
			priority = context;
			exten = NULL;
			context = NULL;
		} else {
			priority = strsep(&working, "|");
			if(!priority) {
				/* Only an extension and priority in this one */
				priority = exten;
				exten = context;
				context = NULL;
		}
	}
	if(atoi(priority) < 0) {
		opbx_log(LOG_WARNING, "Priority '%s' must be a number > 0\n", priority);
		free(orig_s);
		return -1;
	}
	/* At this point we have a priority and maybe an extension and a context */
	chan->priority = atoi(priority);
	if(exten && strcasecmp(exten, "BYEXTENSION"))
		strncpy(chan->exten, exten, sizeof(chan->exten)-1);
	if(context)
		strncpy(chan->context, context, sizeof(chan->context)-1);
	} else {  /* increment the priority by default*/
		chan->priority++;
	}


	if(option_verbose > 2) {
		opbx_verbose( VERBOSE_PREFIX_3 "Return Context: (%s,%s,%d) ID: %s\n", chan->context,chan->exten, chan->priority, chan->cid.cid_num);
		if(!opbx_exists_extension(chan, chan->context, chan->exten, chan->priority, chan->cid.cid_num)) {
			opbx_verbose( VERBOSE_PREFIX_3 "Warning: Return Context Invalid, call will return to default|s\n");
		}
	}
  
	LOCAL_USER_ADD(u);

	/* we are using masq_park here to protect * from touching the channel once we park it.  If the channel comes out of timeout
	before we are done announcing and the channel is messed with, Kablooeee.  So we use Masq to prevent this.  */

	opbx_masq_park_call(chan, NULL, timeout, &lot);

	res=-1; 

	opbx_verbose( VERBOSE_PREFIX_3 "Call Parking Called, lot: %d, timeout: %d, context: %s\n", lot, timeout, return_context);

	/* Now place the call to the extention */

	dchan = opbx_request_and_dial(dialtech, OPBX_FORMAT_SLINEAR, dialstr,30000, &outstate, chan->cid.cid_num, chan->cid.cid_name);

	if(dchan) {
		if(dchan->_state == OPBX_STATE_UP) {
			if(option_verbose > 3)
				opbx_verbose(VERBOSE_PREFIX_4 "Channel %s was answered.\n", dchan->name);
		} else {
			if(option_verbose > 3)
				opbx_verbose(VERBOSE_PREFIX_4 "Channel %s was never answered.\n", dchan->name);
        			opbx_log(LOG_WARNING, "PARK: Channel %s was never answered for the announce.\n", dchan->name);
			opbx_hangup(dchan);
			free(orig_s);
			LOCAL_USER_REMOVE(u);
			return -1;
		}
	} else {
		opbx_log(LOG_WARNING, "PARK: Unable to allocate announce channel.\n");
		free(orig_s);
		LOCAL_USER_REMOVE(u);
		return -1; 
	}

	opbx_stopstream(dchan);

	/* now we have the call placed and are ready to play stuff to it */

	opbx_verbose(VERBOSE_PREFIX_4 "Announce Template:%s\n", template);

	tpl_working = template;
	tpl_current=strsep(&tpl_working, ":");

	while(tpl_current && looptemp < sizeof(tmp)) {
		tmp[looptemp]=tpl_current;
		looptemp++;
		tpl_current=strsep(&tpl_working,":");
	}

	for(i=0; i<looptemp; i++) {
		opbx_verbose(VERBOSE_PREFIX_4 "Announce:%s\n", tmp[i]);
		if(!strcmp(tmp[i], "PARKED")) {
			opbx_say_digits(dchan, lot, "", dchan->language);
		} else {
			dres = opbx_streamfile(dchan, tmp[i], dchan->language);
			if(!dres) {
				dres = opbx_waitstream(dchan, "");
			} else {
				opbx_log(LOG_WARNING, "opbx_streamfile of %s failed on %s\n", tmp[i], dchan->name);
				dres = 0;
			}
		}
	}

	opbx_stopstream(dchan);  
	opbx_hangup(dchan);

	LOCAL_USER_REMOVE(u);
	free(orig_s);
	return res;
}



int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return opbx_unregister_application(app);
}

int load_module(void)
{
	/* return opbx_register_application(app, park_exec); */
	return opbx_register_application(app, parkandannounce_exec, synopsis, descrip);
}

char *description(void)
{
	return tdesc;
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
