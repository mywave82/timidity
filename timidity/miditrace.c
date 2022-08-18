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


    miditrace.c - Midi audio synchronized tracer
    Written by Masanao Izumo <mo@goice.co.jp>
*/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */
#ifdef __POCC__
#include <sys/types.h>
#endif //for off_t
#include <stdio.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <stdlib.h>

#ifndef NO_STRING_H
#include <string.h>
#else
#include <strings.h>
#endif

#include "timidity.h"
#include "common.h"
#include "instrum.h"
#include "playmidi.h"
#include "output.h"
#include "controls.h"
#include "miditrace.h"
#include "wrd.h"
#include "aq.h"

enum trace_argtypes
{
    ARG_VOID,
    ARG_INT,
    ARG_CONTEXT_INT_INT,
    ARG_CE,
    ARGTIME_VP,
};

typedef struct _MidiTraceList
{
    int32 start;	/* Samples to start */
    int argtype;	/* Argument type */

    union { /* Argument */
	int args[2];
	uint16 ui16;
	CtlEvent ce;
	void *v;
    } a;

    union { /* Handler function */
	void (*f0)(void);
	void (*f1)(struct timiditycontext_t *c, int);
	void (*f2)(struct timiditycontext_t *c, int, int);
	void (*fce)(CtlEvent *ce);
	void (*fv)(void *);
    } f;

    struct _MidiTraceList *next; /* Chain link next node */
} MidiTraceList;

static MidiTraceList *new_trace_node(struct timiditycontext_t *c)
{
    MidiTraceList *p;

    if(c->midi_trace.free_list == NULL)
	p = (MidiTraceList *)new_segment(c, &c->midi_trace.pool,
					 sizeof(MidiTraceList));
    else
    {
	p = c->midi_trace.free_list;
	c->midi_trace.free_list = c->midi_trace.free_list->next;
    }
    return p;
}

static void reuse_trace_node(struct timiditycontext_t *c, MidiTraceList *p)
{
    p->next = c->midi_trace.free_list;
    c->midi_trace.free_list = p;
}

static int32 trace_start_time(struct timiditycontext_t *c)
{
    if(play_mode->flag & PF_CAN_TRACE)
	return c->current_sample;
    return -1;
}

static void run_midi_trace(struct timiditycontext_t *c, MidiTraceList *p)
{
    if(!ctl->opened)
	return;

    switch(p->argtype)
    {
      case ARG_VOID:
	p->f.f0();
	break;
      case ARG_INT:
	p->f.f1(c, p->a.args[0]);
	break;
      case ARG_CONTEXT_INT_INT:
	p->f.f2(c, p->a.args[0], p->a.args[1]);
	break;
      case ARGTIME_VP:
	p->f.fv(p->a.v);
	break;
      case ARG_CE:
	p->f.fce(&p->a.ce);
	break;
    }
}

static MidiTraceList *midi_trace_setfunc(struct timiditycontext_t *c, MidiTraceList *node)
{
    MidiTraceList *p;

    if(!ctl->trace_playing || node->start < 0)
    {
	run_midi_trace(c, node);
	return NULL;
    }

    p = new_trace_node(c);
    *p = *node;
    p->next = NULL;

    if(c->midi_trace.head == NULL)
	c->midi_trace.head = c->midi_trace.tail = p;
    else
    {
	c->midi_trace.tail->next = p;
	c->midi_trace.tail = p;
    }

    return p;
}

void push_midi_trace0(struct timiditycontext_t *c, void (*f)(void))
{
    MidiTraceList node;
    if(f == NULL)
	return;
    memset(&node, 0, sizeof(node));
    node.start = trace_start_time(c);
    node.argtype = ARG_VOID;
    node.f.f0 = f;
    midi_trace_setfunc(c, &node);
}

void push_midi_trace1(struct timiditycontext_t *c, void (*f)(struct timiditycontext_t *c, int), int arg1)
{
    MidiTraceList node;
    if(f == NULL)
	return;
    memset(&node, 0, sizeof(node));
    node.start = trace_start_time(c);
    node.argtype = ARG_INT;
    node.f.f1 = f;
    node.a.args[0] = arg1;
    midi_trace_setfunc(c, &node);
}

