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
*/

#ifndef ___MIDITRACE_H_
#define ___MIDITRACE_H_
#include "mblock.h"
#include "controls.h"

struct timiditycontext_t;
typedef struct _MidiTrace
{
    int offset;		/* sample offset */
    int flush_flag;	/* True while in trace_flush() */

    void (* trace_loop_hook)(void);	/* Hook function for extension */

    /* Delayed event list  (The member is only access in miditrace.c) */
    struct _MidiTraceList *head;
    struct _MidiTraceList *tail;

    /* Memory buffer */
    struct _MidiTraceList *free_list;
    MBlockList pool;
} MidiTrace;

extern void init_midi_trace(struct timiditycontext_t *c);

extern void push_midi_trace0(struct timiditycontext_t *c, void (*f)(void));
extern void push_midi_trace1(struct timiditycontext_t *c, void (*f)(struct timiditycontext_t *c, int), int arg1);
extern void push_midi_trace2(struct timiditycontext_t *c, void (*f)(struct timiditycontext_t *c, int, int), int arg1, int arg2);
extern void push_midi_trace_ce(struct timiditycontext_t *c, void (*f)(CtlEvent *), CtlEvent *ce);
extern void push_midi_time_vp(struct timiditycontext_t *c, int32 start, void (*f)(void *), void *vp);

extern int32 trace_loop(struct timiditycontext_t *c);
extern void trace_flush(struct timiditycontext_t *c);
extern void trace_offset(struct timiditycontext_t *c, int offset);
extern void trace_nodelay(int nodelay);
extern void set_trace_loop_hook(struct timiditycontext_t *c, void (* f)(void));
extern int32 current_trace_samples(struct timiditycontext_t *c);
extern int32 trace_wait_samples(struct timiditycontext_t *c);

#endif /* ___MIDITRACE_H_ */
