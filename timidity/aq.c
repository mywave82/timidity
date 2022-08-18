/*
    TiMidity++ -- MIDI to WAVE converter and player
    Copyright (C) 1999-2002 Masanao Izumo <mo@goice.co.jp>
    Copyright (C) 1995 Tuukka Toivonen <tt@cgs.fi>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    aq.c - Audio queue.
	      Written by Masanao Izumo <mo@goice.co.jp>
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef __POCC__
#include <sys/types.h>
#endif //for off_t
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#ifndef NO_STRING_H
#include <string.h>
#else
#include <strings.h>
#endif

#include "timidity.h"
#include "common.h"
#include "output.h"
#include "aq.h"
#include "timer.h"
#include "controls.h"
#include "miditrace.h"
#include "instrum.h"
#include "playmidi.h"

#define TEST_SPARE_RATE 0.9
#define MAX_BUCKET_TIME 0.2
#define MAX_FILLED_TIME 2.0

static void alloc_soft_queue(struct timiditycontext_t *c);
static void set_bucket_size(struct timiditycontext_t *c, int size);
static int add_play_bucket(struct timiditycontext_t *c, const char *buf, int n);
static void reuse_audio_bucket(struct timiditycontext_t *c, AudioBucket *bucket);
static AudioBucket *next_allocated_bucket(struct timiditycontext_t *c);
static void flush_buckets(struct timiditycontext_t *c);
static int aq_fill_one(struct timiditycontext_t *c);
static void aq_wait_ticks(struct timiditycontext_t *c);
static int32 estimate_queue_size(struct timiditycontext_t *c);

/* effect.c */
extern void init_effect(struct timiditycontext_t *c);
extern void do_effect(struct timiditycontext_t *c, int32* buf, int32 count);

int aq_calc_fragsize(struct timiditycontext_t *c)
{
    int ch, bps, bs;
    double dq, bt;

    if(play_mode->encoding & PE_MONO)
	ch = 1;
    else
	ch = 2;
    if(play_mode->encoding & PE_24BIT)
	bps = ch * 3;
    else if(play_mode->encoding & PE_16BIT)
	bps = ch * 2;
    else
	bps = ch;

    bs = audio_buffer_size * bps;
    dq = play_mode->rate * MAX_FILLED_TIME * bps;
    while(bs * 2 > dq)
	bs /= 2;

    bt = (double)bs / bps / play_mode->rate;
    while(bt > MAX_BUCKET_TIME)
    {
	bs /= 2;
	bt = (double)bs / bps / play_mode->rate;
    }

    return bs;
}

void aq_setup(struct timiditycontext_t *c)
{
    int ch, frag_size;

    /* Initialize Bps, bucket_size, device_qsize, and bucket_time */

    if(play_mode->encoding & PE_MONO)
	ch = 1;
    else
	ch = 2;
    if(play_mode->encoding & PE_24BIT)
	c->Bps = 3 * ch;
    else if(play_mode->encoding & PE_16BIT)
	c->Bps = 2 * ch;
    else
	c->Bps = ch;

    if(play_mode->acntl(PM_REQ_GETFRAGSIZ, &frag_size) == -1)
	frag_size = audio_buffer_size * c->Bps;
    set_bucket_size(c, frag_size);
    c->bucket_time = (double)c->bucket_size / c->Bps / play_mode->rate;

    if(IS_STREAM_TRACE)
    {
	if(play_mode->acntl(PM_REQ_GETQSIZ, &c->device_qsize) == -1)
	    c->device_qsize = estimate_queue_size(c);
	if(c->bucket_size * 2 > c->device_qsize) {
	  ctl->cmsg(CMSG_WARNING, VERB_VERBOSE,
		    "Warning: Audio buffer is too small. (bucket_size %d * 2 > device_qsize %d)", (int)c->bucket_size, (int)c->device_qsize);
	  c->device_qsize = 0;
	} else {
	  c->device_qsize -= c->device_qsize % c->Bps; /* Round Bps */
	  ctl->cmsg(CMSG_INFO, VERB_DEBUG,
		    "Audio device queue size: %d bytes", c->device_qsize);
	  ctl->cmsg(CMSG_INFO, VERB_DEBUG,
		    "Write bucket size: %d bytes (%d msec)",
		    c->bucket_size, (int)(c->bucket_time * 1000 + 0.5));
	}
    }
    else
    {
	c->device_qsize = 0;
	free_soft_queue(c);
	c->nbuckets = 0;
    }

    init_effect(c);
    c->aq_add_count = 0;
}

