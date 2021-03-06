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
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>
#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES
#include <spandsp.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/app.h"
#include "callweaver/dsp.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/translate.h"
#include "callweaver/manager.h"
#include "callweaver/keywords.h"

static const char tdesc[] = "Trivial FAX Transmit Application";

static void *txfax_app;
static const char txfax_name[] = "TxFAX";
static const char txfax_synopsis[] = "Send a FAX file";
static const char txfax_syntax[] = "TxFAX(filename[, caller][, debug])";
static const char txfax_descrip[] = 
"Send a given TIFF file to the channel as a FAX.\n"
"The \"caller\" option makes the application behave as a calling machine,\n"
"rather than the answering machine. The default behaviour is to behave as\n"
"an answering machine.\n"
"The \"ecm\" option enables ECM.\n"
"Uses LOCALSTATIONID to identify itself to the remote end.\n"
"     LOCALSUBADDRESS to specify a sub-address to the remote end.\n"
"     LOCALHEADERINFO to generate a header line on each page.\n"
"     FAX_DISABLE_V17\n"
"     FAX_DISABLE_ECM\n"
"Sets REMOTESTATIONID to the remote end's identity.\n"
"     FAXPAGES to the number of pages sent.\n"
"     FAXBITRATE to the transmition rate.\n"
"     FAXRESOLUTION to the resolution.\n"
"     PHASEESTATUS to the phase E result status.\n"
"     PHASEESTRING to the phase E result string.\n"
"Returns -1 when the user hangs up, or if the file does not exist.\n"
"Returns 0 otherwise.\n";


#define MAX_BLOCK_SIZE 240
#define ready_to_talk(chan) ( (!chan ||  cw_check_hangup(chan) )  ?  0  :  1)

static void span_message(int level, const char *msg)
{
    cw_log_level cw_level = CW_LOG_DEBUG;
    
    if (level == SPAN_LOG_ERROR)
        cw_level = CW_LOG_ERROR;
    else if (level == SPAN_LOG_WARNING)
        cw_level = CW_LOG_WARNING;

    cw_log(cw_level, "%s", msg);
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
        cw_log(CW_LOG_WARNING, "clock_gettime returned %s\n", strerror(errno));
    now = ts.tv_sec*1000000LL + ts.tv_nsec/1000;
#endif
    return now;
}
/*- End of function --------------------------------------------------------*/

struct faxgen_state {
    fax_state_t *fax;
    struct cw_frame f;
    uint8_t buf[sizeof(uint16_t)*MAX_BLOCK_SIZE + 2*CW_FRIENDLY_OFFSET];
};

static void *faxgen_alloc(struct cw_channel *chan, void *params)
{
    struct faxgen_state *fgs;

    CW_UNUSED(chan);

    cw_log(CW_LOG_DEBUG, "Allocating fax generator\n");
    if ((fgs = malloc(sizeof(*fgs)))) {
        fgs->fax = params;
    }
    return fgs;
}
/*- End of function --------------------------------------------------------*/

static void faxgen_release(struct cw_channel *chan, void *data)
{
    CW_UNUSED(chan);

    cw_log(CW_LOG_DEBUG, "Releasing fax generator\n");
    free(data);
    return;
}
/*- End of function --------------------------------------------------------*/

static struct cw_frame *faxgen_generate(struct cw_channel *chan, void *data, int samples)
{
    struct faxgen_state *fgs = data;
    int len;
    
    CW_UNUSED(chan);

    cw_fr_init_ex(&fgs->f, CW_FRAME_VOICE, CW_FORMAT_SLINEAR);

    samples = (samples <= MAX_BLOCK_SIZE)  ?  samples  :  MAX_BLOCK_SIZE;
    if ((len = fax_tx(fgs->fax, (int16_t *) &fgs->buf[2*CW_FRIENDLY_OFFSET], samples)))
    {
        fgs->f.datalen = len*sizeof(int16_t);
        fgs->f.samples = len;
        fgs->f.data = &fgs->buf[2*CW_FRIENDLY_OFFSET];
        fgs->f.offset = 2*CW_FRIENDLY_OFFSET;
    }

    return &fgs->f;
}
/*- End of function --------------------------------------------------------*/

static struct cw_generator faxgen = 
{
	alloc: 		faxgen_alloc,
	release: 	faxgen_release,
	generate: 	faxgen_generate,
};


