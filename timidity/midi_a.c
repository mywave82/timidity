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

    midi_a.c - write midi file

    Author: Stas Sergeev <stsp@users.sourceforge.net>
    Based on midi-writer code by Rober Komar <rkomar@telus.net>
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif				/* HAVE_CONFIG_H */

#ifdef HAVE_OPEN_MEMSTREAM
#define _GNU_SOURCE
#endif

#include <stdio.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif				/* HAVE_UNISTD_H */
#ifndef NO_STRING_H
#include <string.h>
#else
#include <strings.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#ifndef WIN32
#include <arpa/inet.h>
#else
#include <winsock.h>
#endif
#ifdef HAVE_SYS_SOUNDCARD_H
#include <sys/soundcard.h>
#else
#include "server_defs.h"
#endif /* HAVE_SYS_SOUNDCARD_H */
#ifndef __GNUC__
#include <stdarg.h>
#endif
#ifdef WIN32
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#endif

#include "timidity.h"
#include "common.h"
#include "instrum.h"
#include "playmidi.h"
#include "readmidi.h"
#include "output.h"
#include "controls.h"

#define IGNORE_TEMPO_EVENTS 0

static int open_output(void);
static void close_output(void);
static int acntl(int request, void *arg);

static char *midibuf;
static size_t midi_pos;
#ifdef HAVE_OPEN_MEMSTREAM
static FILE *fp;
#else
static size_t midibuf_size;
#define fflush(x) do {} while (0)
#define fclose(x) do {} while (0)
#endif

static long track_size_pos;
static double last_time;

static int tempo;
static int ticks_per_quarter_note;
#define TICKS_OFFSET 12

#define dmp midi_play_mode

PlayMode dmp = {
    DEFAULT_RATE, 0, PF_MIDI_EVENT,
    -1,
    {0, 0, 0, 0, 0},
    "Write MIDI file", 'm',
    NULL,
    open_output,
    close_output,
    NULL,
    acntl
};

static size_t m_fwrite(const void *ptr, size_t size);
#ifdef __GNUC__
#define CARR(...) ((uint8_t []) {__VA_ARGS__})
#define M_FWRITE(a, ...) m_fwrite(CARR(__VA_ARGS__), sizeof(CARR(__VA_ARGS__)))
#else
void M_FWRITE(int n, ...)
{
	uint8 a;
	va_list ap;
	va_start(ap, n);
	while(n--){
		a=(uint8)va_arg(ap, int);
		m_fwrite(&a, sizeof(uint8));
	};
	va_end(ap);
}
	
#endif
#define M_FWRITE_STR(s) m_fwrite((s), sizeof(s) - 1)

static size_t m_fwrite(const void *ptr, size_t size)
{
#ifdef HAVE_OPEN_MEMSTREAM
    return fwrite(ptr, size, 1, fp);
#else
    if (midi_pos + size > midibuf_size) {
	size_t new_len = (midi_pos + size) * 2;
	midibuf = safe_realloc(midibuf, new_len);
	midibuf_size = new_len;
    }
    memcpy(midibuf + midi_pos, ptr, size);
    midi_pos += size;
    return 1;
#endif
}

#ifndef HAVE_OPEN_MEMSTREAM
static void my_open_memstream(void)
{
    midibuf_size = 1024;
    midibuf = safe_malloc(midibuf_size);
    midi_pos = 0;
}
#endif

static void write_midi_header(void)
{
    /* Write out MID file header.
     * The file will have a single track, with the configured number of
     * ticks per quarter note.
     */
    M_FWRITE_STR("MThd");
    M_FWRITE_STR("\0\0\0\6");		/* header size */
    M_FWRITE_STR("\0\0");		/* single track format */
    M_FWRITE_STR("\0\1");		/* #tracks = 1 */
    M_FWRITE_STR("\0\0");		/* #ticks / quarter note written later */
}

static void finalize_midi_header(void)
{
    unsigned short tpqn = htons(ticks_per_quarter_note);

    fflush(fp);
    memcpy(midibuf + TICKS_OFFSET, &tpqn, 2);	/* #ticks / quarter note */
}

