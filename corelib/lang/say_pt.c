/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * George Konstantoulakis <gkon@inaccessnetworks.com>
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
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <time.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>

#ifdef SOLARIS
#include <iso/limits_iso.h>
#endif

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/channel.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/say.h"
#include "callweaver/lock.h"
#include "callweaver/localtime.h"
#include "callweaver/utils.h"

#include "say.h"

/* 	Extra sounds needed: */
/* 	For feminin all sound files end with F */
/*	100E for 100+ something */
/*	1000000S for plural */
/*	pt-e for 'and' */
static int say_number_full(struct cw_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
    int res = 0;
    int playh = 0;
    int mf = 1;                            /* +1 = male; -1 = female */
    char fn[256] = "";

    if (!num)
        return cw_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

    if (options && !strncasecmp(options, "f",1))
        mf = -1;

    while (!res  &&  num)
    {
        if (num < 0)
        {
            snprintf(fn, sizeof(fn), "digits/minus");
            if (num > INT_MIN)
                num = -num;
            else
                num = 0;
        }
        else if (num < 20)
        {
            if ((num == 1 || num == 2) && (mf < 0))
                snprintf(fn, sizeof(fn), "digits/%dF", num);
            else
                snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num < 100)
        {
            snprintf(fn, sizeof(fn), "digits/%d", (num / 10) * 10);
            if (num % 10)
                playh = 1;
            num = num % 10;
        }
        else if (num < 1000)
        {
            if (num == 100)
                snprintf(fn, sizeof(fn), "digits/100");
            else if (num < 200)
                snprintf(fn, sizeof(fn), "digits/100E");
            else
            {
                if (mf < 0 && num > 199)
                    snprintf(fn, sizeof(fn), "digits/%dF", (num / 100) * 100);
                else
                    snprintf(fn, sizeof(fn), "digits/%d", (num / 100) * 100);
                if (num % 100)
                    playh = 1;
            }
            num = num % 100;
        }
        else if (num < 1000000)
        {
            if (num > 1999)
            {
                res = say_number_full(chan, (num / 1000) * mf, ints, language, options, audiofd, ctrlfd);
                if (res)
                    return res;
            }
            snprintf(fn, sizeof(fn), "digits/1000");
            if ((num % 1000) && ((num % 1000) < 100  || !(num % 100)))
                playh = 1;
            num = num % 1000;
        }
        else if (num < 1000000000)
        {
            res = say_number_full(chan, (num / 1000000), ints, language, options, audiofd, ctrlfd );
            if (res)
                return res;
            if (num < 2000000)
                snprintf(fn, sizeof(fn), "digits/1000000");
            else
                snprintf(fn, sizeof(fn), "digits/1000000S");

            if ((num % 1000000) &&
                    /* no thousands */
                    ((!((num / 1000) % 1000) && ((num % 1000) < 100 || !(num % 100))) ||
                     /* no hundreds and below */
                     (!(num % 1000) && (((num / 1000) % 1000) < 100 || !((num / 1000) % 100))) ) )
                playh = 1;
            num = num % 1000000;
        }
        if (!res)
        {
            if (!cw_streamfile(chan, fn, language))
            {
                if ((audiofd > -1) && (ctrlfd > -1))
                    res = cw_waitstream_full(chan, ints, audiofd, ctrlfd);
                else
                    res = cw_waitstream(chan, ints);
            }
            cw_stopstream(chan);
        }
        if (!res && playh)
        {
            res = wait_file(chan, ints, "digits/pt-e", language);
            cw_stopstream(chan);
            playh = 0;
        }
    }
    return res;
}

static int say_date(struct cw_channel *chan, time_t t, const char *ints, const char *lang)
{
    struct tm tm;
    char fn[256];
    int res = 0;

    cw_localtime(&t, &tm, NULL);
    localtime_r(&t, &tm);
    snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
    if (!res)
        res = wait_file(chan, ints, fn, lang);
    if (!res)
        res = cw_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);
    if (!res)
        res = wait_file(chan, ints, "digits/pt-de", lang);
    snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
    if (!res)
        res = wait_file(chan, ints, fn, lang);
    if (!res)
        res = wait_file(chan, ints, "digits/pt-de", lang);
    if (!res)
        res = cw_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);

    return res;
}

