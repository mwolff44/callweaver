/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Jeff Ollie
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
 * \brief OGG/Vorbis streams.
 * 
 */
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/module.h"

#define SAMPLES_MAX 160
#define BLOCK_SIZE 4096


struct pvt
{
    FILE *fp;

    /* structures for handling the Ogg container */
    ogg_sync_state     oy;
    ogg_stream_state os;
    ogg_page     og;
    ogg_packet     op;
    
    /* structures for handling Vorbis audio data */
    vorbis_info     vi;
    vorbis_comment     vc;
    vorbis_dsp_state vd;
    vorbis_block     vb;
    
    /*! \brief Indicates whether this filestream is set up for reading or writing. */
    int writing;

    /*! \brief Indicates whether an End of Stream condition has been detected. */
    int eos;

    /*! \brief CallWeaver frame object. */
    struct cw_frame fr;

    /*! \brief Buffer to hold audio data. */
    uint8_t buf[CW_FRIENDLY_OFFSET + SAMPLES_MAX * sizeof(int16_t)];
};


static struct cw_format format;

static const char desc[] = "OGG/Vorbis audio";


/*!
 * \brief Create a new OGG/Vorbis filestream and set it up for reading.
 * \param fd Descriptor that points to on disk storage of the OGG/Vorbis data.
 * \return The new filestream.
 */