static void phase_e_handler(t30_state_t *s, void *user_data, int result)
{
    struct cw_channel *chan;
    char buf[128];
    const char *mode;
    t30_stats_t t;
    const char *tx_ident;
    const char *rx_ident;

    chan = (struct cw_channel *) user_data;
    t30_get_transfer_statistics(s, &t);
    
    tx_ident = t30_get_tx_ident(s);
    if (tx_ident == NULL)
        tx_ident = "";
    rx_ident = t30_get_rx_ident(s);
    if (rx_ident == NULL)
        rx_ident = "";
    pbx_builtin_setvar_helper(chan, "REMOTESTATIONID", rx_ident);
    snprintf(buf, sizeof(buf), "%d", t.pages_tx);
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

    cw_log(CW_LOG_DEBUG, "==============================================================================\n");
    if (result == T30_ERR_OK) {
        cw_log(CW_LOG_DEBUG, "Fax successfully sent (%s).\n", mode);
        cw_log(CW_LOG_DEBUG, "Remote station id: %s\n", rx_ident);
        cw_log(CW_LOG_DEBUG, "Local station id:  %s\n", tx_ident);
        cw_log(CW_LOG_DEBUG, "Pages transferred: %i\n", t.pages_tx);
        cw_log(CW_LOG_DEBUG, "Image resolution:  %i x %i\n", t.x_resolution, t.y_resolution);
        cw_log(CW_LOG_DEBUG, "Transfer Rate:     %i\n", t.bit_rate);

        cw_manager_event(CW_EVENT_FLAG_CALL, "FaxSent",
            9,
            cw_msg_tuple("Channel",          "%s", chan->name),
            cw_msg_tuple("Exten",            "%s", chan->name),
            cw_msg_tuple("CallerID",         "%s", (chan->cid.cid_num ? chan->cid.cid_num  :  "")),
            cw_msg_tuple("RemoteStationID",  "%s", rx_ident),
            cw_msg_tuple("LocalStationID",   "%s", tx_ident),
            cw_msg_tuple("PagesTransferred", "%i", t.pages_tx),
            cw_msg_tuple("Resolution",       "%i", t.y_resolution),
            cw_msg_tuple("TransferRate",     "%i", t.bit_rate),
            cw_msg_tuple("FileName",         "%s", s->rx_file)
        );
    }
    else
        cw_log(CW_LOG_DEBUG, "Fax send not successful - result (%d) %s.\n", result, t30_completion_code_to_str(result));
    cw_log(CW_LOG_DEBUG, "==============================================================================\n");
}
/*- End of function --------------------------------------------------------*/

static int t38_tx_packet_handler(t38_core_state_t *s, void *user_data, const uint8_t *buf, int len, int count)
{
    struct cw_frame outf, *f;
    struct cw_channel *chan;

    CW_UNUSED(s);

    chan = (struct cw_channel *) user_data;

    cw_fr_init_ex(&outf, CW_FRAME_MODEM, CW_MODEM_T38);
    outf.datalen = len;
    outf.data = (char *) buf;
    outf.tx_copies = count;
    f = &outf;
    if (cw_write(chan, &f) < 0)
        cw_log(CW_LOG_WARNING, "Unable to write frame to channel; %s\n", strerror(errno));
    cw_fr_free(f);
    return 0;
}
/*- End of function --------------------------------------------------------*/