static int say_date_with_format(struct cw_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tz)
{
    struct tm tm;
    int res = 0;
    int offset;
    int sndoffset;
    char sndfile[256];
    char nextmsg[256];

    cw_localtime(&t,&tm,tz);

    for (offset = 0;  format[offset] != '\0';  offset++)
    {
        cw_log(CW_LOG_DEBUG, "Parsing %c (offset %d) in %s\n", format[offset], offset, format);
        switch (format[offset])
        {
            /* NOTE:  if you add more options here, please try to be consistent with strftime(3) */
        case '\'':
            /* Literal name of a sound file */
            sndoffset=0;
            for (sndoffset=0 ; (format[++offset] != '\'') && (sndoffset < 256) ; sndoffset++)
                sndfile[sndoffset] = format[offset];
            sndfile[sndoffset] = '\0';
            snprintf(nextmsg, sizeof(nextmsg), "%s", sndfile);
            res = wait_file(chan, ints, nextmsg, lang);
            break;
        case 'A':
        case 'a':
            /* Sunday - Saturday */
            snprintf(nextmsg, sizeof(nextmsg), "digits/day-%d", tm.tm_wday);
            res = wait_file(chan, ints, nextmsg, lang);
            break;
        case 'B':
        case 'b':
        case 'h':
            /* January - December */
            snprintf(nextmsg, sizeof(nextmsg), "digits/mon-%d", tm.tm_mon);
            res = wait_file(chan, ints, nextmsg, lang);
            break;
        case 'm':
            /* First - Twelfth */
            snprintf(nextmsg, sizeof(nextmsg), "digits/h-%d", tm.tm_mon +1);
            res = wait_file(chan, ints, nextmsg, lang);
            break;
        case 'd':
        case 'e':
            /* First - Thirtyfirst */
            res = cw_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);
            break;
        case 'Y':
            /* Year */
            res = cw_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
            break;
        case 'I':
        case 'l':
            /* 12-Hour */
            if (tm.tm_hour == 0)
            {
                if (format[offset] == 'I')
                    res = wait_file(chan, ints, "digits/pt-ah", lang);
                if (!res)
                    res = wait_file(chan, ints, "digits/pt-meianoite", lang);
            }
            else if (tm.tm_hour == 12)
            {
                if (format[offset] == 'I')
                    res = wait_file(chan, ints, "digits/pt-ao", lang);
                if (!res)
                    res = wait_file(chan, ints, "digits/pt-meiodia", lang);
            }
            else
            {
                if (format[offset] == 'I')
                {
                    res = wait_file(chan, ints, "digits/pt-ah", lang);
                    if ((tm.tm_hour % 12) != 1)
                        if (!res)
                            res = wait_file(chan, ints, "digits/pt-sss", lang);
                }
                if (!res)
                    res = cw_say_number(chan, (tm.tm_hour % 12), ints, lang, "f");
            }
            break;
        case 'H':
        case 'k':
            /* 24-Hour */
            res = cw_say_number(chan, -tm.tm_hour, ints, lang, NULL);
            if (!res)
            {
                if (tm.tm_hour != 0)
                {
                    int rem = tm.tm_hour;
                    if (tm.tm_hour > 20)
                    {
                        res = wait_file(chan,ints, "digits/20",lang);
                        rem -= 20;
                    }
                    if (!res)
                    {
                        snprintf(nextmsg, sizeof(nextmsg), "digits/%d", rem);
                        res = wait_file(chan,ints,nextmsg,lang);
                    }
                }
            }
            break;
        case 'M':
            /* Minute */
            if (tm.tm_min == 0)
            {
                res = wait_file(chan, ints, "digits/pt-hora", lang);
                if (tm.tm_hour != 1)
                    if (!res)
                        res = wait_file(chan, ints, "digits/pt-sss", lang);
            }
            else
            {
                res = wait_file(chan,ints,"digits/pt-e",lang);
                if (!res)
                    res = cw_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
            }
            break;
        case 'P':
        case 'p':
            /* AM/PM */
            if (tm.tm_hour > 12)
                res = wait_file(chan, ints, "digits/p-m", lang);
            else if (tm.tm_hour  && tm.tm_hour < 12)
                res = wait_file(chan, ints, "digits/a-m", lang);
            break;
        case 'Q':
            /* Shorthand for "Today", "Yesterday", or ABdY */
        {
            struct timeval now;
            struct tm tmnow;
            time_t beg_today;

            gettimeofday(&now,NULL);
            cw_localtime(&now.tv_sec,&tmnow,tz);
            /* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
            /* In any case, it saves not having to do cw_mktime() */
            beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
            if (beg_today < t)
            {
                /* Today */
                res = wait_file(chan,ints, "digits/today",lang);
            }
            else if (beg_today - 86400 < t)
            {
                /* Yesterday */
                res = wait_file(chan,ints, "digits/yesterday",lang);
            }
            else
            {
                res = cw_say_date_with_format(chan, t, ints, lang, "Ad 'digits/pt-de' B 'digits/pt-de' Y", tz);
            }
        }
            break;
        case 'q':
            /* Shorthand for "" (today), "Yesterday", A (weekday), or ABdY */
        {
            struct timeval now;
            struct tm tmnow;
            time_t beg_today;

            gettimeofday(&now,NULL);
            cw_localtime(&now.tv_sec,&tmnow,tz);
            /* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
            /* In any case, it saves not having to do cw_mktime() */
            beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
            if (beg_today < t)
            {
                /* Today */
            }
            else if ((beg_today - 86400) < t)
            {
                /* Yesterday */
                res = wait_file(chan,ints, "digits/yesterday",lang);
            }
            else if (beg_today - 86400 * 6 < t)
            {
                /* Within the last week */
                res = cw_say_date_with_format(chan, t, ints, lang, "A", tz);
            }
            else
            {
                res = cw_say_date_with_format(chan, t, ints, lang, "Ad 'digits/pt-de' B 'digits/pt-de' Y", tz);
            }
        }
            break;
        case 'R':
            res = cw_say_date_with_format(chan, t, ints, lang, "H 'digits/pt-e' M", tz);
            break;
        case 'S':
            /* Seconds */
            if (tm.tm_sec == 0)
            {
                snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec);
                res = wait_file(chan,ints,nextmsg,lang);
            }
            else if (tm.tm_sec < 10)
            {
                res = wait_file(chan,ints, "digits/oh",lang);
                if (!res)
                {
                    snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec);
                    res = wait_file(chan,ints,nextmsg,lang);
                }
            }
            else if ((tm.tm_sec < 21) || (tm.tm_sec % 10 == 0))
            {
                snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec);
                res = wait_file(chan,ints,nextmsg,lang);
            }
            else
            {
                int ten;
                int one;

                ten = (tm.tm_sec / 10) * 10;
                one = (tm.tm_sec % 10);
                snprintf(nextmsg, sizeof(nextmsg), "digits/%d", ten);
                res = wait_file(chan,ints,nextmsg,lang);
                if (!res)
                {
                    /* Fifty, not fifty-zero */
                    if (one != 0)
                    {
                        snprintf(nextmsg, sizeof(nextmsg), "digits/%d", one);
                        res = wait_file(chan,ints,nextmsg,lang);
                    }
                }
            }
            break;
        case 'T':
            res = cw_say_date_with_format(chan, t, ints, lang, "HMS", tz);
            break;
        case ' ':
        case '	':
            /* Just ignore spaces and tabs */
            break;
        default:
            /* Unknown character */
            cw_log(CW_LOG_WARNING, "Unknown character in datetime format %s: %c at pos %d\n", format, format[offset], offset);
        }
        /* Jump out on DTMF */
        if (res)
            break;
    }
    return res;
}