void aq_set_soft_queue(struct timiditycontext_t *c, double soft_buff_time, double fill_start_time)
{
    int nb;

    /* for re-initialize */
    if(soft_buff_time < 0)
	soft_buff_time = c->aq__last_soft_buff_time;
    if(fill_start_time < 0)
	fill_start_time = c->aq__last_fill_start_time;

    nb = (int)(soft_buff_time / c->bucket_time);
    if(nb == 0)
	c->aq_start_count = 0;
    else
	c->aq_start_count = (int32)(fill_start_time * play_mode->rate);
    c->aq_fill_buffer_flag = (c->aq_start_count > 0);

    if(c->nbuckets != nb)
    {
	c->nbuckets = nb;
	alloc_soft_queue(c);
    }

    c->aq__last_soft_buff_time = soft_buff_time;
    c->aq__last_fill_start_time = fill_start_time;
}

/* Estimates the size of audio device queue.
 * About sun audio, there are long-waiting if buffer of device audio is full.
 * So it is impossible to completely estimate the size.
 */
static int32 estimate_queue_size(struct timiditycontext_t *c)
{
    char *nullsound;
    double tb, init_time, chunktime;
    int32 qbytes, max_qbytes;
    int ntries;

    nullsound = (char *)safe_malloc(c->bucket_size);
    memset(nullsound, 0, c->bucket_size);
    if(play_mode->encoding & (PE_ULAW|PE_ALAW))
	general_output_convert((int32 *)nullsound, c->bucket_size/c->Bps);
    tb = play_mode->rate * c->Bps * TEST_SPARE_RATE;
    ntries = 1;
    max_qbytes = play_mode->rate * MAX_FILLED_TIME * c->Bps;

  retry:
    chunktime = (double)c->bucket_size / c->Bps / play_mode->rate;
    qbytes = 0;

    init_time = get_current_calender_time();	/* Start */
    for(;;)
    {
	double start, diff;

	start = get_current_calender_time();
	if(start - init_time > 1.0) /* ?? */
	{
	    ctl->cmsg(CMSG_WARNING, VERB_DEBUG,
		      "Warning: Audio test is terminated");
	    break;
	}
	play_mode->output_data(c, nullsound, c->bucket_size);
	diff = get_current_calender_time() - start;

	if(diff > chunktime/2 || qbytes > 1024*512 || chunktime < diff)
	    break;
	qbytes += (int32)((chunktime - diff) * tb);

	if(qbytes > max_qbytes)
	{
	    qbytes = max_qbytes;
	    break;
	}
    }
    play_mode->acntl(PM_REQ_DISCARD, NULL);

    if(c->bucket_size * 2 > qbytes)
    {
	if(ntries == 4)
	{
	    ctl->cmsg(CMSG_ERROR, VERB_NOISY,
		      "Can't estimate audio queue length");
	    set_bucket_size(c, audio_buffer_size * c->Bps);
	    free(nullsound);
	    return 2 * audio_buffer_size * c->Bps;
	}

	ctl->cmsg(CMSG_WARNING, VERB_DEBUG,
		  "Retry to estimate audio queue length (%d times)",
		  ntries);
	set_bucket_size(c, c->bucket_size / 2);
	ntries++;
	goto retry;
    }

    free(nullsound);

    return qbytes;
}

/* Send audio data to play_mode->output_data() */
static int aq_output_data(struct timiditycontext_t *c, char *buff, int nbytes)
{
    int i;

    c->aq_play_counter += nbytes / c->Bps;

    while(nbytes > 0)
    {
	i = nbytes;
	if(i > c->bucket_size)
	    i = c->bucket_size;
	if(play_mode->output_data(c, buff, i) == -1)
	    return -1;
	nbytes -= i;
	buff += i;
    }

    return 0;
}

