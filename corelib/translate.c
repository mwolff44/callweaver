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
 *
 * \brief Translate via the use of pseudo channels
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <spandsp.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/channel.h"
#include "callweaver/logger.h"
#include "callweaver/translate.h"
#include "callweaver/options.h"
#include "callweaver/frame.h"
#include "callweaver/sched.h"
#include "callweaver/cli.h"
#include "callweaver/term.h"

#define MAX_RECALC 200 /* max sample recalc */

/* This could all be done more efficiently *IF* we chained packets together
   by default, but it would also complicate virtually every application. */
   
OPBX_MUTEX_DEFINE_STATIC(list_lock);
static struct opbx_translator *list = NULL;

struct opbx_translator_dir
{
    struct opbx_translator *step;    /* Next step translator */
    int cost;            /* Complete cost to destination */
};

struct opbx_frame_delivery
{
    struct opbx_frame *f;
    struct opbx_channel *chan;
    int fd;
    struct translator_pvt *owner;
    struct opbx_frame_delivery *prev;
    struct opbx_frame_delivery *next;
};

static struct opbx_translator_dir tr_matrix[MAX_FORMAT][MAX_FORMAT];

struct opbx_trans_pvt
{
    struct opbx_translator *step;
    struct opbx_translator_pvt *state;
    struct opbx_trans_pvt *next;
    struct timeval nextin;
    struct timeval nextout;
};

void opbx_translator_free_path(struct opbx_trans_pvt *p)
{
    struct opbx_trans_pvt *pl;
    struct opbx_trans_pvt *pn;

    pn = p;
    while (pn)
    {
        pl = pn;
        pn = pn->next;
        if (pl->state  &&  pl->step->destroy)
            pl->step->destroy(pl->state);
        free(pl);
    }
}

/* Build a set of translators based upon the given source and destination formats */
struct opbx_trans_pvt *opbx_translator_build_path(int dest, int dest_rate, int source, int source_rate)
{
    struct opbx_trans_pvt *tmpr = NULL;
    struct opbx_trans_pvt *tmp = NULL;
    
    source = bottom_bit(source);
    dest = bottom_bit(dest);
    
    while (source != dest)
    {
        if (!tr_matrix[source][dest].step)
        {
            /* We shouldn't have allocated any memory */
            opbx_log(LOG_WARNING,
                     "No translator path from %s to %s\n", 
                     opbx_getformatname(1 << source),
                     opbx_getformatname(1 << dest));
            return NULL;
        }

        if (tmp)
        {
            tmp->next = malloc(sizeof(*tmp));
            tmp = tmp->next;
        }
        else
        {
            tmp = malloc(sizeof(*tmp));
        }
        if (tmp == NULL)
        {
            opbx_log(LOG_WARNING, "Out of memory\n");
            if (tmpr)
                opbx_translator_free_path(tmpr);    
            return NULL;
        }

        /* Set the root, if it doesn't exist yet... */
        if (tmpr == NULL)
            tmpr = tmp;

        tmp->next = NULL;
        tmp->nextin =
        tmp->nextout = opbx_tv(0, 0);
        tmp->step = tr_matrix[source][dest].step;
        tmp->state = tmp->step->newpvt();
        
        if (!tmp->state)
        {
            opbx_log(LOG_WARNING, "Failed to build translator step from %d to %d\n", source, dest);
            opbx_translator_free_path(tmpr);    
            return NULL;
        }
        
        /* Keep going if this isn't the final destination */
        source = tmp->step->dst_format;
    }
    return tmpr;
}

struct opbx_frame *opbx_translate(struct opbx_trans_pvt *path, struct opbx_frame *f, int consume)
{
    struct opbx_trans_pvt *p;
    struct opbx_frame *out;
    struct timeval delivery;
    int has_timing_info;
    long ts;
    long len;
    int seq_no;
    
    has_timing_info = f->has_timing_info;
    ts = f->ts;
    len = f->len;
    seq_no = f->seq_no;