static void *ogg_vorbis_open(FILE *fp)
{
    struct pvt *tmp;
    int i;
    int bytes;
    int result;
    char **ptr;
    char *buffer;

    if ((tmp = calloc(1, sizeof(*tmp))))
    {
        tmp->writing = 0;
        tmp->fp = fp;

        ogg_sync_init(&tmp->oy);

        buffer = ogg_sync_buffer(&tmp->oy, BLOCK_SIZE);
        bytes = fread(buffer, 1, BLOCK_SIZE, tmp->fp);

        ogg_sync_wrote(&tmp->oy, bytes);

        result = ogg_sync_pageout(&tmp->oy, &tmp->og);
        if (result != 1)
        {
            if (bytes < BLOCK_SIZE)
                cw_log(CW_LOG_ERROR, "Run out of data... %d %s\n", errno, strerror(errno));
            else
                cw_log(CW_LOG_ERROR, "Input does not appear to be an Ogg bitstream.\n");
            fclose(fp);
            ogg_sync_clear(&tmp->oy);
            free(tmp);
            return NULL;
        }
        
        ogg_stream_init(&tmp->os, ogg_page_serialno(&tmp->og));
        vorbis_info_init(&tmp->vi);
        vorbis_comment_init(&tmp->vc);

        if (ogg_stream_pagein(&tmp->os, &tmp->og) < 0)
        { 
            cw_log(CW_LOG_ERROR, "Error reading first page of Ogg bitstream data.\n");
            fclose(fp);
            ogg_stream_clear(&tmp->os);
            vorbis_comment_clear(&tmp->vc);
            vorbis_info_clear(&tmp->vi);
            ogg_sync_clear(&tmp->oy);
            free(tmp);
            return NULL;
        }
        
        if (ogg_stream_packetout(&tmp->os, &tmp->op) != 1)
        { 
            cw_log(CW_LOG_ERROR, "Error reading initial header packet.\n");
            fclose(fp);
            ogg_stream_clear(&tmp->os);
            vorbis_comment_clear(&tmp->vc);
            vorbis_info_clear(&tmp->vi);
            ogg_sync_clear(&tmp->oy);
            free(tmp);
            return NULL;
        }
        
        if (vorbis_synthesis_headerin(&tmp->vi, &tmp->vc, &tmp->op) < 0)
        { 
            cw_log(CW_LOG_ERROR, "This Ogg bitstream does not contain Vorbis audio data.\n");
            fclose(fp);
            ogg_stream_clear(&tmp->os);
            vorbis_comment_clear(&tmp->vc);
            vorbis_info_clear(&tmp->vi);
            ogg_sync_clear(&tmp->oy);
            free(tmp);
            return NULL;
        }
        
        i = 0;
        while (i < 2)
        {
            while (i < 2)
            {
                result = ogg_sync_pageout(&tmp->oy, &tmp->og);
                if (result == 0)
                    break;
                if (result == 1)
                {
                    ogg_stream_pagein(&tmp->os, &tmp->og);
                    while (i < 2)
                    {
                        result = ogg_stream_packetout(&tmp->os,&tmp->op);
                        if (result == 0)
                            break;
                        if (result < 0)
                        {
                            cw_log(CW_LOG_ERROR, "Corrupt secondary header.  Exiting.\n");
                            fclose(fp);
                            ogg_stream_clear(&tmp->os);
                            vorbis_comment_clear(&tmp->vc);
                            vorbis_info_clear(&tmp->vi);
                            ogg_sync_clear(&tmp->oy);
                            free(tmp);
                            return NULL;
                        }
                        vorbis_synthesis_headerin(&tmp->vi, &tmp->vc, &tmp->op);
                        i++;
                    }
                }
            }

            buffer = ogg_sync_buffer(&tmp->oy, BLOCK_SIZE);
            bytes = fread(buffer, 1, BLOCK_SIZE, tmp->fp);
            if (bytes == 0  &&  i < 2)
            {
                cw_log(CW_LOG_ERROR, "End of file before finding all Vorbis headers!\n");
                fclose(fp);
                ogg_stream_clear(&tmp->os);
                vorbis_comment_clear(&tmp->vc);
                vorbis_info_clear(&tmp->vi);
                ogg_sync_clear(&tmp->oy);
                free(tmp);
                return NULL;
            }
            ogg_sync_wrote(&tmp->oy, bytes);
        }
        
        ptr = tmp->vc.user_comments;
        while (*ptr)
        {
            cw_log(CW_LOG_DEBUG, "OGG/Vorbis comment: %s\n", *ptr);
            ++ptr;
        }
        cw_log(CW_LOG_DEBUG, "OGG/Vorbis bitstream is %d channel, %ldHz\n", tmp->vi.channels, tmp->vi.rate);
        cw_log(CW_LOG_DEBUG, "OGG/Vorbis file encoded by: %s\n", tmp->vc.vendor);

        if (tmp->vi.channels != 1)
        {
            cw_log(CW_LOG_ERROR, "Only monophonic OGG/Vorbis files are currently supported!\n");
            ogg_stream_clear(&tmp->os);
            vorbis_comment_clear(&tmp->vc);
            vorbis_info_clear(&tmp->vi);
            ogg_sync_clear(&tmp->oy);
            free(tmp);
            return NULL;
        }
        

        if (tmp->vi.rate != 8000)
        {
            cw_log(CW_LOG_ERROR, "Only 8000Hz OGG/Vorbis files are currently supported!\n");
            fclose(fp);
            ogg_stream_clear(&tmp->os);
            vorbis_block_clear(&tmp->vb);
            vorbis_dsp_clear(&tmp->vd);
            vorbis_comment_clear(&tmp->vc);
            vorbis_info_clear(&tmp->vi);
            ogg_sync_clear(&tmp->oy);
            free(tmp);
            return NULL;
        }
        
        vorbis_synthesis_init(&tmp->vd, &tmp->vi);
        vorbis_block_init(&tmp->vd, &tmp->vb);
    }

    cw_fr_init_ex(&tmp->fr, CW_FRAME_VOICE, CW_FORMAT_SLINEAR);
    tmp->fr.offset = CW_FRIENDLY_OFFSET;
    tmp->fr.data = &tmp->buf[CW_FRIENDLY_OFFSET];
    return tmp;
}

/*!
 * \brief Create a new OGG/Vorbis filestream and set it up for writing.
 * \param fd File descriptor that points to on-disk storage.
 * \param comment Comment that should be embedded in the OGG/Vorbis file.
 * \return A new filestream.
 */