int aq_add(struct timiditycontext_t *c, int32 *samples, int32 count)
{
    int32 nbytes, i;
    char *buff;

    if(!(play_mode->flag & PF_PCM_STREAM))
	return 0;

    if(!count)
    {
	if(!c->aq_fill_buffer_flag)
	    return aq_fill_nonblocking(c);
	return 0;
    }

    c->aq_add_count += count;
    do_effect(c, samples, count);
    nbytes = general_output_convert(samples, count);
    buff = (char *)samples;

    if(c->device_qsize == 0)
      return play_mode->output_data(c, buff, nbytes);

    c->aq_fill_buffer_flag = (c->aq_add_count <= c->aq_start_count);

    if(!c->aq_fill_buffer_flag)
	if(aq_fill_nonblocking(c) == -1)
	    return -1;

    if(!ctl->trace_playing)
    {
	while((i = add_play_bucket(c, buff, nbytes)) < nbytes)
	{
	    buff += i;
	    nbytes -= i;
	    if(c->aq_head && c->aq_head->len == c->bucket_size)
	    {
		if(aq_fill_one(c) == -1)
		    return -1;
	    }
	    c->aq_fill_buffer_flag = 0;
	}
	return 0;
    }

    trace_loop(c);
    while((i = add_play_bucket(c, buff, nbytes)) < nbytes)
    {
	/* Software buffer is full.
	 * Write one bucket to audio device.
	 */
	buff += i;
	nbytes -= i;
	aq_wait_ticks(c);
	trace_loop(c);
	if(aq_fill_nonblocking(c) == -1)
	    return -1;
	c->aq_fill_buffer_flag = 0;
    }
    return 0;
}

static void set_bucket_size(struct timiditycontext_t *c, int size)
{
    if (size == c->bucket_size)
	return;
    c->bucket_size = size;
    if (c->nbuckets != 0)
	alloc_soft_queue(c);
}

/* alloc_soft_queue() (re-)initializes audio buckets. */
static void alloc_soft_queue(struct timiditycontext_t *c)
{
    int i;
    char *base;

    free_soft_queue(c);

    c->aq_base_buckets = (AudioBucket *)safe_malloc(c->nbuckets * sizeof(AudioBucket));
    base = (char *)safe_malloc(c->nbuckets * c->bucket_size);
    for(i = 0; i < c->nbuckets; i++)
	c->aq_base_buckets[i].data = base + i * c->bucket_size;
    flush_buckets(c);
}

void free_soft_queue(struct timiditycontext_t *c)
{
    if(c->aq_base_buckets)
    {
	free(c->aq_base_buckets[0].data);
	free(c->aq_base_buckets);
	c->aq_base_buckets = NULL;
    }
}

/* aq_fill_one() transfers one audio bucket to device. */
static int aq_fill_one(struct timiditycontext_t *c)
{
    AudioBucket *tmp;

    if(c->aq_head == NULL)
	return 0;
    if(aq_output_data(c, c->aq_head->data, c->bucket_size) == -1)
	return -1;
    tmp = c->aq_head;
    c->aq_head = c->aq_head->next;
    reuse_audio_bucket(c, tmp);
    return 0;
}

/* aq_fill_nonblocking() transfers some audio buckets to device.
 * This function is non-blocking.  But it is possible to block because
 * of miss-estimated aq_fillable() calculation.
 */
int aq_fill_nonblocking(struct timiditycontext_t *c)
{
    int32 i, nfills;
    AudioBucket *tmp;

    if(c->aq_head == NULL || c->aq_head->len != c->bucket_size || !IS_STREAM_TRACE)
	return 0;

    nfills = (aq_fillable(c) * c->Bps) / c->bucket_size;
    for(i = 0; i < nfills; i++)
    {
	if(c->aq_head == NULL || c->aq_head->len != c->bucket_size)
	    break;
	if(aq_output_data(c, c->aq_head->data, c->bucket_size) == -1)
	    return RC_ERROR;
	tmp = c->aq_head;
	c->aq_head = c->aq_head->next;
	reuse_audio_bucket(c, tmp);
    }
    return 0;
}