static int fax_set_common(struct cw_channel *chan, t30_state_t *t30, const char *source_file, int calling_party, int verbose)
{
    struct cw_var_t 	*var;

    CW_UNUSED(calling_party);

    if (!verbose && (var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_FAX_DEBUG, "FAX_DEBUG"))) {
        verbose = 1;
        cw_object_put(var);
    }

    span_log_set_message_handler(&t30->logging, span_message);

    if (verbose)
        span_log_set_level(&t30->logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);

    if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_LOCALSTATIONID, "LOCALSTATIONID"))) {
        t30_set_tx_ident(t30, var->value);
	cw_object_put(var);
    }
    if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_LOCALSUBADDRESS, "LOCALSUBADDRESS"))) {
        t30_set_tx_sub_address(t30, var->value);
	cw_object_put(var);
    }
    if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_LOCALHEADERINFO, "LOCALHEADERINFO"))) {
        t30_set_tx_page_header_info(t30, var->value);
	cw_object_put(var);
    }
    t30_set_tx_file(t30, source_file, -1, -1);

    //t30_set_phase_b_handler(t30, phase_b_handler, chan);
    //t30_set_phase_d_handler(t30, phase_d_handler, chan);
    t30_set_phase_e_handler(t30, phase_e_handler, chan);

    if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_FAX_DISABLE_V17, "FAX_DISABLE_V17"))) {
        t30_set_supported_modems(t30, T30_SUPPORT_V29 | T30_SUPPORT_V27TER);
        cw_object_put(var);
    } else
        t30_set_supported_modems(t30, T30_SUPPORT_V17 | T30_SUPPORT_V29 | T30_SUPPORT_V27TER);

    if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_FAX_DISABLE_ECM, "FAX_DISABLE_ECM"))) {
        cw_log(CW_LOG_DEBUG, "Disabling ECM mode\n");
        t30_set_ecm_capability(t30, FALSE);
        t30_set_supported_compressions(t30, T30_SUPPORT_T4_1D_COMPRESSION | T30_SUPPORT_T4_2D_COMPRESSION);
        cw_object_put(var);
    } else {
        t30_set_ecm_capability(t30, TRUE);
        t30_set_supported_compressions(t30, T30_SUPPORT_T4_1D_COMPRESSION | T30_SUPPORT_T4_2D_COMPRESSION | T30_SUPPORT_T6_COMPRESSION);
    }

    /* Support for different image sizes && resolutions*/
    t30_set_supported_image_sizes(t30, T30_SUPPORT_US_LETTER_LENGTH | T30_SUPPORT_US_LEGAL_LENGTH | T30_SUPPORT_UNLIMITED_LENGTH
                                     | T30_SUPPORT_215MM_WIDTH | T30_SUPPORT_255MM_WIDTH | T30_SUPPORT_303MM_WIDTH);
    t30_set_supported_resolutions(t30, T30_SUPPORT_STANDARD_RESOLUTION | T30_SUPPORT_FINE_RESOLUTION | T30_SUPPORT_SUPERFINE_RESOLUTION
                                     | T30_SUPPORT_R8_RESOLUTION | T30_SUPPORT_R16_RESOLUTION);

    return verbose;
}
/*- End of function --------------------------------------------------------*/