static int say_time(struct cw_channel *chan, time_t t, const char *ints, const char *lang)
{
    struct tm tm;
    int res = 0;
    int hour;

    localtime_r(&t, &tm);
    hour = tm.tm_hour;
    if (!res)
        res = cw_say_number(chan, hour, ints, lang, "f");
    if (tm.tm_min)
    {
        if (!res)
            res = wait_file(chan, ints, "digits/pt-e", lang);
        if (!res)
            res = cw_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
    }
    else
    {
        if (!res)
            res = wait_file(chan, ints, "digits/pt-hora", lang);
        if (tm.tm_hour != 1)
            if (!res)
                res = wait_file(chan, ints, "digits/pt-sss", lang);
    }
    if (!res)
        res = cw_say_number(chan, hour, ints, lang, (char *) NULL);
    return res;
}

static int say_datetime(struct cw_channel *chan, time_t t, const char *ints, const char *lang)
{
    struct tm tm;
    char fn[256];
    int res = 0;
    int hour;
    int pm = 0;

    localtime_r(&t, &tm);
    if (!res)
    {
        snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
        res = cw_streamfile(chan, fn, lang);
        if (!res)
            res = cw_waitstream(chan, ints);
    }
    if (!res)
    {
        snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
        res = cw_streamfile(chan, fn, lang);
        if (!res)
            res = cw_waitstream(chan, ints);
    }
    if (!res)
        res = cw_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);

    hour = tm.tm_hour;
    if (!hour)
        hour = 12;
    else if (hour == 12)
        pm = 1;
    else if (hour > 12)
    {
        hour -= 12;
        pm = 1;
    }
    if (!res)
        res = cw_say_number(chan, hour, ints, lang, (char *) NULL);

    if (tm.tm_min > 9)
    {
        if (!res)
            res = cw_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
    }
    else if (tm.tm_min)
    {
        if (!res)
            res = cw_streamfile(chan, "digits/oh", lang);
        if (!res)
            res = cw_waitstream(chan, ints);
        if (!res)
            res = cw_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
    }
    else
    {
        if (!res)
            res = cw_streamfile(chan, "digits/oclock", lang);
        if (!res)
            res = cw_waitstream(chan, ints);
    }
    if (pm)
    {
        if (!res)
            res = cw_streamfile(chan, "digits/p-m", lang);
    }
    else
    {
        if (!res)
            res = cw_streamfile(chan, "digits/a-m", lang);
    }
    if (!res)
        res = cw_waitstream(chan, ints);
    if (!res)
        res = cw_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
    return res;
}