static void set_tempo(void)
{
    M_FWRITE_STR("\xff\x51\3");
    M_FWRITE(3, tempo >> 16, tempo >> 8, tempo);
}

static void set_time_sig(void)
{
    /* Set the time sig to 4/4 */
    M_FWRITE_STR("\xff\x58\4\4\x2\x18\x08");
}

static void midout_write_delta_time(int32 time)
{
    int32 delta_time;
    unsigned char c[4];
    int idx;
    int started_printing = 0;

#if !IGNORE_TEMPO_EVENTS
    double div;
    delta_time = time - last_time;
    div = (double)tempo * (double)play_mode->rate /
	(double)(ticks_per_quarter_note * 1000000);
    delta_time /= div;
    last_time += delta_time * div;
#else
    delta_time = time - last_time;
    last_time += delta_time;
#endif

    /* We have to divide the number of ticks into 7-bit segments, and only write
     * the non-zero segments starting with the most significant (except for the
     * least significant segment, which we always write).  The most significant bit
     * is set to 1 in all but the least significant segment.
     */
    c[0] = (delta_time >> 21) & 0x7f;
    c[1] = (delta_time >> 14) & 0x7f;
    c[2] = (delta_time >> 7) & 0x7f;
    c[3] = (delta_time) & 0x7f;

    for (idx = 0; idx < 3; idx++) {
	if (started_printing || c[idx]) {
	    started_printing = 1;
	    M_FWRITE(1, c[idx] | 0x80);
	}
    }
    M_FWRITE(1, c[3]);
}

static void start_midi_track(void)
{
    /* Write out track header.
     * The track will have a large length (0x7fffffff) because we don't know at
     * this time how big it will really be.
     */
    M_FWRITE_STR("MTrk");
    fflush(fp);
    track_size_pos = midi_pos;
    M_FWRITE_STR("\x7f\xff\xff\xff");	/* #chunks */

    last_time = 0;

#if !IGNORE_TEMPO_EVENTS
    tempo = 500000;
#else
    tempo = (ticks_per_quarter_note * 1000000) /
	(double)play_mode->rate;
#endif

    midout_write_delta_time(0);
    set_tempo();
    midout_write_delta_time(0);
    set_time_sig();
}

static void end_midi_track(void)
{
    int32 track_bytes;
    /* Send (with delta-time of 0) "0xff 0x2f 0x0" to finish the track. */
    M_FWRITE_STR("\0\xff\x2f\0");

    fflush(fp);

    track_bytes = htonl(midi_pos - track_size_pos - 4);
    memcpy(midibuf + track_size_pos, &track_bytes, 4);
}

static int open_output(void)
{
#ifdef HAVE_OPEN_MEMSTREAM
    fp = open_memstream(&midibuf, &midi_pos);
#else
    my_open_memstream();
#endif

    ticks_per_quarter_note = 144;
    write_midi_header();

    return 0;
}

static void close_output(void)
{
    finalize_midi_header();

    fclose(fp);
    if (dmp.name) {
	if (strcmp(dmp.name, "-") == 0)
	    dmp.fd = STDOUT_FILENO;
	else
#ifndef WIN32
	    dmp.fd = creat(dmp.name, 0666);
#else
#ifdef __DMC__
		dmp.fd = open(dmp.name, O_BINARY|O_CREAT|O_WRONLY,S_IREAD|S_IWRITE);
#else
        dmp.fd = _open(dmp.name, _O_BINARY|_O_CREAT|_O_WRONLY,_S_IREAD|_S_IWRITE);
#endif
#endif
	if (dmp.fd != -1) {
	    std_write(dmp.fd, midibuf, midi_pos);
	    if (strcmp(dmp.name, "-") != 0)
		close(dmp.fd);
	}
	dmp.fd = -1;
    }
    free(midibuf);
}

