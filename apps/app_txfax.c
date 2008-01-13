/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Trivial application to send a TIFF file as a FAX
 * 
 * Copyright (C) 2003, 2005, Steve Underwood
 *
 * Steve Underwood <steveu@coppice.org>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 *
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>

#include <spandsp.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/translate.h"
#include "callweaver/manager.h"

static const char tdesc[] = "Trivial FAX Transmit Application";

static void *txfax_app;
static const char txfax_name[] = "TxFAX";
static const char txfax_synopsis[] = "Send a FAX file";
static const char txfax_syntax[] = "TxFAX(filename[, caller][, debug][, ecm])";
static const char txfax_descrip[] = 
"Send a given TIFF file to the channel as a FAX.\n"
"The \"caller\" option makes the application behave as a calling machine,\n"
"rather than the answering machine. The default behaviour is to behave as\n"
"an answering machine.\n"
"The \"ecm\" option enables ECM.\n"
"Uses LOCALSTATIONID to identify itself to the remote end.\n"
"     LOCALSUBADDRESS to specify a sub-address to the remote end.\n"
"     LOCALHEADERINFO to generate a header line on each page.\n"
"Sets REMOTESTATIONID to the receiver CSID.\n"
"     FAXPAGES to the number of pages sent.\n"
"     FAXBITRATE to the transmition rate.\n"
"     FAXRESOLUTION to the resolution.\n"
"     PHASEESTATUS to the phase E result status.\n"
"     PHASEESTRING to the phase E result string.\n"
"Returns -1 when the user hangs up, or if the file does not exist.\n"
"Returns 0 otherwise.\n";


#define MAX_BLOCK_SIZE 240
#define ready_to_talk(chan) ( (!chan ||  opbx_check_hangup(chan) )  ?  0  :  1)

static void span_message(int level, const char *msg)
{
    int opbx_level;
    
    if (level == SPAN_LOG_ERROR)
        opbx_level = __OPBX_LOG_ERROR;
    else if (level == SPAN_LOG_WARNING)
        opbx_level = __OPBX_LOG_WARNING;
    else
        opbx_level = __OPBX_LOG_DEBUG;
    //opbx_level = __OPBX_LOG_WARNING;
    opbx_log(opbx_level, __FILE__, __LINE__, __PRETTY_FUNCTION__, "%s", msg);
}
/*- End of function --------------------------------------------------------*/

/* Return a monotonically increasing time, in microseconds */
static uint64_t nowis(void)
{
    int64_t now;
#ifndef HAVE_POSIX_TIMERS
    struct timeval tv;

    gettimeofday(&tv, NULL);
    now = tv.tv_sec*1000000LL + tv.tv_usec;
#else
    struct timespec ts;
    
    if (clock_gettime(CLOCK_MONOTONIC, &ts))
        opbx_log(OPBX_LOG_WARNING, "clock_gettime returned %s\n", strerror(errno));
    now = ts.tv_sec*1000000LL + ts.tv_nsec/1000;
#endif
    return now;
}

/*- End of function --------------------------------------------------------*/

/* *****************************************************************************
	MEMBER GENERATOR
   ****************************************************************************/

struct faxgen_state {
    fax_state_t *fax;
    struct opbx_frame f;
    uint8_t buf[sizeof(uint16_t)*MAX_BLOCK_SIZE + 2*OPBX_FRIENDLY_OFFSET];
};

static void *faxgen_alloc(struct opbx_channel *chan, void *params)
{
    struct faxgen_state *fgs;

    opbx_log(OPBX_LOG_DEBUG,"Allocating fax generator\n");
    if ((fgs = malloc(sizeof(*fgs)))) {
        fgs->fax = params;
    }
    return fgs;
}

/*- End of function --------------------------------------------------------*/

static void faxgen_release(struct opbx_channel *chan, void *data)
{
    opbx_log(OPBX_LOG_DEBUG,"Releasing fax generator\n");
    free(data);
    return;
}