void push_midi_trace2(struct timiditycontext_t *c, void (*f)(struct timiditycontext_t *c, int, int), int arg1, int arg2)
{
    MidiTraceList node;
    if(f == NULL)
	return;
    memset(&node, 0, sizeof(node));
    node.start = trace_start_time(c);
    node.argtype = ARG_CONTEXT_INT_INT;
    node.f.f2 = f;
    node.a.args[0] = arg1;
    node.a.args[1] = arg2;
    midi_trace_setfunc(c, &node);
}

void push_midi_trace_ce(struct timiditycontext_t *c, void (*f)(CtlEvent *), CtlEvent *ce)
{
    MidiTraceList node;
    if(f == NULL)
	return;
    memset(&node, 0, sizeof(node));
    node.start = trace_start_time(c);
    node.argtype = ARG_CE;
    node.f.fce = f;
    node.a.ce = *ce;
    midi_trace_setfunc(c, &node);
}

void push_midi_time_vp(struct timiditycontext_t *c, int32 start, void (*f)(void *), void *vp)
{
    MidiTraceList node;
    if(f == NULL)
	return;
    memset(&node, 0, sizeof(node));
    node.start = start;
    node.argtype = ARGTIME_VP;
    node.f.fv = f;
    node.a.v = vp;
    midi_trace_setfunc(c, &node);
}

int32 trace_loop(struct timiditycontext_t *c)
{
    int32 cur;
    int ctl_update;

    if(c->midi_trace.trace_loop_hook != NULL)
	c->midi_trace.trace_loop_hook();

    if(c->midi_trace.head == NULL)
	return 0;

    if((cur = current_trace_samples(c)) == -1 || !ctl->trace_playing)
	cur = 0x7fffffff; /* apply all trace event */

    ctl_update = 0;
    while(c->midi_trace.head && cur >= c->midi_trace.head->start
	  && cur > 0) /* privent flying start */
    {
	MidiTraceList *p;

	p = c->midi_trace.head;
	run_midi_trace(c, p);
	if(p->argtype == ARG_CE)
	    ctl_update = 1;
	c->midi_trace.head = c->midi_trace.head->next;
	reuse_trace_node(c, p);
    }

    if(ctl_update)
	ctl_mode_event(c, CTLE_REFRESH, 0, 0, 0);

    if(c->midi_trace.head == NULL)
    {
	c->midi_trace.tail = NULL;
	return 0; /* No more tracing */
    }

    if(!ctl_update)
    {
	if(c->trace_loop_lasttime == cur)
	    c->midi_trace.head->start--;	/* avoid infinite loop */
	c->trace_loop_lasttime = cur;
    }

    return 1; /* must call trace_loop() continued */
}

void trace_offset(struct timiditycontext_t *c, int offset)
{
    c->midi_trace.offset = offset;
}

void trace_flush(struct timiditycontext_t *c)
{
    c->midi_trace.flush_flag = 1;
    wrd_midi_event(c, WRD_START_SKIP, WRD_NOARG);
    while(c->midi_trace.head)
    {
	MidiTraceList *p;

	p = c->midi_trace.head;
	run_midi_trace(c, p);
	c->midi_trace.head = c->midi_trace.head->next;
	reuse_trace_node(c, p);
    }
    wrd_midi_event(c, WRD_END_SKIP, WRD_NOARG);
    reuse_mblock(c, &c->midi_trace.pool);
    c->midi_trace.head = c->midi_trace.tail = c->midi_trace.free_list = NULL;
    ctl_mode_event(c, CTLE_REFRESH, 0, 0, 0);
    c->midi_trace.flush_flag = 0;
}

void set_trace_loop_hook(struct timiditycontext_t *c, void (* f)(void))
{
    c->midi_trace.trace_loop_hook = f;
}

int32 current_trace_samples(struct timiditycontext_t *c)
{
    int32 sp;
    if((sp = aq_samples(c)) == -1)
	return -1;
    return c->midi_trace.offset + aq_samples(c);
}

void init_midi_trace(struct timiditycontext_t *c)
{
    memset(&c->midi_trace, 0, sizeof(c->midi_trace));
    init_mblock(&c->midi_trace.pool);
}

int32 trace_wait_samples(struct timiditycontext_t *c)
{
    int32 s;

    if(c->midi_trace.head == NULL)
	return -1;
    if((s = current_trace_samples(c)) == -1)
	return 0;
    s = c->midi_trace.head->start - s;
    if(s < 0)
	s = 0;
    return s;
}