int32 aq_samples(struct timiditycontext_t *c)
{
    double realtime, es;
    int s;

    if(play_mode->acntl(PM_REQ_GETSAMPLES, &s) != -1)
    {
	/* Reset counter & timer */
	if(c->aq_play_counter)
	{
	    c->aq_play_start_time = get_current_calender_time();
	    c->aq_play_offset_counter = s;
	    c->aq_play_counter = 0;
	}
	return s;
    }

    if(!IS_STREAM_TRACE)
	return -1;

    realtime = get_current_calender_time();
    if(c->aq_play_counter == 0)
    {
	c->aq_play_start_time = realtime;
	return c->aq_play_offset_counter;
    }
    es = play_mode->rate * (realtime - c->aq_play_start_time);
    if(es >= c->aq_play_counter)
    {
	/* Ouch!
	 * Audio device queue may be empty!
	 * Reset counters.
	 */

	c->aq_play_offset_counter += c->aq_play_counter;
	c->aq_play_counter = 0;
	c->aq_play_start_time = realtime;
	return c->aq_play_offset_counter;
    }

    return (int32)es + c->aq_play_offset_counter;
}

int32 aq_filled(struct timiditycontext_t *c)
{
    double realtime, es;
    int filled;

    if(!IS_STREAM_TRACE)
	return 0;

    if(play_mode->acntl(PM_REQ_GETFILLED, &filled) != -1)
      return filled;

    realtime = get_current_calender_time();
    if(c->aq_play_counter == 0)
    {
	c->aq_play_start_time = realtime;
	return 0;
    }
    es = play_mode->rate * (realtime - c->aq_play_start_time);
    if(es >= c->aq_play_counter)
    {
	/* out of play counter */
	c->aq_play_offset_counter += c->aq_play_counter;
	c->aq_play_counter = 0;
	c->aq_play_start_time = realtime;
	return 0;
    }
    return c->aq_play_counter - (int32)es;
}

int32 aq_soft_filled(struct timiditycontext_t *c)
{
    int32 bytes;
    AudioBucket *cur;

    bytes = 0;
    for(cur = c->aq_head; cur != NULL; cur = cur->next)
	bytes += cur->len;
    return bytes / c->Bps;
}

int32 aq_fillable(struct timiditycontext_t *c)
{
    int fillable;
    if(!IS_STREAM_TRACE)
	return 0;
    if(play_mode->acntl(PM_REQ_GETFILLABLE, &fillable) != -1)
	return fillable;
    return c->device_qsize / c->Bps - aq_filled(c);
}

double aq_filled_ratio(struct timiditycontext_t *c)
{
    double ratio;

    if(!IS_STREAM_TRACE)
	return 1.0;
    ratio = (double)aq_filled(c) * c->Bps / c->device_qsize;
    if(ratio > 1.0)
	return 1.0; /* for safety */
    return ratio;
}

int aq_get_dev_queuesize(struct timiditycontext_t *c)
{
    if(!IS_STREAM_TRACE)
	return 0;
    return c->device_qsize / c->Bps;
}

int aq_soft_flush(struct timiditycontext_t *c)
{
    int rc;

    while(c->aq_head)
    {
	if(c->aq_head->len < c->bucket_size)
	{
	    /* Add silence code */
	    memset (c->aq_head->data + c->aq_head->len, 0, c->bucket_size - c->aq_head->len);
	    c->aq_head->len = c->bucket_size;
	}
	if(aq_fill_one(c) == -1)
	    return RC_ERROR;
	trace_loop(c);
	rc = check_apply_control(c);
	if(RC_IS_SKIP_FILE(rc))
	{
	    play_mode->acntl(PM_REQ_DISCARD, NULL);
	    flush_buckets(c);
	    return rc;
	}
    }
    play_mode->acntl(PM_REQ_OUTPUT_FINISH, NULL);
    return RC_NONE;
}