/*- End of function --------------------------------------------------------*/

static struct opbx_frame *faxgen_generate(struct opbx_channel *chan, void *data, int samples)
{
    struct faxgen_state *fgs = data;
    int len;
    
    opbx_fr_init_ex(&fgs->f, OPBX_FRAME_VOICE, OPBX_FORMAT_SLINEAR, "TxFAX");

    samples = ( samples <= MAX_BLOCK_SIZE )  ?  samples  :  MAX_BLOCK_SIZE;
    len = fax_tx(fgs->fax, (int16_t *) &fgs->buf[2*OPBX_FRIENDLY_OFFSET], samples);
    if (len) {
        fgs->f.datalen = len*sizeof(int16_t);
        fgs->f.samples = len;
        fgs->f.data = &fgs->buf[2*OPBX_FRIENDLY_OFFSET];
        fgs->f.offset = 2*OPBX_FRIENDLY_OFFSET;
    }

    return &fgs->f;
}

static struct opbx_generator faxgen = 
{
	alloc: 		faxgen_alloc,
	release: 	faxgen_release,
	generate: 	faxgen_generate,
};

/*- End of function --------------------------------------------------------*/

static void phase_e_handler(t30_state_t *s, void *user_data, int result)
{
    struct opbx_channel *chan;
    char local_ident[21];
    char far_ident[21];
    char buf[128];
    const char *mode;
    t30_stats_t t;

    t30_get_transfer_statistics(s, &t);
    
    chan = (struct opbx_channel *) user_data;
    t30_get_local_ident(s, local_ident);
    t30_get_far_ident(s, far_ident);
    pbx_builtin_setvar_helper(chan, "REMOTESTATIONID", far_ident);
    snprintf(buf, sizeof(buf), "%d", t.pages_transferred);
    pbx_builtin_setvar_helper(chan, "FAXPAGES", buf);
    snprintf(buf, sizeof(buf), "%d", t.y_resolution);
    pbx_builtin_setvar_helper(chan, "FAXRESOLUTION", buf);
    snprintf(buf, sizeof(buf), "%d", t.bit_rate);
    pbx_builtin_setvar_helper(chan, "FAXBITRATE", buf);
    snprintf(buf, sizeof(buf), "%d", result);
    pbx_builtin_setvar_helper(chan, "PHASEESTATUS", buf);
    snprintf(buf, sizeof(buf), "%s", t30_completion_code_to_str(result));
    pbx_builtin_setvar_helper(chan, "PHASEESTRING", buf);
    mode = (chan->t38_status == T38_NEGOTIATED)  ?  "T.38"  :  "Analogue";
    pbx_builtin_setvar_helper(chan, "FAXMODE", mode);

    opbx_log(OPBX_LOG_DEBUG, "==============================================================================\n");
    if (result == T30_ERR_OK) {
        opbx_log(OPBX_LOG_DEBUG, "Fax successfully sent (%s).\n", mode);
        opbx_log(OPBX_LOG_DEBUG, "Remote station id: %s\n", far_ident);
        opbx_log(OPBX_LOG_DEBUG, "Local station id:  %s\n", local_ident);
        opbx_log(OPBX_LOG_DEBUG, "Pages transferred: %i\n", t.pages_transferred);
        opbx_log(OPBX_LOG_DEBUG, "Image resolution:  %i x %i\n", t.x_resolution, t.y_resolution);
        opbx_log(OPBX_LOG_DEBUG, "Transfer Rate:     %i\n", t.bit_rate);
        manager_event(EVENT_FLAG_CALL,
                      "FaxSent", "Channel: %s\nExten: %s\nCallerID: %s\nRemoteStationID: %s\nLocalStationID: %s\nPagesTransferred: %i\nResolution: %i\nTransferRate: %i\nFileName: %s\n",
                      chan->name,
                      chan->exten,
                      (chan->cid.cid_num)  ?  chan->cid.cid_num  :  "",
                      far_ident,
                      local_ident,
                      t.pages_transferred,
                      t.y_resolution,
                      t.bit_rate,
                      s->rx_file);
    }
    else
        opbx_log(OPBX_LOG_DEBUG, "Fax send not successful - result (%d) %s.\n", result, t30_completion_code_to_str(result));
    opbx_log(OPBX_LOG_DEBUG, "==============================================================================\n");
}
/*- End of function --------------------------------------------------------*/