    p = path;
    /* Feed the first frame into the first translator */
    p->step->framein(p->state, f);
    if (!opbx_tvzero(f->delivery))
    {
        if (!opbx_tvzero(path->nextin))
        {
            /* Make sure this is in line with what we were expecting */
            if (!opbx_tveq(path->nextin, f->delivery))
            {
                /* The time has changed between what we expected and this
                   most recent time on the new packet.  If we have a
                   valid prediction adjust our output time appropriately */
                if (!opbx_tvzero(path->nextout))
                {
                    path->nextout = opbx_tvadd(path->nextout,
                                               opbx_tvsub(f->delivery, path->nextin));
                }
                path->nextin = f->delivery;
            }
        }
        else
        {
            /* This is our first pass.  Make sure the timing looks good */
            path->nextin = f->delivery;
            path->nextout = f->delivery;
        }
        /* Predict next incoming sample */
        path->nextin = opbx_tvadd(path->nextin, opbx_samp2tv(f->samples, 8000));
    }
    delivery = f->delivery;
    if (consume)
        opbx_fr_free(f);
    while (p)
    {
        /* If we get nothing out, return NULL */
        if ((out = p->step->frameout(p->state)) == NULL)
            return NULL;
        /* If there is a next state, feed it in there.  If not,
           return this frame  */
        if (p->next)
        { 
            p->next->step->framein(p->next->state, out);
        }
        else
        {
            if (!opbx_tvzero(delivery))
            {
                /* Regenerate prediction after a discontinuity */
                if (opbx_tvzero(path->nextout))
                    path->nextout = opbx_tvnow();

                /* Use next predicted outgoing timestamp */
                out->delivery = path->nextout;
                
                /* Predict next outgoing timestamp from samples in this
                   frame. */
                path->nextout = opbx_tvadd(path->nextout, opbx_samp2tv( out->samples, 8000));
            }
            else
            {
                out->delivery = opbx_tv(0, 0);
            }
            /* Invalidate prediction if we're entering a silence period */
            if (out->frametype == OPBX_FRAME_CNG)
                path->nextout = opbx_tv(0, 0);

            out->has_timing_info = has_timing_info;
            if (has_timing_info)
            {
                out->ts = ts;
                out->len = len;
                //out->len = opbx_codec_get_samples(out)/8;
                out->seq_no = seq_no;
            }

            return out;
        }
        p = p->next;
    }
    opbx_log(LOG_WARNING, "I should never get here...\n");
    return NULL;
}

static void calc_cost(struct opbx_translator *t, int secs)
{
    int sofar;
    struct opbx_translator_pvt *pvt;
    struct opbx_frame *f;
    struct opbx_frame *out;
    struct timeval start;
    int cost;

    if (secs < 1)
        secs = 1;
    
    /* If they don't make samples, give them a terrible score */
    if (t->sample == NULL)
    {
        opbx_log(LOG_WARNING, "Translator '%s' does not produce sample frames.\n", t->name);
        t->cost = 99999;
        return;
    }
    if ((pvt = t->newpvt()) == NULL)
    {
        opbx_log(LOG_WARNING, "Translator '%s' appears to be broken and will probably fail.\n", t->name);
        t->cost = 99999;
        return;
    }
    start = opbx_tvnow();
    /* Call the encoder until we've processed "secs" seconds of data */
    for (sofar = 0;  sofar < secs*t->dst_rate;  )
    {
        if ((f = t->sample()) == NULL)
        {
            opbx_log(LOG_WARNING, "Translator '%s' failed to produce a sample frame.\n", t->name);
            t->destroy(pvt);
            t->cost = 99999;
            return;
        }
        t->framein(pvt, f);
        opbx_fr_free(f);
        while ((out = t->frameout(pvt)))
        {
            sofar += out->samples;
            opbx_fr_free(out);
        }
    }
    cost = opbx_tvdiff_ms(opbx_tvnow(), start);
    t->destroy(pvt);
    t->cost = cost/secs;
    if (t->cost <= 0)
        t->cost = 1;
}