static int txfax_t38(struct cw_channel *chan, t38_terminal_state_t *t38, const char *source_file, int calling_party,int verbose) {
    struct cw_frame 	*inf = NULL;
    int 		ready = 1;
    uint64_t now;
    uint64_t passage;
    int old_policy;
    struct sched_param old_sp;
    t30_state_t *t30;
    t38_core_state_t *t38_core;

    memset(t38, 0, sizeof(*t38));

    if (t38_terminal_init(t38, calling_party, t38_tx_packet_handler, chan) == NULL)
    {
        cw_log(CW_LOG_WARNING, "Unable to start T.38 termination.\n");
        return -1;
    }

    t30 = t38_terminal_get_t30_state(t38);
    t38_core = t38_terminal_get_t38_core_state(t38);

    verbose = fax_set_common(chan, t30, source_file, calling_party, verbose);

    span_log_set_message_handler(&t38->logging, span_message);
    span_log_set_message_handler(&t38_core->logging, span_message);

    if (verbose)
    {
        span_log_set_level(&t38->logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
        span_log_set_level(&t38_core->logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
    }

    pthread_getschedparam(pthread_self(), &old_policy, &old_sp);
    pthread_setschedparam(pthread_self(), SCHED_RR, &global_sched_param_rr);

    passage = nowis();

    t38_terminal_set_tep_mode(t38, TRUE);
    while (ready  &&  ready_to_talk(chan))
    {
    
	if (chan->t38_status != T38_NEGOTIATED)
	    break;

        if (cw_waitfor(chan, 20) < 0) {
	    ready = 0;
            break;
	}

        now = nowis();
        t38_terminal_send_timeout(t38, (now - passage)/125);
        passage = now;
        /* End application when T38/T30 has finished */
        if (!t30_call_active(t30)) 
            break;

        inf = cw_read(chan);
        if (inf == NULL) {
	    ready = 0;
            break;
        }

        if (inf->frametype == CW_FRAME_MODEM  &&  inf->subclass == CW_MODEM_T38)
    	    t38_core_rx_ifp_packet(t38_core, inf->data, inf->datalen, inf->seq_no);

        cw_fr_free(inf);
    }

    pthread_setschedparam(pthread_self(), old_policy, &old_sp);

    return ready;

}
/*- End of function --------------------------------------------------------*/

static int txfax_audio(struct cw_channel *chan, fax_state_t *fax, const char *source_file, int calling_party,int verbose) {
    struct cw_frame 	*inf = NULL;
    struct cw_frame 	outf, *fout;
    int 		ready = 1,
			samples = 0,
			len = 0,
			generator_mode = 0;
    uint64_t		begin = 0,
			received_frames = 0;

    uint8_t __buf[sizeof(uint16_t)*MAX_BLOCK_SIZE + 2*CW_FRIENDLY_OFFSET];
    uint8_t *buf = __buf + CW_FRIENDLY_OFFSET;
#if 0
    struct cw_frame *dspf = NULL;
    struct cw_dsp *dsp = NULL;
#endif
    uint64_t voice_frames;
    int old_policy;
    struct sched_param old_sp;
    t30_state_t *t30;

    memset(fax, 0, sizeof(*fax));

    if (fax_init(fax, calling_party) == NULL)
    {
        cw_log(CW_LOG_WARNING, "Unable to start FAX\n");
        return -1;
    }

    t30 = fax_get_t30_state(fax);
    fax_set_transmit_on_idle(fax, TRUE);

    verbose = fax_set_common(chan, t30, source_file, calling_party, verbose);

    span_log_set_message_handler(&fax->logging, span_message);

    if (verbose)
        span_log_set_level(&fax->logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);

    if (calling_party)
    {
        voice_frames = 0;
    }
    else
    {
#if 0
        /* Initializing the DSP */
        if ((dsp = cw_dsp_new()) = NULL)
        {
            cw_log(CW_LOG_WARNING, "Unable to allocate DSP!\n");
        }
        else
        {
            cw_dsp_set_threshold(dsp, 256);
            cw_dsp_set_deatures(dsp, DSP_FEATURE_DTMF_DETECT | DSP_FEATURE_FAX_CNG_DETECT);
            cw_dsp_digitmode(dsp, DSP_DIGITMODE_DTMF | DSP_DIGITMODE_RELAXDTMF);
        }
#endif
        voice_frames = 1;
    }

    /* This is the main loop */
    pthread_getschedparam(pthread_self(), &old_policy, &old_sp);
    pthread_setschedparam(pthread_self(), SCHED_RR, &global_sched_param_rr);

    begin = nowis();

    while (ready && ready_to_talk(chan))
    {
	if (chan->t38_status == T38_NEGOTIATED)
	    break;

        if (cw_waitfor(chan, 20) < 0) {
	    ready = 0;
            break;
	}

        if (!t30_call_active(t30))
            break;

        inf = cw_read(chan);
        if (inf == NULL) {
	    ready = 0;
            break;
        }

	/* We got a frame */
        if (inf->frametype == CW_FRAME_VOICE) {
#if 0
            if (dsp)
            {
                if ((dspf = cw_frdup(inf)))
                    dspf = cw_dsp_process(chan, dsp, dspf);

                if (dspf)
                {
                    if (dspf->frametype == CW_FRAME_DTMF)
                    {
                        if (dspf->subclass == 'f')
                        {
                            cw_log(CW_LOG_DEBUG, "Fax detected in TxFax !!!\n");
                            cw_app_request_t38(chan);
                            /* Prevent any further attempts to negotiate T.38 */
                            cw_dsp_free(dsp);
                            dsp = NULL;
                        }
                    }
                    cw_fr_free(dspf);
                    dspf = NULL;
                }
            }
#else
            if (voice_frames)
            {
                /* Wait a little before trying to switch to T.38, as some things don't seem
                 * to like entirely missing the audio.
                 */
                if (++voice_frames == 100)
                {
                    cw_log(CW_LOG_DEBUG, "Requesting T.38 negotiation in TxFax !!!\n");
                    cw_app_request_t38(chan);
                    voice_frames = 0;
                }
            }
#endif
	    received_frames ++;

            if (fax_rx(fax, inf->data, inf->samples))
                    break;

            samples = (inf->samples <= MAX_BLOCK_SIZE)  ?  inf->samples  :  MAX_BLOCK_SIZE;
            if ((len = fax_tx(fax, (int16_t *) &buf[CW_FRIENDLY_OFFSET], samples)) > 0) {
                cw_fr_init_ex(&outf, CW_FRAME_VOICE, CW_FORMAT_SLINEAR);
                outf.datalen = len*sizeof(int16_t);
                outf.samples = len;
                outf.data = &buf[CW_FRIENDLY_OFFSET];
                outf.offset = CW_FRIENDLY_OFFSET;
                fout = &outf;
                if (cw_write(chan, &fout) < 0) {
                    cw_log(CW_LOG_WARNING, "Unable to write frame to channel; %s\n", strerror(errno));
                    break;
                }
                cw_fr_free(fout);
            }
        }
	else
        {
	    if ((nowis() - begin) > 1000000)
            {
		if (received_frames < 20 ) { // just to be sure we have had no frames ...
		    cw_log(CW_LOG_WARNING, "Switching to generator mode\n");
		    generator_mode = 1;
		    break;
		}
	    }
	}
        cw_fr_free(inf);
        inf = NULL;
    }

    if (inf) {
        cw_fr_free(inf);
        inf = NULL;
    }

    if (generator_mode) {
	/* This is activated when we don't receive any frame for X seconds (see above)... */
#if 0
        if (dsp)
            cw_dsp_reset(dsp);
#endif
	cw_generator_activate(chan, &chan->generator, &faxgen, fax);

	while (ready && ready_to_talk(chan)) {

	    if (chan->t38_status == T38_NEGOTIATED)
		break;

	    if (cw_waitfor(chan, 20) < 0) {
	        ready = 0;
        	break;
	    }

    	    if (!t30_call_active(t30))
        	break;

    	    inf = cw_read(chan);
    	    if (inf == NULL) {
		ready = 0;
        	break;
    	    }

	    /* We got a frame */
            if (inf->frametype == CW_FRAME_VOICE)
            {
#if 0
                if (dsp)
                {
                    if ((dspf = cw_frdup(inf)))
                        dspf = cw_dsp_process(chan, dsp, dspf);

	            if (dspf)
                    {
                        if (dspf->frametype == CW_FRAME_DTMF)
                        {
                            if (dspf->subclass == 'f')
                            {
                                cw_log(CW_LOG_DEBUG, "Fax detected in TxFax !!!\n");
                                cw_app_request_t38(chan);
                                /* Prevent any further attempts to negotiate T.38 */
                                cw_dsp_free(dsp);
                                dsp = NULL;
		            }
		        }
                        cw_fr_free(dspf);
                        dspf = NULL;
	            }
                }
#else
                if (voice_frames)
                {
                    if (++voice_frames == 100)
                    {
                        cw_log(CW_LOG_DEBUG, "Requesting T.38 negotiation in TxFax !!!\n");
                        cw_app_request_t38(chan);
                        voice_frames = 0;
                    }
                }
#endif
        	if (fax_rx(fax, inf->data, inf->samples)) {
		    ready = 0;
                    break;
		}
	    }

    	    cw_fr_free(inf);
	    inf = NULL;
	}

	if (inf) {
    	    cw_fr_free(inf);
	    inf = NULL;
	}
	cw_generator_deactivate(&chan->generator);

    }

    pthread_setschedparam(pthread_self(), old_policy, &old_sp);

#if 0
    if (dsp)
        cw_dsp_free(dsp);
#endif
    return ready;
}
/*- End of function --------------------------------------------------------*/

static int txfax_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    fax_state_t 	fax;
    t38_terminal_state_t t38;
    t30_state_t *t30;

    const char *file_name;
    int res = 0;
    int ready;

    int calling_party;
    int verbose;

    struct localuser *u;

    int original_read_fmt;
    int original_write_fmt;

    CW_UNUSED(result);

    /* Basic initial checkings */

    if (chan == NULL)
    {
        cw_log(CW_LOG_WARNING, "Fax transmit channel is NULL. Giving up.\n");
        return -1;
    }

    /* Make sure they are initialized to zero */
    memset(&fax, 0, sizeof(fax));
    memset(&t38, 0, sizeof(t38));

    if (argc < 1)
        return cw_function_syntax(txfax_syntax);

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

    file_name = argv[0];

    while (argv++, --argc) {
        if (strcmp("caller", argv[0]) == 0)
        {
            calling_party = TRUE;
        }
        else if (strcmp("debug", argv[0]) == 0)
        {
            verbose = TRUE;
        }
    }
    /* Done parsing */

    LOCAL_USER_ADD(u);

    if (chan->_state != CW_STATE_UP)
    {
        /* Shouldn't need this, but checking to see if channel is already answered
         * Theoretically the PBX should already have answered before running the app */
        res = cw_answer(chan);
	if (!res)
	{
    	    cw_log(CW_LOG_DEBUG, "Could not answer channel '%s'\n", chan->name);
	    //LOCAL_USER_REMOVE(u);
	    //return res;
	}
    }

    /* Setting read and write formats */
    
    original_read_fmt = chan->readformat;
    if (original_read_fmt != CW_FORMAT_SLINEAR)
    {
        res = cw_set_read_format(chan, CW_FORMAT_SLINEAR);
        if (res < 0)
        {
            cw_log(CW_LOG_WARNING, "Unable to set to linear read mode, giving up\n");
            LOCAL_USER_REMOVE(u);
            return -1;
        }
    }

    original_write_fmt = chan->writeformat;
    if (original_write_fmt != CW_FORMAT_SLINEAR)
    {
        res = cw_set_write_format(chan, CW_FORMAT_SLINEAR);
        if (res < 0)
        {
            cw_log(CW_LOG_WARNING, "Unable to set to linear write mode, giving up\n");
            res = cw_set_read_format(chan, original_read_fmt);
            if (res)
                cw_log(CW_LOG_WARNING, "Unable to restore read format on '%s'\n", chan->name);
            LOCAL_USER_REMOVE(u);
            return -1;
        }
    }

    /* Remove any app level gain adjustments and disable echo cancel. */
    cw_channel_setoption(chan, CW_OPTION_RXGAIN, 0);
    cw_channel_setoption(chan, CW_OPTION_TXGAIN, 0);
    cw_channel_setoption(chan, CW_OPTION_ECHOCANCEL, 0);

    /* This is the main loop */

    ready = TRUE;        

    if (chan->t38_status == T38_NEGOTIATED)
	t30 = t38_terminal_get_t30_state(&t38);
    else
	t30 = fax_get_t30_state(&fax);
    while (ready && ready_to_talk(chan))
    {
        if (ready && chan->t38_status != T38_NEGOTIATED) {
	    t30 = fax_get_t30_state(&fax);
	    ready = txfax_audio(chan, &fax, file_name, calling_party, verbose);
	}

        if (ready && chan->t38_status == T38_NEGOTIATED) {
	    t30 = t38_terminal_get_t30_state(&t38);
	    ready = txfax_t38(chan, &t38, file_name, calling_party, verbose);
	}

	if (chan->t38_status != T38_NEGOTIATING)
	    ready = FALSE; // 1 loop is enough. This could be useful if we want to turn from udptl to RTP later.
    }

    t30_terminate(t30);

    fax_release(&fax);
    t38_terminal_release(&t38);

    /* Maybe we should restore gain and echo cancel settings here? */

    /* Restoring initial channel formats. */
    if (original_read_fmt != CW_FORMAT_SLINEAR)
    {
        if ((res = cw_set_read_format(chan, original_read_fmt)))
            cw_log(CW_LOG_WARNING, "Unable to restore read format on '%s'\n", chan->name);
    }
    if (original_write_fmt != CW_FORMAT_SLINEAR)
    {
        if ((res = cw_set_write_format(chan, original_write_fmt)))
            cw_log(CW_LOG_WARNING, "Unable to restore write format on '%s'\n", chan->name);
    }

    return ready;

}
/*- End of function --------------------------------------------------------*/

static int unload_module(void)
{
    int res = 0;

    res |= cw_unregister_function(txfax_app);
    return res;
}
/*- End of function --------------------------------------------------------*/

static int load_module(void)
{
    if (!faxgen.is_initialized)
        cw_object_init_obj(&faxgen.obj, CW_OBJECT_CURRENT_MODULE, 0);

    txfax_app = cw_register_function(txfax_name, txfax_exec, txfax_synopsis, txfax_syntax, txfax_descrip);
    return 0;
}
/*- End of function --------------------------------------------------------*/


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)

/*- End of file ------------------------------------------------------------*/