static int t38_tx_packet_handler(t38_core_state_t *s, void *user_data, const uint8_t *buf, int len, int count)
{
    struct opbx_frame outf;
    struct opbx_channel *chan;

    chan = (struct opbx_channel *) user_data;

    opbx_fr_init_ex(&outf, OPBX_FRAME_MODEM, OPBX_MODEM_T38, "TxFAX");
    outf.datalen = len;
    outf.data = (char *) buf;
    outf.tx_copies = count;
    if (opbx_write(chan, &outf) < 0)
        opbx_log(OPBX_LOG_WARNING, "Unable to write frame to channel; %s\n", strerror(errno));
    return 0;
}
/*- End of function --------------------------------------------------------*/

static int txfax_t38(struct opbx_channel *chan, t38_terminal_state_t *t38, char *source_file, int calling_party,int verbose, int ecm) {
    char 		*x;
    struct opbx_frame 	*inf = NULL;
    int 		ready = 1,
			res = 0;
    uint64_t now;
    uint64_t passage;

    memset(t38, 0, sizeof(*t38));

    if (t38_terminal_init(t38, calling_party, t38_tx_packet_handler, chan) == NULL)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to start T.38 termination.\n");
        return -1;
    }

    span_log_set_message_handler(&t38->logging, span_message);
    span_log_set_message_handler(&t38->t30_state.logging, span_message);
    span_log_set_message_handler(&t38->t38.logging, span_message);

    if (verbose)
    {
        span_log_set_level(&t38->logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
        span_log_set_level(&t38->t30_state.logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
        span_log_set_level(&t38->t38.logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
    }

    x = pbx_builtin_getvar_helper(chan, "LOCALSTATIONID");
    if (x  &&  x[0])
        t30_set_local_ident(&t38->t30_state, x);
    x = pbx_builtin_getvar_helper(chan, "LOCALSUBADDRESS");
    if (x  &&  x[0])
        t30_set_local_sub_address(&t38->t30_state, x);
    x = pbx_builtin_getvar_helper(chan, "LOCALHEADERINFO");
    if (x  &&  x[0])
        t30_set_header_info(&t38->t30_state, x);
    t30_set_tx_file(&t38->t30_state, source_file, -1, -1);

    //t30_set_phase_b_handler(&t38.t30_state, phase_b_handler, chan);
    //t30_set_phase_d_handler(&t38.t30_state, phase_d_handler, chan);
    t30_set_phase_e_handler(&t38->t30_state, phase_e_handler, chan);

    x = pbx_builtin_getvar_helper(chan, "FAX_DISABLE_V17");
    if (x  &&  x[0])
        t30_set_supported_modems(&t38->t30_state, T30_SUPPORT_V29 | T30_SUPPORT_V27TER);
    else
        t30_set_supported_modems(&t38->t30_state, T30_SUPPORT_V17 | T30_SUPPORT_V29 | T30_SUPPORT_V27TER);

    t30_set_supported_image_sizes(&t38->t30_state, T30_SUPPORT_US_LETTER_LENGTH | T30_SUPPORT_US_LEGAL_LENGTH | T30_SUPPORT_UNLIMITED_LENGTH
                                	        | T30_SUPPORT_215MM_WIDTH | T30_SUPPORT_255MM_WIDTH | T30_SUPPORT_303MM_WIDTH);
    t30_set_supported_resolutions(&t38->t30_state, T30_SUPPORT_STANDARD_RESOLUTION | T30_SUPPORT_FINE_RESOLUTION | T30_SUPPORT_SUPERFINE_RESOLUTION
                                                | T30_SUPPORT_R8_RESOLUTION | T30_SUPPORT_R16_RESOLUTION);

    if (ecm) {
        t30_set_ecm_capability(&t38->t30_state, TRUE);
        t30_set_supported_compressions(&t38->t30_state, T30_SUPPORT_T4_1D_COMPRESSION | T30_SUPPORT_T4_2D_COMPRESSION | T30_SUPPORT_T6_COMPRESSION);
        opbx_log(OPBX_LOG_DEBUG, "Enabling ECM mode for app_txfax\n"  );
    } 
    else 
    {
        t30_set_supported_compressions(&t38->t30_state, T30_SUPPORT_T4_1D_COMPRESSION | T30_SUPPORT_T4_2D_COMPRESSION);
    }

    passage = nowis();

    t38_terminal_set_tep_mode(t38, TRUE);
    while (ready  &&  ready_to_talk(chan))
    {
    
	if ( chan->t38_status != T38_NEGOTIATED )
	    break;

        if ((res = opbx_waitfor(chan, 20)) < 0) {
	    ready = 0;
            break;
	}

        now = nowis();
        t38_terminal_send_timeout(t38, (now - passage)/125);
        passage = now;
        /* End application when T38/T30 has finished */
        if ((t38->current_rx_type == T30_MODEM_DONE)  ||  (t38->current_tx_type == T30_MODEM_DONE)) 
            break;

        inf = opbx_read(chan);
        if (inf == NULL) {
	    ready = 0;
            break;
        }

        if (inf->frametype == OPBX_FRAME_MODEM  &&  inf->subclass == OPBX_MODEM_T38)
    	    t38_core_rx_ifp_packet(&t38->t38, inf->data, inf->datalen, inf->seq_no);

        opbx_fr_free(inf);
    }

    return ready;

}
/*- End of function --------------------------------------------------------*/

static int txfax_audio(struct opbx_channel *chan, fax_state_t *fax, char *source_file, int calling_party,int verbose, int ecm) {
    char 		*x;
    struct opbx_frame 	*inf = NULL;
    struct opbx_frame 	outf;
    int 		ready = 1,
			samples = 0,
			res = 0,
			len = 0,
			generator_mode = 0;
    uint64_t		begin = 0,
			received_frames = 0;

    uint8_t __buf[sizeof(uint16_t)*MAX_BLOCK_SIZE + 2*OPBX_FRIENDLY_OFFSET];
    uint8_t *buf = __buf + OPBX_FRIENDLY_OFFSET;

    memset(fax, 0, sizeof(*fax));

    if (fax_init(fax, calling_party) == NULL)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to start FAX\n");
        return -1;
    }
    fax_set_transmit_on_idle(fax, TRUE);
    span_log_set_message_handler(&fax->logging, span_message);
    span_log_set_message_handler(&fax->t30_state.logging, span_message);
    if (verbose)
    {
        span_log_set_level(&fax->logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
        span_log_set_level(&fax->t30_state.logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
    }
    x = pbx_builtin_getvar_helper(chan, "LOCALSTATIONID");
    if (x  &&  x[0])
        t30_set_local_ident(&fax->t30_state, x);
    x = pbx_builtin_getvar_helper(chan, "LOCALSUBADDRESS");
    if (x  &&  x[0])
        t30_set_local_sub_address(&fax->t30_state, x);
    x = pbx_builtin_getvar_helper(chan, "LOCALHEADERINFO");
    if (x  &&  x[0])
        t30_set_header_info(&fax->t30_state, x);
    t30_set_tx_file(&fax->t30_state, source_file, -1, -1);
    //t30_set_phase_b_handler(&fax.t30_state, phase_b_handler, chan);
    //t30_set_phase_d_handler(&fax.t30_state, phase_d_handler, chan);
    t30_set_phase_e_handler(&fax->t30_state, phase_e_handler, chan);

    x = pbx_builtin_getvar_helper(chan, "FAX_DISABLE_V17");
    if (x  &&  x[0])
        t30_set_supported_modems(&fax->t30_state, T30_SUPPORT_V29 | T30_SUPPORT_V27TER);
    else
        t30_set_supported_modems(&fax->t30_state, T30_SUPPORT_V17 | T30_SUPPORT_V29 | T30_SUPPORT_V27TER);

    /* Support for different image sizes && resolutions*/
    t30_set_supported_image_sizes(&fax->t30_state, T30_SUPPORT_US_LETTER_LENGTH | T30_SUPPORT_US_LEGAL_LENGTH | T30_SUPPORT_UNLIMITED_LENGTH
                                                | T30_SUPPORT_215MM_WIDTH | T30_SUPPORT_255MM_WIDTH | T30_SUPPORT_303MM_WIDTH);
    t30_set_supported_resolutions(&fax->t30_state, T30_SUPPORT_STANDARD_RESOLUTION | T30_SUPPORT_FINE_RESOLUTION | T30_SUPPORT_SUPERFINE_RESOLUTION
                                                | T30_SUPPORT_R8_RESOLUTION | T30_SUPPORT_R16_RESOLUTION);
    if (ecm) {
        t30_set_ecm_capability(&fax->t30_state, TRUE);
        t30_set_supported_compressions(&fax->t30_state, T30_SUPPORT_T4_1D_COMPRESSION | T30_SUPPORT_T4_2D_COMPRESSION | T30_SUPPORT_T6_COMPRESSION);
        opbx_log(OPBX_LOG_DEBUG, "Enabling ECM mode for app_txfax\n"  );
    }

    /* This is the main loop */

    begin = nowis();

    while ( ready && ready_to_talk(chan) )
    {
    
	if ( chan->t38_status == T38_NEGOTIATED )
	    break;

        if ((res = opbx_waitfor(chan, 20)) < 0) {
	    ready = 0;
            break;
	}

        if ((fax->current_rx_type == T30_MODEM_DONE)  ||  (fax->current_tx_type == T30_MODEM_DONE))
            break;

        inf = opbx_read(chan);
        if (inf == NULL) {
	    ready = 0;
            break;
        }

	/* We got a frame */
        if (inf->frametype == OPBX_FRAME_VOICE) {

	    received_frames ++;

            if (fax_rx(fax, inf->data, inf->samples))
                    break;

            samples = (inf->samples <= MAX_BLOCK_SIZE)  ?  inf->samples  :  MAX_BLOCK_SIZE;
            if ((len = fax_tx(fax, (int16_t *) &buf[OPBX_FRIENDLY_OFFSET], samples)) > 0) {
                opbx_fr_init_ex(&outf, OPBX_FRAME_VOICE, OPBX_FORMAT_SLINEAR, "TxFAX");
                outf.datalen = len*sizeof(int16_t);
                outf.samples = len;
                outf.data = &buf[OPBX_FRIENDLY_OFFSET];
                outf.offset = OPBX_FRIENDLY_OFFSET;

                if (opbx_write(chan, &outf) < 0) {
                    opbx_log(OPBX_LOG_WARNING, "Unable to write frame to channel; %s\n", strerror(errno));
                    break;
                }
            }
	    else
	    {
	    	len = samples;
    		opbx_fr_init_ex(&outf, OPBX_FRAME_VOICE, OPBX_FORMAT_SLINEAR, "TxFAX");
    		outf.datalen = len*sizeof(int16_t);
    		outf.samples = len;
    		outf.data = &buf[OPBX_FRIENDLY_OFFSET];
    		outf.offset = OPBX_FRIENDLY_OFFSET;
    		memset(&buf[OPBX_FRIENDLY_OFFSET],0,outf.datalen);
    		if (opbx_write(chan, &outf) < 0)
    		{
        	    opbx_log(OPBX_LOG_WARNING, "Unable to write frame to channel; %s\n", strerror(errno));
		    break;
    		}
	    }
        }
	else {
	    if ( (nowis() - begin) > 1000000 ) {
		if (received_frames < 20 ) { // just to be sure we have had no frames ...
		    opbx_log(OPBX_LOG_WARNING,"Switching to generator mode\n");
		    generator_mode = 1;
		    break;
		}
	    }
	}
        opbx_fr_free(inf);
        inf = NULL;
    }

    if (inf) {
        opbx_fr_free(inf);
        inf = NULL;
    }

    if (generator_mode) {
	// This is activated when we don't receive any frame for
	// X seconds (see above)... we are probably on ZAP or talking without UDPTL to
	// another callweaver box
	opbx_generator_activate(chan, &chan->generator, &faxgen, fax);

	while ( ready && ready_to_talk(chan) ) {

	    if ( chan->t38_status == T38_NEGOTIATED )
		break;

	    if ((res = opbx_waitfor(chan, 20)) < 0) {
	        ready = 0;
        	break;
	    }

    	    if ((fax->current_rx_type == T30_MODEM_DONE)  ||  (fax->current_tx_type == T30_MODEM_DONE))
        	break;

    	    inf = opbx_read(chan);
    	    if (inf == NULL) {
		ready = 0;
        	break;
    	    }

	    /* We got a frame */
    	    if (inf->frametype == OPBX_FRAME_VOICE) {
        	if (fax_rx(fax, inf->data, inf->samples)) {
		    ready = 0;
                    break;
		}
	    }

    	    opbx_fr_free(inf);
	    inf = NULL;
	}

	if (inf) {
    	    opbx_fr_free(inf);
	    inf = NULL;
	}
	opbx_generator_deactivate(&chan->generator);

    }

    return ready;
}
/*- End of function --------------------------------------------------------*/

static int txfax_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_len)
{
    fax_state_t 	fax;
    t38_terminal_state_t t38;

    char *source_file;
    int res = 0;
    int ready;

    int calling_party;
    int verbose;
    int ecm = FALSE;
    
    struct localuser *u;

    int original_read_fmt;
    int original_write_fmt;

    /* Basic initial checkings */

    if (chan == NULL)
    {
        opbx_log(OPBX_LOG_WARNING, "Fax transmit channel is NULL. Giving up.\n");
        return -1;
    }

    /* Make sure they are initialized to zero */
    memset(&fax, 0, sizeof(fax));
    memset(&t38, 0, sizeof(t38));

    if (argc < 1)
        return opbx_function_syntax(txfax_syntax);

    /* Resetting channel variables related to T38 */
    
    pbx_builtin_setvar_helper(chan, "REMOTESTATIONID", "");
    pbx_builtin_setvar_helper(chan, "FAXPAGES", "");
    pbx_builtin_setvar_helper(chan, "FAXRESOLUTION", "");
    pbx_builtin_setvar_helper(chan, "FAXBITRATE", "");
    pbx_builtin_setvar_helper(chan, "FAXMODE", "");
    pbx_builtin_setvar_helper(chan, "PHASEESTATUS", "");
    pbx_builtin_setvar_helper(chan, "PHASEESTRING", "");

    /* Parsing parameters */
    
    calling_party = FALSE;
    verbose = FALSE;

    source_file = argv[0];

    while (argv++, --argc) {
        if (strcmp("caller", argv[0]) == 0)
        {
            calling_party = TRUE;
        }
        else if (strcmp("debug", argv[0]) == 0)
        {
            verbose = TRUE;
        }
        else if (strcmp("ecm", argv[0]) == 0)
        {
            ecm = TRUE;
        }
        else if (strcmp("start", argv[0]) == 0)
        {
            /* TODO: handle this */
        }
        else if (strcmp("end", argv[0]) == 0)
        {
            /* TODO: handle this */
        }
    }
    /* Done parsing */

    LOCAL_USER_ADD(u);

    if (chan->_state != OPBX_STATE_UP)
    {
        /* Shouldn't need this, but checking to see if channel is already answered
         * Theoretically the PBX should already have answered before running the app */
        res = opbx_answer(chan);
	if (!res)
	{
    	    opbx_log(OPBX_LOG_DEBUG, "Could not answer channel '%s'\n", chan->name);
	    //LOCAL_USER_REMOVE(u);
	    //return res;
	}
    }

    /* Setting read and write formats */
    
    original_read_fmt = chan->readformat;
    if (original_read_fmt != OPBX_FORMAT_SLINEAR)
    {
        res = opbx_set_read_format(chan, OPBX_FORMAT_SLINEAR);
        if (res < 0)
        {
            opbx_log(OPBX_LOG_WARNING, "Unable to set to linear read mode, giving up\n");
            LOCAL_USER_REMOVE(u);
            return -1;
        }
    }

    original_write_fmt = chan->writeformat;
    if (original_write_fmt != OPBX_FORMAT_SLINEAR)
    {
        res = opbx_set_write_format(chan, OPBX_FORMAT_SLINEAR);
        if (res < 0)
        {
            opbx_log(OPBX_LOG_WARNING, "Unable to set to linear write mode, giving up\n");
            res = opbx_set_read_format(chan, original_read_fmt);
            if (res)
                opbx_log(OPBX_LOG_WARNING, "Unable to restore read format on '%s'\n", chan->name);
            LOCAL_USER_REMOVE(u);
            return -1;
        }
    }


    /* This is the main loop */

    ready = TRUE;        

    while ( ready && ready_to_talk(chan) )
    {


        if ( ready && chan->t38_status != T38_NEGOTIATED ) {
	    ready = txfax_audio( chan, &fax, source_file, calling_party, verbose, ecm);
	}

        if ( ready && chan->t38_status == T38_NEGOTIATED ) {
	    ready = txfax_t38  ( chan, &t38, source_file, calling_party, verbose, ecm);
	}

	if (chan->t38_status != T38_NEGOTIATING)
	    ready = 0; // 1 loop is enough. This could be useful if we want to turn from udptl to RTP later.

    }

    if (chan->t38_status != T38_NEGOTIATED)
	t30_terminate(&fax.t30_state);
    else
	t30_terminate(&t38.t30_state);

    fax_release(&fax);
    t38_terminal_release(&t38);

    /* Restoring initial channel formats. */

    if (original_read_fmt != OPBX_FORMAT_SLINEAR)
    {
        if ((res = opbx_set_read_format(chan, original_read_fmt)))
            opbx_log(OPBX_LOG_WARNING, "Unable to restore read format on '%s'\n", chan->name);
    }
    if (original_write_fmt != OPBX_FORMAT_SLINEAR)
    {
        if ((res = opbx_set_write_format(chan, original_write_fmt)))
            opbx_log(OPBX_LOG_WARNING, "Unable to restore write format on '%s'\n", chan->name);
    }

    return ready;

}
/*- End of function --------------------------------------------------------*/

static int unload_module(void)
{
    int res = 0;

    res |= opbx_unregister_function(txfax_app);
    return res;
}
/*- End of function --------------------------------------------------------*/

static int load_module(void)
{
    if (!faxgen.is_initialized)
        opbx_object_init(&faxgen, OPBX_OBJECT_CURRENT_MODULE, OPBX_OBJECT_NO_REFS);

    txfax_app = opbx_register_function(txfax_name, txfax_exec, txfax_synopsis, txfax_syntax, txfax_descrip);
    return 0;
}
/*- End of function --------------------------------------------------------*/


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)

/*- End of file ------------------------------------------------------------*/