static void rebuild_matrix(int samples)
{
    struct opbx_translator *t;
    int changed;
    int x;
    int y;
    int z;

    if (option_debug)
        opbx_log(LOG_DEBUG, "Reseting translation matrix\n");
    /* Use the list of translators to build a translation matrix */
    bzero(tr_matrix, sizeof(tr_matrix));
    t = list;
    while (t)
    {
        if (samples)
            calc_cost(t,samples);
      
        if (!tr_matrix[t->src_format][t->dst_format].step
            ||
            tr_matrix[t->src_format][t->dst_format].cost > t->cost)
        {
            tr_matrix[t->src_format][t->dst_format].step = t;
            tr_matrix[t->src_format][t->dst_format].cost = t->cost;
        }
        t = t->next;
    }
    do
    {
        changed = 0;
        /* Don't you just love O(N^3) operations? */
        for (x = 0;  x < MAX_FORMAT;  x++)
        {
            for (y = 0;  y < MAX_FORMAT;  y++)
            {
                if (x != y)
                {
                    for (z = 0;  z < MAX_FORMAT;  z++)
                    {
                        if ((x != z)  &&  (y != z))
                        {
                            if (tr_matrix[x][y].step
                                &&
                                tr_matrix[y][z].step
                                &&
                                    (!tr_matrix[x][z].step
                                    ||
                                    (tr_matrix[x][y].cost + tr_matrix[y][z].cost < tr_matrix[x][z].cost)))
                            {
                                /* We can get from x to z via y with a cost that
                                   is the sum of the transition from x to y and
                                   from y to z */
                                tr_matrix[x][z].step = tr_matrix[x][y].step;
                                tr_matrix[x][z].cost = tr_matrix[x][y].cost + tr_matrix[y][z].cost;
                                if (option_debug)
                                    opbx_log(LOG_DEBUG, "Discovered %d cost path from %s to %s, via %d\n", tr_matrix[x][z].cost, opbx_getformatname(x), opbx_getformatname(z), y);
                                changed++;
                            }
                        }
                    }
                }
            }
        }
    }
    while (changed);
}

static int show_translation(int fd, int argc, char *argv[])
{
#define SHOW_TRANS 11
    int x;
    int y;
    int z;
    char line[120]; /* Assume 120 character wide screen */

    if (argc > 4) 
        return RESULT_SHOWUSAGE;

    if (argv[2]  &&  !strcasecmp(argv[2], "recalc"))
    {
        z = argv[3]  ?  atoi(argv[3])  :  1;

        if (z <= 0)
        {
            opbx_cli(fd,"         C'mon let's be serious here... defaulting to 1.\n");
            z = 1;
        }

        if (z > MAX_RECALC)
        {
            opbx_cli(fd,"         Maximum limit of recalc exceeded by %d, truncating value to %d\n", z - MAX_RECALC,MAX_RECALC);
            z = MAX_RECALC;
        }
        opbx_cli(fd,"         Recalculating Codec Translation (number of sample seconds: %d)\n\n", z);
        rebuild_matrix(z);
    }

    opbx_cli(fd, "         Translation times between formats (in milliseconds)\n");
    opbx_cli(fd, "          Source Format (Rows) Destination Format(Columns)\n\n");
    opbx_mutex_lock(&list_lock);
    for (x = -1;  x < SHOW_TRANS;  x++)
    {
        line[0] = ' ';
        line[1] = '\0';
        for (y = -1;  y < SHOW_TRANS;  y++)
        {
            if (x >= 0  &&  y >= 0  &&  tr_matrix[x][y].step)
                snprintf(line + strlen(line), sizeof(line) - strlen(line), " %5d", (tr_matrix[x][y].cost >= 99999)  ?  tr_matrix[x][y].cost - 99999  :  tr_matrix[x][y].cost);
            else if (((x == -1  &&  y >= 0)  ||  (y == -1  &&  x >= 0)))
                snprintf(line + strlen(line), sizeof(line) - strlen(line), " %5s", opbx_getformatname(1 << (x + y + 1)));
            else if (x != -1  &&  y != -1)
                snprintf(line + strlen(line), sizeof(line) - strlen(line), "     -");
            else
                snprintf(line + strlen(line), sizeof(line) - strlen(line), "      ");
        }
        snprintf(line + strlen(line), sizeof(line) - strlen(line), "\n");
        opbx_cli(fd, line);            
    }
    opbx_mutex_unlock(&list_lock);
    return RESULT_SUCCESS;
}