static void *ogg_vorbis_rewrite(FILE *fp, const char *comment)
{
    ogg_packet header;
    ogg_packet header_comm;
    ogg_packet header_code;
    struct pvt *tmp;

    if ((tmp = calloc(1, sizeof(*tmp))))
    {
        tmp->writing = 1;
        tmp->fp = fp;

        vorbis_info_init(&tmp->vi);

        if (vorbis_encode_init_vbr(&tmp->vi, 1, 8000, 0.4))
        {
            cw_log(CW_LOG_ERROR, "Unable to initialize Vorbis encoder!\n");
            free(tmp);
            return NULL;
        }

        vorbis_comment_init(&tmp->vc);
        vorbis_comment_add_tag(&tmp->vc, (char *)"ENCODER", (char *)"CallWeaver");
        if (comment)
            vorbis_comment_add_tag(&tmp->vc, (char *)"COMMENT", (char *)comment);

        vorbis_analysis_init(&tmp->vd, &tmp->vi);
        vorbis_block_init(&tmp->vd, &tmp->vb);

        ogg_stream_init(&tmp->os, cw_random());

        vorbis_analysis_headerout(&tmp->vd, &tmp->vc, &header, &header_comm, &header_code);
        ogg_stream_packetin(&tmp->os, &header);
        ogg_stream_packetin(&tmp->os, &header_comm);
        ogg_stream_packetin(&tmp->os, &header_code);

        while (!tmp->eos)
        {
            if (ogg_stream_flush(&tmp->os, &tmp->og) == 0)
                break;
            fwrite(tmp->og.header, 1, tmp->og.header_len, tmp->fp);
            fwrite(tmp->og.body, 1, tmp->og.body_len, tmp->fp);
            if (ogg_page_eos(&tmp->og))
                tmp->eos = 1;
        }
    }
    return tmp;
}

/*!
 * \brief Write out any pending encoded data.
 * \param s A OGG/Vorbis filestream.
 */
static void write_stream(struct pvt *pvt)
{
    while (vorbis_analysis_blockout(&pvt->vd, &pvt->vb) == 1)
    {
        vorbis_analysis(&pvt->vb, NULL);
        vorbis_bitrate_addblock(&pvt->vb);
        
        while (vorbis_bitrate_flushpacket(&pvt->vd, &pvt->op))
        {
            ogg_stream_packetin(&pvt->os, &pvt->op);
            while (!pvt->eos)
            {
                if (ogg_stream_pageout(&pvt->os, &pvt->og) == 0)
                    break;
                fwrite(pvt->og.header, 1, pvt->og.header_len, pvt->fp);
                fwrite(pvt->og.body, 1, pvt->og.body_len, pvt->fp);
                if (ogg_page_eos(&pvt->og))
                    pvt->eos = 1;
            }
        }
    }
}

/*!
 * \brief Write audio data from a frame to an OGG/Vorbis filestream.
 * \param s A OGG/Vorbis filestream.
 * \param f An frame containing audio to be written to the filestream.
 * \return -1 ifthere was an error, 0 on success.
 */
static int ogg_vorbis_write(void *data, struct cw_frame *f)
{
    struct pvt *pvt = data;
    int i;
    float **buffer;
    short *fdata;

    if (!pvt->writing)
    {
        cw_log(CW_LOG_ERROR, "This stream is not set up for writing!\n");
        return -1;
    }

    if (f->frametype != CW_FRAME_VOICE)
    {
        cw_log(CW_LOG_WARNING, "Asked to write non-voice frame!\n");
        return -1;
    }
    if (f->subclass != CW_FORMAT_SLINEAR)
    {
        cw_log(CW_LOG_WARNING, "Asked to write non-SLINEAR frame (%d)!\n", f->subclass);
        return -1;
    }
    if (!f->datalen)
        return -1;

    fdata = (short *) f->data;

    buffer = vorbis_analysis_buffer(&pvt->vd, f->samples);

    for (i = 0;  i < f->samples;  i++)
        buffer[0][i] = fdata[i]/32768.f;

    vorbis_analysis_wrote(&pvt->vd, f->samples);

    write_stream(pvt);

    return 0;
}