static void midout_noteon(int chn, int note, int vel, int32 time)
{
    midout_write_delta_time(time);
    M_FWRITE(3,(chn & 0x0f) | MIDI_NOTEON, note & 0x7f, vel & 0x7f);
}

static void midout_noteoff(int chn, int note, int vel, int32 time)
{
    midout_write_delta_time(time);
    M_FWRITE(3, (chn & 0x0f) | MIDI_NOTEOFF, note & 0x7f, vel & 0x7f);
}

static void midout_control(int chn, int control, int value, int32 time)
{
    midout_write_delta_time(time);
    M_FWRITE(3, (chn & 0x0f) | MIDI_CTL_CHANGE, control & 0x7f, value & 0x7f);
}

static void midout_keypressure(int chn, int control, int value, int32 time)
{
    midout_write_delta_time(time);
    M_FWRITE(3, (chn & 0x0f) | MIDI_KEY_PRESSURE, control & 0x7f, value & 0x7f);
}

static void midout_channelpressure(int chn, int vel, int32 time)
{
    midout_write_delta_time(time);
    M_FWRITE(2, (chn & 0x0f) | MIDI_CHN_PRESSURE, vel & 0x7f);
}

static void midout_bender(int chn, int pitch, int32 time)
{
    midout_write_delta_time(time);
    M_FWRITE(3, (chn & 0x0f) | MIDI_PITCH_BEND, pitch & 0x7f, (pitch >> 7) & 0x7f);
}

static void midout_program(int chn, int pgm, int32 time)
{
    midout_write_delta_time(time);
    M_FWRITE(2, (chn & 0x0f) | MIDI_PGM_CHANGE, pgm & 0x7f);
}

static void midout_tempo(int chn, int a, int b, int32 time)
{
    midout_write_delta_time(time);
    tempo = (a << 16) | (b << 8) | chn;
    set_tempo();
}

static int find_bit(int val)
{
    int i = 0;
    while (val) {
	if (val & 1)
	    return i;
	i++;
	val >>= 1;
    }
    return -1;
}

static void midout_timesig(int chn, int a, int b, int32 time)
{
    if (chn == 0) {
	if (!b)
	    return;
	b = find_bit(b);
	midout_write_delta_time(time);
	M_FWRITE_STR("\xff\x58\4");
    }
    M_FWRITE(2, a, b);
}

static int do_event(MidiEvent * ev)
{
    int ch, co, va, type;

    ch = ev->channel;

    if ((type = unconvert_midi_control_change(ev)) != -1) {
	midout_control(ch, type, ev->a, ev->time);
	return RC_NONE;
    }

    switch (ev->type) {
    case ME_NOTEON:
	midout_noteon(ch, ev->a, ev->b, ev->time);
	break;
    case ME_NOTEOFF:
	midout_noteoff(ch, ev->a, ev->b, ev->time);
	break;
    case ME_KEYPRESSURE:
	midout_keypressure(ch, ev->a, ev->b, ev->time);
	break;
    case ME_PROGRAM:
	midout_program(ch, ev->a, ev->time);
	break;
    case ME_CHANNEL_PRESSURE:
	midout_channelpressure(ch, ev->a, ev->time);
	break;
    case ME_PITCHWHEEL:
	midout_bender(ch, ev->a, ev->time);
	break;
    case ME_TEMPO:
#if !IGNORE_TEMPO_EVENTS
	midout_tempo(ch, ev->a, ev->b, ev->time);
#endif
	break;
    case ME_TIMESIG:
	midout_timesig(ch, ev->a, ev->b, ev->time);
	break;
    case ME_EOT:
	return RC_TUNE_END;
    }
    return RC_NONE;
}

static int acntl(int request, void *arg)
{
    switch (request) {
    case PM_REQ_MIDI:
	return do_event((MidiEvent *) arg);
    case PM_REQ_PLAY_START:
	start_midi_track();
	return 0;
    case PM_REQ_PLAY_END:
	end_midi_track();
	return 0;
    case PM_REQ_DIVISIONS:
	ticks_per_quarter_note = *(int32 *) arg;
	return 0;
    }
    return -1;
}