int opbx_translator_best_choice(int *dst, int *srcs)
{
    /* Calculate our best source format, given costs, and a desired destination */
    int x;
    int y;
    int best = -1;
    int bestdst = 0;
    int cur = 1;
    int besttime = INT_MAX;
    int common;

    if ((common = (*dst) & (*srcs)))
    {
        /* We have a format in common */
        for (y = 0;  y < MAX_FORMAT;  y++)
        {
            if (cur & common)
            {
                /* This is a common format to both.  Pick it if we don't have one already */
                besttime = 0;
                bestdst = cur;
                best = cur;
            }
            cur <<= 1;
        }
    }
    else
    {
        /* We will need to translate */
        opbx_mutex_lock(&list_lock);
        for (y = 0;  y < MAX_FORMAT;  y++)
        {
            if (cur & *dst)
            {
                for (x = 0;  x < MAX_FORMAT;  x++)
                {
                    if ((*srcs & (1 << x))          /* x is a valid source format */
                        &&
                        tr_matrix[x][y].step        /* There's a step */
                        &&
                        (tr_matrix[x][y].cost < besttime))
                    {
                        /* It's better than what we have so far */
                        best = 1 << x;
                        bestdst = cur;
                        besttime = tr_matrix[x][y].cost;
                    }
                }
            }
            cur <<= 1;
        }
        opbx_mutex_unlock(&list_lock);
    }
    if (best > -1)
    {
        *srcs = best;
        *dst = bestdst;
        best = 0;
    }
    return best;
}







/* ************************************************************************** */

static int added_cli = 0;

static char show_trans_usage[] =
"Usage: show translation [recalc] [<recalc seconds>]\n"
"       Displays known codec translators and the cost associated\n"
"with each conversion.  if the argument 'recalc' is supplied along\n"
"with optional number of seconds to test a new test will be performed\n"
"as the chart is being displayed.\n";

static struct opbx_cli_entry show_trans =
{
    { "show", "translation", NULL },
    show_translation,
    "Display translation matrix",
    show_trans_usage
};

int opbx_register_translator(struct opbx_translator *t)
{
    char tmp[120]; /* Assume 120 character wide screen */

    t->src_format = bottom_bit(t->src_format);
    t->dst_format = bottom_bit(t->dst_format);
    if (t->src_format >= MAX_FORMAT)
    {
        opbx_log(LOG_WARNING, "Source format %s is larger than MAX_FORMAT\n", opbx_getformatname(1 << t->src_format));
        return -1;
    }
    if (t->dst_format >= MAX_FORMAT)
    {
        opbx_log(LOG_WARNING, "Destination format %s is larger than MAX_FORMAT\n", opbx_getformatname(1 << t->dst_format));
        return -1;
    }
    calc_cost(t, 1);
    if (option_verbose > 1)
        opbx_verbose(VERBOSE_PREFIX_2 "Registered translator '%s' from format %s to %s, cost %d\n", opbx_term_color(tmp, t->name, COLOR_MAGENTA, COLOR_BLACK, sizeof(tmp)), opbx_getformatname(1 << t->src_format), opbx_getformatname(1 << t->dst_format), t->cost);
    opbx_mutex_lock(&list_lock);
    if (!added_cli)
    {
        opbx_cli_register(&show_trans);
        added_cli++;
    }
    t->next = list;
    list = t;
    rebuild_matrix(0);
    opbx_mutex_unlock(&list_lock);
    return 0;
}

int opbx_unregister_translator(struct opbx_translator *t)
{
    char tmp[120]; /* Assume 120 character wide screen */
    struct opbx_translator *u;
    struct opbx_translator *ul = NULL;
    
    opbx_mutex_lock(&list_lock);
    u = list;
    while (u)
    {
        if (u == t)
        {
            if (ul)
                ul->next = u->next;
            else
                list = u->next;
            if (option_verbose > 1)
                opbx_verbose(VERBOSE_PREFIX_2 "Unregistered translator '%s' from format %s to %s\n", opbx_term_color(tmp, t->name, COLOR_MAGENTA, COLOR_BLACK, sizeof(tmp)), opbx_getformatname(1 << t->src_format), opbx_getformatname(1 << t->dst_format));
            break;
        }
        ul = u;
        u = u->next;
    }
    rebuild_matrix(0);
    opbx_mutex_unlock(&list_lock);
    return (u  ?  0  :  -1);
}