/*!
 * \brief Close a OGG/Vorbis filestream.
 * \param s A OGG/Vorbis filestream.
 */
static void ogg_vorbis_close(void *data)
{
    struct pvt *pvt = data;

    if (pvt->writing)
    {
        /* Tell the Vorbis encoder that the stream is finished
         * and write out the rest of the data */
        vorbis_analysis_wrote(&pvt->vd, 0);
        write_stream(pvt);
    }

    ogg_stream_clear(&pvt->os);
    vorbis_block_clear(&pvt->vb);
    vorbis_dsp_clear(&pvt->vd);
    vorbis_comment_clear(&pvt->vc);
    vorbis_info_clear(&pvt->vi);

    if (pvt->writing)
        ogg_sync_clear(&pvt->oy);
    
    fclose(pvt->fp);
    free(pvt);
}

/*!
 * \brief Get audio data.
 * \param s An OGG/Vorbis filestream.
 * \param pcm Pointer to a buffere to store audio data in.
 */

static int read_samples(struct pvt *pvt, float ***pcm)
{
    int samples_in;
    int result;
    char *buffer;
    int bytes;

    while (1)
    {
        samples_in = vorbis_synthesis_pcmout(&pvt->vd, pcm);
        if (samples_in > 0)
            return samples_in;
        
        /* The Vorbis decoder needs more data... */
        /* See ifOGG has any packets in the current page for the Vorbis decoder. */
        result = ogg_stream_packetout(&pvt->os, &pvt->op);
        if (result > 0)
        {
            /* Yes OGG had some more packets for the Vorbis decoder. */
            if (vorbis_synthesis(&pvt->vb, &pvt->op) == 0)
                vorbis_synthesis_blockin(&pvt->vd, &pvt->vb);
            continue;
        }

        if (result < 0)
            cw_log(CW_LOG_WARNING, "Corrupt or missing data at this page position; continuing...\n");
        
        /* No more packets left in the current page... */

        if (pvt->eos)
        {
            /* No more pages left in the stream */
            return -1;
        }

        while (!pvt->eos)
        {
            /* See ifOGG has any pages in it's internal buffers */
            result = ogg_sync_pageout(&pvt->oy, &pvt->og);
            if (result > 0)
            {
                /* Yes, OGG has more pages in it's internal buffers,
                   add the page to the stream state */
                result = ogg_stream_pagein(&pvt->os, &pvt->og);
                if (result == 0)
                {
                    /* Yes, got a new,valid page */
                    if (ogg_page_eos(&pvt->og))
                        pvt->eos = 1;
                    break;
                }
                cw_log(CW_LOG_WARNING, "Invalid page in the bitstream; continuing...\n");
            }
            
            if (result < 0)
                cw_log(CW_LOG_WARNING, "Corrupt or missing data in bitstream; continuing...\n");

            /* No, we need to read more data from the file descrptor */
            /* get a buffer from OGG to read the data into */
            buffer = ogg_sync_buffer(&pvt->oy, BLOCK_SIZE);
            /* read more data from the file descriptor */
            bytes = fread(buffer, 1, BLOCK_SIZE, pvt->fp);
            /* Tell OGG how many bytes we actually read into the buffer */
            ogg_sync_wrote(&pvt->oy, bytes);
            if (bytes == 0)
                pvt->eos = 1;
        }
    }
}

/*!
 * \brief Read a frame full of audio data from the filestream.
 * \param s The filestream.
 * \param whennext Number of sample times to schedule the next call.
 * \return A pointer to a frame containing audio data or NULL ifthere is no more audio data.
 */
static struct cw_frame *ogg_vorbis_read(void *data, int *whennext)
{
    struct pvt *pvt = data;
    int16_t *buffer = (int16_t *)pvt->fr.data;
    int clipflag = 0;
    int i;
    int j;
    float **pcm;
    float *mono;
    double accumulator[SAMPLES_MAX];
    int val;
    int samples_in;
    int samples_out = 0;