static int say_datetime_from_now(struct cw_channel *chan, time_t t, const char *ints, const char *lang)
{
    int res = 0;
    time_t nowt;
    int daydiff;
    struct tm tm;
    struct tm now;
    char fn[256];

    time(&nowt);
    localtime_r(&t, &tm);
    localtime_r(&nowt, &now);
    daydiff = now.tm_yday - tm.tm_yday;
    if ((daydiff < 0) || (daydiff > 6))
    {
        /* Day of month and month */
        if (!res)
            res = cw_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);
        if (!res)
            res = wait_file(chan, ints, "digits/pt-de", lang);
        snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
        if (!res)
            res = wait_file(chan, ints, fn, lang);

    }
    else if (daydiff)
    {
        /* Just what day of the week */
        snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
        if (!res)
            res = wait_file(chan, ints, fn, lang);
    }	/* Otherwise, it was today */
    snprintf(fn, sizeof(fn), "digits/pt-ah");
    if (!res)
        res = wait_file(chan, ints, fn, lang);
    if (tm.tm_hour != 1)
        if (!res)
            res = wait_file(chan, ints, "digits/pt-sss", lang);
    if (!res)
        res = cw_say_time(chan, t, ints, lang);
    return res;
}

lang_specific_speech_t lang_specific_pt =
{
    "pt",
    say_number_full,
    NULL,
    say_date,
    say_time,
    say_datetime,
    say_datetime_from_now,
    say_date_with_format
};