int aq_flush(struct timiditycontext_t *c, int discard)
{
    int rc;
    int more_trace;

    /* to avoid infinite loop */
    double t, timeout_expect;

    c->aq_add_count = 0;
    init_effect(c);

    if(discard)
    {
	trace_flush(c);
	if(play_mode->acntl(PM_REQ_DISCARD, NULL) != -1)
	{
	    flush_buckets(c);
	    return RC_NONE;
	}
	ctl->cmsg(CMSG_ERROR, VERB_NORMAL,
		  "ERROR: Can't discard audio buffer");
    }

    if(!IS_STREAM_TRACE)
    {
	play_mode->acntl(PM_REQ_FLUSH, NULL);
	c->aq_play_counter = c->aq_play_offset_counter = 0;
	return RC_NONE;
    }

    rc = aq_soft_flush(c);
    if(RC_IS_SKIP_FILE(rc))
	return rc;

    more_trace = 1;
    t = get_current_calender_time();
    timeout_expect = t + (double)aq_filled(c) / play_mode->rate;

    while(more_trace || aq_filled(c) > 0)
    {
	rc = check_apply_control(c);
	if(RC_IS_SKIP_FILE(rc))
	{
	    play_mode->acntl(PM_REQ_DISCARD, NULL);
	    flush_buckets(c);
	    return rc;
	}
	more_trace = trace_loop(c);

	t = get_current_calender_time();
	if(t >= timeout_expect - 0.1)
	  break;

	if(!more_trace)
	  usleep((unsigned long)((timeout_expect - t) * 1000000));
	else
	  aq_wait_ticks(c);
    }

    trace_flush(c);
    play_mode->acntl(PM_REQ_FLUSH, NULL);
    flush_buckets(c);
    return RC_NONE;
}

/* Wait a moment */
static void aq_wait_ticks(struct timiditycontext_t *c)
{
    int32 trace_wait, wait_samples;

    if(c->device_qsize == 0 ||
       (trace_wait = trace_wait_samples(c)) == 0)
	return; /* No wait */
    wait_samples = (c->device_qsize / c->Bps) / 5; /* 20% */
    if(trace_wait != -1 && /* There are more trace events */
       trace_wait < wait_samples)
	wait_samples = trace_wait;
    usleep((unsigned int)((double)wait_samples / play_mode->rate * 1000000.0));
}

/* add_play_bucket() attempts to add buf to audio bucket.
 * It returns actually added bytes.
 */
static int add_play_bucket(struct timiditycontext_t *c, const char *buf, int n)
{
    int total;

    if(n == 0)
	return 0;

    if(!c->nbuckets) {
      play_mode->output_data(c, (char *)buf, n);
      return n;
    }

    if(c->aq_head == NULL)
	c->aq_head = c->aq_tail = next_allocated_bucket(c);

    total = 0;
    while(n > 0)
    {
	int i;

	if(c->aq_tail->len == c->bucket_size)
	{
	    AudioBucket *b;
	    if((b = next_allocated_bucket(c)) == NULL)
		break;
	    if(c->aq_head == NULL)
		c->aq_head = c->aq_tail = b;
	    else
		c->aq_tail = c->aq_tail->next = b;
	}

	i = c->bucket_size - c->aq_tail->len;
	if(i > n)
	    i = n;
	memcpy(c->aq_tail->data + c->aq_tail->len, buf + total, i);
	total += i;
	n     -= i;
	c->aq_tail->len += i;
    }

    return total;
}

/* Flush and clear audio bucket */
static void flush_buckets(struct timiditycontext_t *c)
{
    int i;

    c->aq_allocated_bucket_list = NULL;
    for(i = 0; i < c->nbuckets; i++)
	reuse_audio_bucket(c, &c->aq_base_buckets[i]);
    c->aq_head = c->aq_tail = NULL;
    c->aq_fill_buffer_flag = (c->aq_start_count > 0);
    c->aq_play_counter = c->aq_play_offset_counter = 0;
}

/* next_allocated_bucket() gets free bucket.  If all buckets is used, it
 * returns NULL.
 */
static AudioBucket *next_allocated_bucket(struct timiditycontext_t *c)
{
    AudioBucket *b;

    if(c->aq_allocated_bucket_list == NULL)
	return NULL;
    b = c->aq_allocated_bucket_list;
    c->aq_allocated_bucket_list = c->aq_allocated_bucket_list->next;
    b->len = 0;
    b->next = NULL;
    return b;
}

/* Reuse specified bucket */
static void reuse_audio_bucket(struct timiditycontext_t *c, AudioBucket *bucket)
{
    bucket->next = c->aq_allocated_bucket_list;
    c->aq_allocated_bucket_list = bucket;
}