    for (;;)
    {
        /* See if we have filled up an audio frame yet */
        if (samples_out == SAMPLES_MAX)
            break;

        /* See ifVorbis decoder has some audio data for us ... */
        samples_in = read_samples(pvt, &pcm);
        if (samples_in <= 0)
            break;

        /* Got some audio data from Vorbis... */
        /* Convert the float audio data to 16-bit signed linear */
        clipflag = 0;

        samples_in = samples_in < (SAMPLES_MAX - samples_out)  ?  samples_in  :  (SAMPLES_MAX - samples_out);
  
        for(j = 0;  j < samples_in;  j++)
            accumulator[j] = 0.0;

        for (i = 0;  i < pvt->vi.channels;  i++)
        {
            mono = pcm[i];
            for (j = 0;  j < samples_in;  j++)
                accumulator[j] += mono[j];
        }

        for (j = 0; j < samples_in; j++)
        {
            val =  accumulator[j] * 32767.0 / pvt->vi.channels;
            if (val > 32767)
            {
                val = 32767;
                clipflag = 1;
            }
            if (val < -32768)
            {
                val = -32768;
                clipflag = 1;
            }
            buffer[samples_out + j] = val;
        }
            
        if (clipflag)
            cw_log(CW_LOG_WARNING, "Clipping in frame %ld\n", (long)(pvt->vd.sequence));
        
        /* Tell the Vorbis decoder how many samples we actually used. */
        vorbis_synthesis_read(&pvt->vd, samples_in);
        samples_out += samples_in;
    }

    if (samples_out > 0)
    {
        pvt->fr.datalen = samples_out * sizeof(buffer[0]);
        pvt->fr.samples = samples_out;
        *whennext = samples_out;
        return &pvt->fr;
    }
    return NULL;
}

/*!
 * \brief Trucate an OGG/Vorbis filestream.
 * \param s The filestream to truncate.
 * \return 0 on success, -1 on failure.
 */

static int ogg_vorbis_trunc(void *data)
{
    CW_UNUSED(data);

    cw_log(CW_LOG_WARNING, "Truncation is not supported on OGG/Vorbis streams!\n");
    return -1;
}

/*!
 * \brief Seek to a specific position in an OGG/Vorbis filestream.
 * \param s The filestream to truncate.
 * \param sample_offset New position for the filestream, measured in 8KHz samples.
 * \param whence Location to measure 
 * \return 0 on success, -1 on failure.
 */

static int ogg_vorbis_seek(void *data, long sample_offset, int whence)
{
    CW_UNUSED(data);
    CW_UNUSED(sample_offset);
    CW_UNUSED(whence);

    cw_log(CW_LOG_WARNING, "Seeking is not supported on OGG/Vorbis streams!\n");
    return -1;
}

static long ogg_vorbis_tell(void *data)
{
    CW_UNUSED(data);

    cw_log(CW_LOG_WARNING, "Telling is not supported on OGG/Vorbis streams!\n");
    return -1;
}

static char *ogg_vorbis_getcomment(void *data)
{
    CW_UNUSED(data);

    cw_log(CW_LOG_WARNING, "Getting comments is not supported on OGG/Vorbis streams!\n");
    return NULL;
}

static struct cw_format format =
{
	.name = "ogg_vorbis",
	.exts = "ogg",
	.format = CW_FORMAT_SLINEAR,
	.open = ogg_vorbis_open,
	.rewrite = ogg_vorbis_rewrite,
	.write = ogg_vorbis_write,
	.seek = ogg_vorbis_seek,
	.trunc = ogg_vorbis_trunc,
	.tell = ogg_vorbis_tell,
	.read = ogg_vorbis_read,
	.close = ogg_vorbis_close,
	.getcomment = ogg_vorbis_getcomment,
};


static int load_module(void)
{
	cw_format_register(&format);
	return 0;
}

static int unload_module(void)
{
	cw_format_unregister(&format);
	return 0;
}

MODULE_INFO(load_module, NULL, unload_module, NULL, desc)
