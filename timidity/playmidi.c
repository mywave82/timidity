/*
    TiMidity++ -- MIDI to WAVE converter and player
    Copyright (C) 1999-2009 Masanao Izumo <iz@onicos.co.jp>
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
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

    playmidi.c -- random stuff in need of rearrangement
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */
#ifdef __POCC__
#include <sys/types.h>
#endif // for off_t
#ifdef __W32__
#include "interface.h"
#endif
#include <stdio.h>
#include <stdlib.h>

#ifndef NO_STRING_H
#include <string.h>
#else
#include <strings.h>
#endif
#include <math.h>
#ifdef __W32__
#include <windows.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include "timidity.h"
#include "common.h"
#include "instrum.h"
#include "playmidi.h"
#include "readmidi.h"
#include "output.h"
#include "mix.h"
#include "controls.h"
#include "miditrace.h"
#include "recache.h"
#include "arc.h"
#include "reverb.h"
#include "wrd.h"
#include "aq.h"
#include "freq.h"
#include "quantity.h"
#include "timidity.h"

#define ABORT_AT_FATAL 1 /*#################*/
#define MYCHECK(s) do { if(s == 0) { printf("## L %d\n", __LINE__); abort(); } } while(0)

/* #define SUPPRESS_CHANNEL_LAYER */

#ifdef SOLARIS
/* shut gcc warning up */
int usleep(unsigned int useconds);
#endif

#ifdef SUPPORT_SOUNDSPEC
#include "soundspec.h"
#endif /* SUPPORT_SOUNDSPEC */

#include "tables.h"

#define PLAY_INTERLEAVE_SEC		1.0
#define PORTAMENTO_TIME_TUNING		(1.0 / 5000.0)
#define PORTAMENTO_CONTROL_RATIO	256	/* controls per sec */
#define DEFAULT_CHORUS_DELAY1		0.02
#define DEFAULT_CHORUS_DELAY2		0.003
#define CHORUS_OPPOSITE_THRESHOLD	32
#define EOT_PRESEARCH_LEN		32
#define SPEED_CHANGE_RATE		1.0594630943592953  /* 2^(1/12) */

static void set_reverb_level(struct timiditycontext_t *c, int ch, int level);

#define VIBRATO_DEPTH_MAX 384	/* 600 cent */

static int find_samples(struct timiditycontext_t *c, MidiEvent *, int *);
static int select_play_sample(struct timiditycontext_t *c, Sample *, int, int *, int *, MidiEvent *, int);
static double get_play_note_ratio(struct timiditycontext_t *c, int, int);
static int find_voice(struct timiditycontext_t *c, MidiEvent *);
static void finish_note(struct timiditycontext_t *c, int i);
static void update_portamento_controls(struct timiditycontext_t *c, int ch);
static void update_rpn_map(struct timiditycontext_t *c, int ch, int addr, int update_now);
static void ctl_prog_event(struct timiditycontext_t *c, int ch);
static void ctl_timestamp(struct timiditycontext_t *c);
static void ctl_updatetime(struct timiditycontext_t *c, int32 samples);
static void ctl_pause_event(struct timiditycontext_t *c, int pause, int32 samples);
static void update_legato_controls(struct timiditycontext_t *c, int ch);
static void set_master_tuning(struct timiditycontext_t *c, int);
static void set_single_note_tuning(struct timiditycontext_t *c, int, int, int, int);
static void set_user_temper_entry(struct timiditycontext_t *c, int, int, int);
static void set_cuepoint(struct timiditycontext_t *c, int, int, int);
static void recompute_bank_parameter(struct timiditycontext_t *c, int, int);

static void init_voice_filter(struct timiditycontext_t *c, int);
/* XG Part EQ */
static void init_part_eq_xg(struct part_eq_xg *);
static void recompute_part_eq_xg(struct part_eq_xg *);
/* MIDI controllers (MW, Bend, CAf, PAf,...) */
static void init_midi_controller(midi_controller *);
static float get_midi_controller_amp(midi_controller *);
static float get_midi_controller_filter_cutoff(midi_controller *);
static float get_midi_controller_filter_depth(midi_controller *);
static int32 get_midi_controller_pitch(midi_controller *);
static int16 get_midi_controller_pitch_depth(midi_controller *);
static int16 get_midi_controller_amp_depth(midi_controller *);
/* Rx. ~ (Rcv ~) */
static void init_rx(struct timiditycontext_t *c, int);
static void set_rx(struct timiditycontext_t *c, int, int32, int);
static void init_rx_drum(struct DrumParts *);
static void set_rx_drum(struct DrumParts *, int32, int);
static int32 get_rx_drum(struct DrumParts *, int32);

static void play_midi_setup_drums(struct timiditycontext_t *c, int ch, int note);

#define IS_SYSEX_EVENT_TYPE(event) ((event)->type == ME_NONE || (event)->type >= ME_RANDOM_PAN || (event)->b == SYSEX_TAG)

static char *event_name(int type)
{
#define EVENT_NAME(X) case X: return #X
	switch (type) {
	EVENT_NAME(ME_NONE);
	EVENT_NAME(ME_NOTEOFF);
	EVENT_NAME(ME_NOTEON);
	EVENT_NAME(ME_KEYPRESSURE);
	EVENT_NAME(ME_PROGRAM);
	EVENT_NAME(ME_CHANNEL_PRESSURE);
	EVENT_NAME(ME_PITCHWHEEL);
	EVENT_NAME(ME_TONE_BANK_MSB);
	EVENT_NAME(ME_TONE_BANK_LSB);
	EVENT_NAME(ME_MODULATION_WHEEL);
	EVENT_NAME(ME_BREATH);
	EVENT_NAME(ME_FOOT);
	EVENT_NAME(ME_MAINVOLUME);
	EVENT_NAME(ME_BALANCE);
	EVENT_NAME(ME_PAN);
	EVENT_NAME(ME_EXPRESSION);
	EVENT_NAME(ME_SUSTAIN);
	EVENT_NAME(ME_PORTAMENTO_TIME_MSB);
	EVENT_NAME(ME_PORTAMENTO_TIME_LSB);
	EVENT_NAME(ME_PORTAMENTO);
	EVENT_NAME(ME_PORTAMENTO_CONTROL);
	EVENT_NAME(ME_DATA_ENTRY_MSB);
	EVENT_NAME(ME_DATA_ENTRY_LSB);
	EVENT_NAME(ME_SOSTENUTO);
	EVENT_NAME(ME_SOFT_PEDAL);
	EVENT_NAME(ME_LEGATO_FOOTSWITCH);
	EVENT_NAME(ME_HOLD2);
	EVENT_NAME(ME_HARMONIC_CONTENT);
	EVENT_NAME(ME_RELEASE_TIME);
	EVENT_NAME(ME_ATTACK_TIME);
	EVENT_NAME(ME_BRIGHTNESS);
	EVENT_NAME(ME_REVERB_EFFECT);
	EVENT_NAME(ME_TREMOLO_EFFECT);
	EVENT_NAME(ME_CHORUS_EFFECT);
	EVENT_NAME(ME_CELESTE_EFFECT);
	EVENT_NAME(ME_PHASER_EFFECT);
	EVENT_NAME(ME_RPN_INC);
	EVENT_NAME(ME_RPN_DEC);
	EVENT_NAME(ME_NRPN_LSB);
	EVENT_NAME(ME_NRPN_MSB);
	EVENT_NAME(ME_RPN_LSB);
	EVENT_NAME(ME_RPN_MSB);
	EVENT_NAME(ME_ALL_SOUNDS_OFF);
	EVENT_NAME(ME_RESET_CONTROLLERS);
	EVENT_NAME(ME_ALL_NOTES_OFF);
	EVENT_NAME(ME_MONO);
	EVENT_NAME(ME_POLY);
#if 0
	EVENT_NAME(ME_VOLUME_ONOFF);		/* Not supported */
#endif
	EVENT_NAME(ME_MASTER_TUNING);
	EVENT_NAME(ME_SCALE_TUNING);
	EVENT_NAME(ME_BULK_TUNING_DUMP);
	EVENT_NAME(ME_SINGLE_NOTE_TUNING);
	EVENT_NAME(ME_RANDOM_PAN);
	EVENT_NAME(ME_SET_PATCH);
	EVENT_NAME(ME_DRUMPART);
	EVENT_NAME(ME_KEYSHIFT);
	EVENT_NAME(ME_PATCH_OFFS);
	EVENT_NAME(ME_TEMPO);
	EVENT_NAME(ME_CHORUS_TEXT);
	EVENT_NAME(ME_LYRIC);
	EVENT_NAME(ME_GSLCD);
	EVENT_NAME(ME_MARKER);
	EVENT_NAME(ME_INSERT_TEXT);
	EVENT_NAME(ME_TEXT);
	EVENT_NAME(ME_KARAOKE_LYRIC);
	EVENT_NAME(ME_MASTER_VOLUME);
	EVENT_NAME(ME_RESET);
	EVENT_NAME(ME_NOTE_STEP);
	EVENT_NAME(ME_CUEPOINT);
	EVENT_NAME(ME_TIMESIG);
	EVENT_NAME(ME_KEYSIG);
	EVENT_NAME(ME_TEMPER_KEYSIG);
	EVENT_NAME(ME_TEMPER_TYPE);
	EVENT_NAME(ME_MASTER_TEMPER_TYPE);
	EVENT_NAME(ME_USER_TEMPER_ENTRY);
	EVENT_NAME(ME_SYSEX_LSB);
	EVENT_NAME(ME_SYSEX_MSB);
	EVENT_NAME(ME_SYSEX_GS_LSB);
	EVENT_NAME(ME_SYSEX_GS_MSB);
	EVENT_NAME(ME_SYSEX_XG_LSB);
	EVENT_NAME(ME_SYSEX_XG_MSB);
	EVENT_NAME(ME_WRD);
	EVENT_NAME(ME_SHERRY);
	EVENT_NAME(ME_BARMARKER);
	EVENT_NAME(ME_STEP);
	EVENT_NAME(ME_LAST);
	EVENT_NAME(ME_EOT);
	}
	return "Unknown";
#undef EVENT_NAME
}

/*! convert Hz to internal vibrato control ratio. */
static FLOAT_T cnv_Hz_to_vib_ratio(FLOAT_T freq)
{
	return ((FLOAT_T)(play_mode->rate) / (freq * 2.0f * VIBRATO_SAMPLE_INCREMENTS));
}

static void adjust_amplification(struct timiditycontext_t *c)
{
    /* compensate master volume */
    c->master_volume = (double)(c->amplification) / 100.0 *
	((double)c->master_volume_ratio * (c->compensation_ratio/0xFFFF));
}

static int new_vidq(struct timiditycontext_t *c, int ch, int note)
{
    int i;

    if(c->opt_overlap_voice_allow)
    {
	i = ch * 128 + note;
	return c->vidq_head[i]++;
    }
    return 0;
}

static int last_vidq(struct timiditycontext_t *c, int ch, int note)
{
    int i;

    if(c->opt_overlap_voice_allow)
    {
	i = ch * 128 + note;
	if(c->vidq_head[i] == c->vidq_tail[i])
	{
	    ctl->cmsg(CMSG_WARNING, VERB_DEBUG_SILLY,
		      "channel=%d, note=%d: Voice is already OFF", ch, note);
	    return -1;
	}
	return c->vidq_tail[i]++;
    }
    return 0;
}

static void reset_voices(struct timiditycontext_t *c)
{
    int i;
    for(i = 0; i < c->max_voices; i++)
    {
	c->voice[i].status = VOICE_FREE;
	c->voice[i].temper_instant = 0;
	c->voice[i].chorus_link = i;
    }
    c->upper_voices = 0;
    memset(c->vidq_head, 0, sizeof(c->vidq_head));
    memset(c->vidq_tail, 0, sizeof(c->vidq_tail));
}

static void kill_note(struct timiditycontext_t *c, int i)
{
    c->voice[i].status = VOICE_DIE;
    if(!c->prescanning_flag)
	ctl_note_event(c, i);
}

void kill_all_voices(struct timiditycontext_t *c)
{
    int i, uv = c->upper_voices;

    for(i = 0; i < uv; i++)
	if(c->voice[i].status & ~(VOICE_FREE | VOICE_DIE))
	    kill_note(c, i);
    memset(c->vidq_head, 0, sizeof(c->vidq_head));
    memset(c->vidq_tail, 0, sizeof(c->vidq_tail));
}

static void reset_drum_controllers(struct DrumParts *d[], int note)
{
    int i,j;

    if(note == -1)
    {
	for(i = 0; i < 128; i++)
	    if(d[i] != NULL)
	    {
		d[i]->drum_panning = NO_PANNING;
		for(j=0;j<6;j++) {d[i]->drum_envelope_rate[j] = -1;}
		d[i]->pan_random = 0;
		d[i]->drum_level = 1.0f;
		d[i]->coarse = 0;
		d[i]->fine = 0;
		d[i]->delay_level = -1;
		d[i]->chorus_level = -1;
		d[i]->reverb_level = -1;
		d[i]->play_note = -1;
		d[i]->drum_cutoff_freq = 0;
		d[i]->drum_resonance = 0;
		init_rx_drum(d[i]);
	    }
    }
    else
    {
	d[note]->drum_panning = NO_PANNING;
	for(j = 0; j < 6; j++) {d[note]->drum_envelope_rate[j] = -1;}
	d[note]->pan_random = 0;
	d[note]->drum_level = 1.0f;
	d[note]->coarse = 0;
	d[note]->fine = 0;
	d[note]->delay_level = -1;
	d[note]->chorus_level = -1;
	d[note]->reverb_level = -1;
	d[note]->play_note = -1;
	d[note]->drum_cutoff_freq = 0;
	d[note]->drum_resonance = 0;
	init_rx_drum(d[note]);
    }
}

static void reset_module_dependent_controllers(struct timiditycontext_t *c, int ch)
{
	int module = get_module(c);
	switch(module) {	/* TONE MAP-0 NUMBER */
	case MODULE_SC55:
		c->channel[ch].tone_map0_number = 1;
		break;
	case MODULE_SC88:
		c->channel[ch].tone_map0_number = 2;
		break;
	case MODULE_SC88PRO:
		c->channel[ch].tone_map0_number = 3;
		break;
	case MODULE_SC8850:
		c->channel[ch].tone_map0_number = 4;
		break;
	default:
		c->channel[ch].tone_map0_number = 0;
		break;
	}
	switch(module) {	/* MIDI Controllers */
	case MODULE_SC55:
		c->channel[ch].mod.lfo1_pitch_depth = 10;
		break;
	case MODULE_SC88:
		c->channel[ch].mod.lfo1_pitch_depth = 10;
		break;
	case MODULE_SC88PRO:
		c->channel[ch].mod.lfo1_pitch_depth = 10;
		break;
	default:
		c->channel[ch].mod.lfo1_pitch_depth = 50;
		break;
	}
}

static void reset_nrpn_controllers(struct timiditycontext_t *c, int ch)
{
  int i;

  /* NRPN */
  reset_drum_controllers(c->channel[ch].drums, -1);
  c->channel[ch].vibrato_ratio = 1.0;
  c->channel[ch].vibrato_depth = 0;
  c->channel[ch].vibrato_delay = 0;
  c->channel[ch].param_cutoff_freq = 0;
  c->channel[ch].param_resonance = 0;
  c->channel[ch].cutoff_freq_coef = 1.0;
  c->channel[ch].resonance_dB = 0;

  /* System Exclusive */
  c->channel[ch].dry_level = 127;
  c->channel[ch].eq_gs = 1;
  c->channel[ch].insertion_effect = 0;
  c->channel[ch].velocity_sense_depth = 0x40;
  c->channel[ch].velocity_sense_offset = 0x40;
  c->channel[ch].pitch_offset_fine = 0;
  if(c->play_system_mode == GS_SYSTEM_MODE) {c->channel[ch].assign_mode = 1;}
  else {
	  if(ISDRUMCHANNEL(ch)) {c->channel[ch].assign_mode = 1;}
	  else {c->channel[ch].assign_mode = 2;}
  }
  for (i = 0; i < 12; i++)
	  c->channel[ch].scale_tuning[i] = 0;
  c->channel[ch].prev_scale_tuning = 0;
  c->channel[ch].temper_type = 0;

  init_channel_layer(c, ch);
  init_part_eq_xg(&(c->channel[ch].eq_xg));

  /* c->channel pressure & polyphonic key pressure control */
  init_midi_controller(&(c->channel[ch].mod));
  init_midi_controller(&(c->channel[ch].bend));
  init_midi_controller(&(c->channel[ch].caf));
  init_midi_controller(&(c->channel[ch].paf));
  init_midi_controller(&(c->channel[ch].cc1));
  init_midi_controller(&(c->channel[ch].cc2));
  c->channel[ch].bend.pitch = 2;

  init_rx(c, ch);
  c->channel[ch].note_limit_high = 127;
  c->channel[ch].note_limit_low = 0;
  c->channel[ch].vel_limit_high = 127;
  c->channel[ch].vel_limit_low = 0;

  free_drum_effect(c, ch);

  c->channel[ch].legato = 0;
  c->channel[ch].damper_mode = 0;
  c->channel[ch].loop_timeout = 0;

  c->channel[ch].sysex_gs_msb_addr = c->channel[ch].sysex_gs_msb_val =
	c->channel[ch].sysex_xg_msb_addr = c->channel[ch].sysex_xg_msb_val =
	c->channel[ch].sysex_msb_addr = c->channel[ch].sysex_msb_val = 0;
}

/* Process the Reset All Controllers event */
static void reset_controllers(struct timiditycontext_t *c, int ch)
{
  int j;
    /* Some standard says, although the SCC docs say 0. */

  if(c->play_system_mode == XG_SYSTEM_MODE)
      c->channel[ch].volume = 100;
  else
      c->channel[ch].volume = 90;
  if (c->prescanning_flag) {
    if (c->channel[ch].volume > c->mainvolume_max) {	/* pick maximum value of mainvolume */
      c->mainvolume_max = c->channel[ch].volume;
      ctl->cmsg(CMSG_INFO,VERB_DEBUG,"ME_MAINVOLUME/max (CH:%d VAL:%#x)",ch,c->mainvolume_max);
    }
  }

  c->channel[ch].expression = 127; /* SCC-1 does this. */
  c->channel[ch].sustain = 0;
  c->channel[ch].sostenuto = 0;
  c->channel[ch].pitchbend = 0x2000;
  c->channel[ch].pitchfactor = 0; /* to be computed */
  c->channel[ch].mod.val = 0;
  c->channel[ch].bend.val = 0;
  c->channel[ch].caf.val = 0;
  c->channel[ch].paf.val = 0;
  c->channel[ch].cc1.val = 0;
  c->channel[ch].cc2.val = 0;
  c->channel[ch].portamento_time_lsb = 0;
  c->channel[ch].portamento_time_msb = 0;
  c->channel[ch].porta_control_ratio = 0;
  c->channel[ch].portamento = 0;
  c->channel[ch].last_note_fine = -1;
  for(j = 0; j < 6; j++) {c->channel[ch].envelope_rate[j] = -1;}
  update_portamento_controls(c, ch);
  set_reverb_level(c, ch, -1);
  if(c->opt_chorus_control == 1)
      c->channel[ch].chorus_level = 0;
  else
      c->channel[ch].chorus_level = -c->opt_chorus_control;
  c->channel[ch].mono = 0;
  c->channel[ch].delay_level = 0;
}

static void redraw_controllers(struct timiditycontext_t *c, int ch)
{
    ctl_mode_event(c, CTLE_VOLUME, 1, ch, c->channel[ch].volume);
    ctl_mode_event(c, CTLE_EXPRESSION, 1, ch, c->channel[ch].expression);
    ctl_mode_event(c, CTLE_SUSTAIN, 1, ch, c->channel[ch].sustain);
    ctl_mode_event(c, CTLE_MOD_WHEEL, 1, ch, c->channel[ch].mod.val);
    ctl_mode_event(c, CTLE_PITCH_BEND, 1, ch, c->channel[ch].pitchbend);
    ctl_prog_event(c, ch);
    ctl_mode_event(c, CTLE_TEMPER_TYPE, 1, ch, c->channel[ch].temper_type);
    ctl_mode_event(c, CTLE_MUTE, 1,
		ch, (IS_SET_CHANNELMASK(c->channel_mute, ch)) ? 1 : 0);
    ctl_mode_event(c, CTLE_CHORUS_EFFECT, 1, ch, get_chorus_level(c, ch));
    ctl_mode_event(c, CTLE_REVERB_EFFECT, 1, ch, get_reverb_level(c, ch));
}

void reset_midi(struct timiditycontext_t *c, int playing)
{
	int i, cnt;

	for (i = 0; i < MAX_CHANNELS; i++) {
		reset_controllers(c, i);
		reset_nrpn_controllers(c, i);
	reset_module_dependent_controllers(c, i);
		/* The rest of these are unaffected
		 * by the Reset All Controllers event
		 */
		c->channel[i].program = c->default_program[i];
		c->channel[i].panning = NO_PANNING;
		c->channel[i].pan_random = 0;
		/* tone bank or drum set */
		if (ISDRUMCHANNEL(i)) {
			c->channel[i].bank = 0;
			c->channel[i].altassign = c->drumset[0]->alt;
		} else {
			if (c->special_tonebank >= 0)
				c->channel[i].bank = c->special_tonebank;
			else
				c->channel[i].bank = c->default_tonebank;
		}
		c->channel[i].bank_lsb = c->channel[i].bank_msb = 0;
		if (c->play_system_mode == XG_SYSTEM_MODE && i % 16 == 9)
			c->channel[i].bank_msb = 127;	/* Use MSB=127 for XG */
		update_rpn_map(c, i, RPN_ADDR_FFFF, 0);
		c->channel[i].special_sample = 0;
		c->channel[i].key_shift = 0;
		c->channel[i].mapID = get_default_mapID(c, i);
		c->channel[i].lasttime = 0;
	}
	if (playing) {
		kill_all_voices(c);
		if (c->temper_type_mute) {
			if (c->temper_type_mute & 1)
				FILL_CHANNELMASK(c->channel_mute);
			else
				CLEAR_CHANNELMASK(c->channel_mute);
		}
		for (i = 0; i < MAX_CHANNELS; i++)
			redraw_controllers(c, i);
		if (c->midi_streaming && c->free_instruments_afterwards) {
			free_instruments(c, 0);
			/* free unused memory */
			cnt = free_global_mblock(c);
			if (cnt > 0)
				ctl->cmsg(CMSG_INFO, VERB_VERBOSE,
						"%d memory blocks are free", cnt);
		}
	} else
		reset_voices(c);
	c->master_volume_ratio = 0xffff;
	adjust_amplification(c);
	init_freq_table_tuning(c);
	c->master_tuning = 0;
	if (c->current_file_info) {
		COPY_CHANNELMASK(c->drumchannels, c->current_file_info->drumchannels);
		COPY_CHANNELMASK(c->drumchannel_mask, c->current_file_info->drumchannel_mask);
	} else {
		COPY_CHANNELMASK(c->drumchannels, c->default_drumchannels);
		COPY_CHANNELMASK(c->drumchannel_mask, c->default_drumchannel_mask);
	}
	ctl_mode_event(c, CTLE_MASTER_VOLUME, 0, c->amplification, 0);
	ctl_mode_event(c, CTLE_KEY_OFFSET, 0, c->note_key_offset, 0);
	ctl_mode_event(c, CTLE_TIME_RATIO, 0, 100 / c->midi_time_ratio + 0.5, 0);
}

void recompute_freq(struct timiditycontext_t *c, int v)
{
	int i;
	int ch = c->voice[v].channel;
	int note = c->voice[v].note;
	int32 tuning = 0;
	int8 st = c->channel[ch].scale_tuning[note % 12];
	int8 tt = c->channel[ch].temper_type;
	uint8 tp = c->channel[ch].rpnmap[RPN_ADDR_0003];
	int32 f;
	int pb = c->channel[ch].pitchbend;
	int32 tmp;
	FLOAT_T pf, root_freq;
	int32 a;
	Voice *vp = &(c->voice[v]);

	if (! c->voice[v].sample->sample_rate)
		return;
	if (! c->opt_modulation_wheel)
		c->channel[ch].mod.val = 0;
	if (! c->opt_portamento)
		c->voice[v].porta_control_ratio = 0;
	c->voice[v].vibrato_control_ratio = c->voice[v].orig_vibrato_control_ratio;
	if (c->voice[v].vibrato_control_ratio || c->channel[ch].mod.val > 0) {
		/* This instrument has vibrato. Invalidate any precomputed
		 * sample_increments.
		 */

		/* MIDI controllers LFO pitch depth */
		if (c->opt_channel_pressure || c->opt_modulation_wheel) {
			vp->vibrato_depth = vp->sample->vibrato_depth + c->channel[ch].vibrato_depth;
			vp->vibrato_depth += get_midi_controller_pitch_depth(&(c->channel[ch].mod))
				+ get_midi_controller_pitch_depth(&(c->channel[ch].bend))
				+ get_midi_controller_pitch_depth(&(c->channel[ch].caf))
				+ get_midi_controller_pitch_depth(&(c->channel[ch].paf))
				+ get_midi_controller_pitch_depth(&(c->channel[ch].cc1))
				+ get_midi_controller_pitch_depth(&(c->channel[ch].cc2));
			if (vp->vibrato_depth > VIBRATO_DEPTH_MAX) {vp->vibrato_depth = VIBRATO_DEPTH_MAX;}
			else if (vp->vibrato_depth < 1) {vp->vibrato_depth = 1;}
			if (vp->sample->vibrato_depth < 0) {	/* in opposite phase */
				vp->vibrato_depth = -vp->vibrato_depth;
			}
		}

		/* fill parameters for modulation wheel */
		if (c->channel[ch].mod.val > 0) {
			if(vp->vibrato_control_ratio == 0) {
				vp->vibrato_control_ratio =
					vp->orig_vibrato_control_ratio = (int)(cnv_Hz_to_vib_ratio(5.0) * c->channel[ch].vibrato_ratio);
			}
			vp->vibrato_delay = 0;
		}

		for (i = 0; i < VIBRATO_SAMPLE_INCREMENTS; i++)
			vp->vibrato_sample_increment[i] = 0;
		vp->cache = NULL;
	}
	/* At least for GM2, it's recommended not to apply master_tuning for drum channels */
	tuning = ISDRUMCHANNEL(ch) ? 0 : c->master_tuning;
	/* fine: [0..128] => [-256..256]
	 * 1 coarse = 256 fine (= 1 note)
	 * 1 fine = 2^5 tuning
	 */
	tuning += (c->channel[ch].rpnmap[RPN_ADDR_0001] - 0x40
			+ (c->channel[ch].rpnmap[RPN_ADDR_0002] - 0x40) * 64) << 7;
	/* for NRPN Coarse Pitch of Drum (GS) & Fine Pitch of Drum (XG) */
	if (ISDRUMCHANNEL(ch) && c->channel[ch].drums[note] != NULL
			&& (c->channel[ch].drums[note]->fine
			|| c->channel[ch].drums[note]->coarse)) {
		tuning += (c->channel[ch].drums[note]->fine
				+ c->channel[ch].drums[note]->coarse * 64) << 7;
	}
	/* MIDI controllers pitch control */
	if (c->opt_channel_pressure) {
		tuning += get_midi_controller_pitch(&(c->channel[ch].mod))
			+ get_midi_controller_pitch(&(c->channel[ch].bend))
			+ get_midi_controller_pitch(&(c->channel[ch].caf))
			+ get_midi_controller_pitch(&(c->channel[ch].paf))
			+ get_midi_controller_pitch(&(c->channel[ch].cc1))
			+ get_midi_controller_pitch(&(c->channel[ch].cc2));
	}
	if (c->opt_modulation_envelope) {
		if (c->voice[v].sample->tremolo_to_pitch) {
			tuning += lookup_triangular(c, c->voice[v].tremolo_phase >> RATE_SHIFT)
					* (c->voice[v].sample->tremolo_to_pitch << 13) / 100.0 + 0.5;
			c->channel[ch].pitchfactor = 0;
		}
		if (c->voice[v].sample->modenv_to_pitch) {
			tuning += c->voice[v].last_modenv_volume
					* (c->voice[v].sample->modenv_to_pitch << 13) / 100.0 + 0.5;
			c->channel[ch].pitchfactor = 0;
		}
	}
	/* GS/XG - Scale Tuning */
	if (! ISDRUMCHANNEL(ch)) {
		tuning += ((st << 13) + 50) / 100;
		if (st != c->channel[ch].prev_scale_tuning) {
			c->channel[ch].pitchfactor = 0;
			c->channel[ch].prev_scale_tuning = st;
		}
	}
	if (! c->opt_pure_intonation
			&& c->opt_temper_control && c->voice[v].temper_instant) {
		switch (tt) {
		case 0:
			f = c->freq_table_tuning[tp][note];
			break;
		case 1:
			if (c->current_temper_keysig < 8)
				f = c->freq_table_pytha[c->current_temper_freq_table][note];
			else
				f = c->freq_table_pytha[c->current_temper_freq_table + 12][note];
			break;
		case 2:
			if (c->current_temper_keysig < 8)
				f = c->freq_table_meantone[c->current_temper_freq_table
						+ ((c->temper_adj) ? 36 : 0)][note];
			else
				f = c->freq_table_meantone[c->current_temper_freq_table
						+ ((c->temper_adj) ? 24 : 12)][note];
			break;
		case 3:
			if (c->current_temper_keysig < 8)
				f = c->freq_table_pureint[c->current_temper_freq_table
						+ ((c->temper_adj) ? 36 : 0)][note];
			else
				f = c->freq_table_pureint[c->current_temper_freq_table
						+ ((c->temper_adj) ? 24 : 12)][note];
			break;
		default:	/* user-defined temperament */
			if ((tt -= 0x40) >= 0 && tt < 4) {
				if (c->current_temper_keysig < 8)
					f = c->freq_table_user[tt][c->current_temper_freq_table
							+ ((c->temper_adj) ? 36 : 0)][note];
				else
					f = c->freq_table_user[tt][c->current_temper_freq_table
							+ ((c->temper_adj) ? 24 : 12)][note];
			} else
				f = c->freq_table[note];
			break;
		}
		c->voice[v].orig_frequency = f;
	}
	if (! c->voice[v].porta_control_ratio) {
		if (tuning == 0 && pb == 0x2000)
			c->voice[v].frequency = c->voice[v].orig_frequency;
		else {
			pb -= 0x2000;
			if (! c->channel[ch].pitchfactor) {
				/* Damn.  Somebody bent the pitch. */
				tmp = pb * c->channel[ch].rpnmap[RPN_ADDR_0000] + tuning;
				if (tmp >= 0)
					c->channel[ch].pitchfactor = c->bend_fine[tmp >> 5 & 0xff]
							* c->bend_coarse[tmp >> 13 & 0x7f];
				else
					c->channel[ch].pitchfactor = 1.0 /
							(c->bend_fine[-tmp >> 5 & 0xff]
							* c->bend_coarse[-tmp >> 13 & 0x7f]);
			}
			c->voice[v].frequency =
					c->voice[v].orig_frequency * c->channel[ch].pitchfactor;
			if (c->voice[v].frequency != c->voice[v].orig_frequency)
				c->voice[v].cache = NULL;
		}
	} else {	/* Portamento */
		pb -= 0x2000;
		tmp = pb * c->channel[ch].rpnmap[RPN_ADDR_0000]
				+ (c->voice[v].porta_pb << 5) + tuning;
		if (tmp >= 0)
			pf = c->bend_fine[tmp >> 5 & 0xff]
					* c->bend_coarse[tmp >> 13 & 0x7f];
		else
			pf = 1.0 / (c->bend_fine[-tmp >> 5 & 0xff]
					* c->bend_coarse[-tmp >> 13 & 0x7f]);
		c->voice[v].frequency = c->voice[v].orig_frequency * pf;
		c->voice[v].cache = NULL;
	}
	root_freq = c->voice[v].sample->root_freq;
	a = TIM_FSCALE(((double) c->voice[v].sample->sample_rate
			* ((double)c->voice[v].frequency + c->channel[ch].pitch_offset_fine))
			/ (root_freq * play_mode->rate), FRACTION_BITS) + 0.5;
	/* need to preserve the loop direction */
	c->voice[v].sample_increment = (c->voice[v].sample_increment >= 0) ? a : -a;
#ifdef ABORT_AT_FATAL
	if (c->voice[v].sample_increment == 0) {
		fprintf(stderr, "Invalid sample increment a=%e %ld %ld %ld %ld%s\n",
				(double)a, (long) c->voice[v].sample->sample_rate,
				(long) c->voice[v].frequency, (long) c->voice[v].sample->root_freq,
				(long) play_mode->rate, (c->voice[v].cache) ? " (Cached)" : "");
		abort();
	}
#endif	/* ABORT_AT_FATAL */
}

static int32 calc_velocity(struct timiditycontext_t *c, int32 ch,int32 vel)
{
	int32 velocity;
	velocity = c->channel[ch].velocity_sense_depth * vel / 64 + (c->channel[ch].velocity_sense_offset - 64) * 2;
	if(velocity > 127) {velocity = 127;}
	return velocity;
}

static void recompute_voice_tremolo(struct timiditycontext_t *c, int v)
{
	Voice *vp = &(c->voice[v]);
	int ch = vp->channel;
	int32 depth = vp->sample->tremolo_depth;
	depth += get_midi_controller_amp_depth(&(c->channel[ch].mod))
		+ get_midi_controller_amp_depth(&(c->channel[ch].bend))
		+ get_midi_controller_amp_depth(&(c->channel[ch].caf))
		+ get_midi_controller_amp_depth(&(c->channel[ch].paf))
		+ get_midi_controller_amp_depth(&(c->channel[ch].cc1))
		+ get_midi_controller_amp_depth(&(c->channel[ch].cc2));
	if(depth > 256) {depth = 256;}
	vp->tremolo_depth = depth;
}

static void recompute_amp(struct timiditycontext_t *c, int v)
{
	FLOAT_T tempamp;
	int ch = c->voice[v].channel;

	/* master_volume and sample->volume are percentages, used to scale
	 *  amplitude directly, NOT perceived volume
	 *
	 * all other MIDI volumes are linear in perceived volume, 0-127
	 * use a lookup table for the non-linear scalings
	 */
	if (c->opt_user_volume_curve) {
	tempamp = c->master_volume *
		   c->voice[v].sample->volume *
		   c->user_vol_table[calc_velocity(c, ch, c->voice[v].velocity)] *
		   c->user_vol_table[c->channel[ch].volume] *
		   c->user_vol_table[c->channel[ch].expression]; /* 21 bits */
	} else if (c->play_system_mode == GM2_SYSTEM_MODE) {
	tempamp = c->master_volume *
		  c->voice[v].sample->volume *
		  c->gm2_vol_table[calc_velocity(c, ch, c->voice[v].velocity)] *	/* velocity: not in GM2 standard */
		  c->gm2_vol_table[c->channel[ch].volume] *
		  c->gm2_vol_table[c->channel[ch].expression]; /* 21 bits */
	} else if(c->play_system_mode == GS_SYSTEM_MODE) {	/* use measured curve */
	tempamp = c->master_volume *
		   c->voice[v].sample->volume *
		   sc_vel_table[calc_velocity(c, ch, c->voice[v].velocity)] *
		   sc_vol_table[c->channel[ch].volume] *
		   sc_vol_table[c->channel[ch].expression]; /* 21 bits */
	} else if (IS_CURRENT_MOD_FILE) {	/* use linear curve */
	tempamp = c->master_volume *
		  c->voice[v].sample->volume *
		  calc_velocity(c, ch, c->voice[v].velocity) *
		  c->channel[ch].volume *
		  c->channel[ch].expression; /* 21 bits */
	} else {	/* use generic exponential curve */
	tempamp = c->master_volume *
		  c->voice[v].sample->volume *
		  c->perceived_vol_table[calc_velocity(c, ch, c->voice[v].velocity)] *
		  c->perceived_vol_table[c->channel[ch].volume] *
		  c->perceived_vol_table[c->channel[ch].expression]; /* 21 bits */
	}

	/* every digital effect increases amplitude,
	 * so that it must be reduced in advance.
	 */
	if (! (play_mode->encoding & PE_MONO)
	    && (c->opt_reverb_control || c->opt_chorus_control || c->opt_delay_control
		|| (c->opt_eq_control && (c->eq_status_gs.low_gain != 0x40
				       || c->eq_status_gs.high_gain != 0x40))
		|| c->opt_insertion_effect))
		tempamp *= 1.35f * 0.55f;
	else
		tempamp *= 1.35f;

	/* Reduce amplitude for chorus partners.
	 * 2x voices -> 2x power -> sqrt(2)x amplitude.
	 * 1 / sqrt(2) = ~0.7071, which is very close to the old
	 * CHORUS_VELOCITY_TUNING1 value of 0.7.
	 *
	 * The previous amp scaling for the various digital effects should
	 * really be redone to split them into separate scalings for each
	 * effect, rather than a single scaling if any one of them is used
	 * (which is NOT correct).  As it is now, if partner chorus is the
	 * only effect in use, then it is reduced in volume twice and winds
	 * up too quiet.  Compare the output of "-EFreverb=0 -EFchorus=0",
	 * "-EFreverb=0 -EFchorus=2", "-EFreverb=4 -EFchorus=2", and
	 * "-EFreverb=4 -EFchorus=0" to see how the digital effect volumes
	 * are not scaled properly.  Idealy, all the resulting output should
	 * have the same volume, regardless of effects used.  This will
	 * require empirically determining the amount to scale for each
	 * individual effect.
	 */
         if (c->voice[v].chorus_link != v)
            tempamp *= 0.7071067811865f;

	/* NRPN - drum instrument tva level */
	if(ISDRUMCHANNEL(ch)) {
		if(c->channel[ch].drums[c->voice[v].note] != NULL) {
			tempamp *= c->channel[ch].drums[c->voice[v].note]->drum_level;
		}
		tempamp *= (double)c->opt_drum_power * 0.01f;	/* global drum power */
	}

	/* MIDI controllers amplitude control */
	if(c->opt_channel_pressure) {
		tempamp *= get_midi_controller_amp(&(c->channel[ch].mod))
			* get_midi_controller_amp(&(c->channel[ch].bend))
			* get_midi_controller_amp(&(c->channel[ch].caf))
			* get_midi_controller_amp(&(c->channel[ch].paf))
			* get_midi_controller_amp(&(c->channel[ch].cc1))
			* get_midi_controller_amp(&(c->channel[ch].cc2));
		recompute_voice_tremolo(c, v);
	}

	if (c->voice[v].fc.type != 0) {
		tempamp *= c->voice[v].fc.gain;	/* filter gain */
	}

	/* applying panning to amplitude */
	if(!(play_mode->encoding & PE_MONO))
	{
		if(c->voice[v].panning == 64)
		{
			c->voice[v].panned = PANNED_CENTER;
			c->voice[v].left_amp = c->voice[v].right_amp = TIM_FSCALENEG(tempamp * c->pan_table[64], 27);
		}
		else if (c->voice[v].panning < 2)
		{
			c->voice[v].panned = PANNED_LEFT;
			c->voice[v].left_amp = TIM_FSCALENEG(tempamp, 20);
			c->voice[v].right_amp = 0;
		}
		else if(c->voice[v].panning == 127)
		{
#ifdef SMOOTH_MIXING
			if(c->voice[v].panned == PANNED_MYSTERY) {
				c->voice[v].old_left_mix = c->voice[v].old_right_mix;
				c->voice[v].old_right_mix = 0;
			}
#endif
			c->voice[v].panned = PANNED_RIGHT;
			c->voice[v].left_amp =  TIM_FSCALENEG(tempamp, 20);
			c->voice[v].right_amp = 0;
		}
		else
		{
#ifdef SMOOTH_MIXING
			if(c->voice[v].panned == PANNED_RIGHT) {
				c->voice[v].old_right_mix = c->voice[v].old_left_mix;
				c->voice[v].old_left_mix = 0;
			}
#endif
			c->voice[v].panned = PANNED_MYSTERY;
			c->voice[v].left_amp = TIM_FSCALENEG(tempamp * c->pan_table[128 - c->voice[v].panning], 27);
			c->voice[v].right_amp = TIM_FSCALENEG(tempamp * c->pan_table[c->voice[v].panning], 27);
		}
	}
	else
	{
		c->voice[v].panned = PANNED_CENTER;
		c->voice[v].left_amp = TIM_FSCALENEG(tempamp, 21);
	}
}

#define RESONANCE_COEFF 0.2393

void recompute_channel_filter(struct timiditycontext_t *c, int ch, int note)
{
	double coef = 1.0f, reso = 0;

	if(c->channel[ch].special_sample > 0) {return;}

	/* Soft Pedal */
	if(c->channel[ch].soft_pedal != 0) {
		if(note > 49) {	/* tre corde */
			coef *= 1.0 - 0.20 * ((double)c->channel[ch].soft_pedal) / 127.0f;
		} else {	/* una corda (due corde) */
			coef *= 1.0 - 0.25 * ((double)c->channel[ch].soft_pedal) / 127.0f;
		}
	}

	if(!ISDRUMCHANNEL(ch)) {
		/* NRPN Filter Cutoff */
		coef *= pow(1.26, (double)(c->channel[ch].param_cutoff_freq) / 8.0f);
		/* NRPN Resonance */
		reso = (double)c->channel[ch].param_resonance * RESONANCE_COEFF;
	}

	c->channel[ch].cutoff_freq_coef = coef;
	c->channel[ch].resonance_dB = reso;
}

static void init_voice_filter(struct timiditycontext_t *c, int i)
{
  memset(&(c->voice[i].fc), 0, sizeof(FilterCoefficients));
  if(c->opt_lpf_def && c->voice[i].sample->cutoff_freq) {
	  c->voice[i].fc.orig_freq = c->voice[i].sample->cutoff_freq;
	  c->voice[i].fc.orig_reso_dB = (double)c->voice[i].sample->resonance / 10.0f - 3.01f;
	  if (c->voice[i].fc.orig_reso_dB < 0.0f) {c->voice[i].fc.orig_reso_dB = 0.0f;}
	  if (c->opt_lpf_def == 2) {
		  c->voice[i].fc.gain = 1.0;
		  c->voice[i].fc.type = 2;
	  } else if(c->opt_lpf_def == 1) {
		  c->voice[i].fc.gain = pow(10.0f, -c->voice[i].fc.orig_reso_dB / 2.0f / 20.0f);
		  c->voice[i].fc.type = 1;
	  }
	  c->voice[i].fc.start_flag = 0;
  } else {
	  c->voice[i].fc.type = 0;
  }
}

#define CHAMBERLIN_RESONANCE_MAX 24.0

void recompute_voice_filter(struct timiditycontext_t *c, int v)
{
	int ch = c->voice[v].channel, note = c->voice[v].note;
	double coef = 1.0, reso = 0, cent = 0, depth_cent = 0, freq;
	FilterCoefficients *fc = &(c->voice[v].fc);
	Sample *sp = (Sample *) &c->voice[v].sample;

	if(fc->type == 0) {return;}
	coef = c->channel[ch].cutoff_freq_coef;

	if(ISDRUMCHANNEL(ch) && c->channel[ch].drums[note] != NULL) {
		/* NRPN Drum Instrument Filter Cutoff */
		coef *= pow(1.26, (double)(c->channel[ch].drums[note]->drum_cutoff_freq) / 8.0f);
		/* NRPN Drum Instrument Filter Resonance */
		reso += (double)c->channel[ch].drums[note]->drum_resonance * RESONANCE_COEFF;
	}

	/* MIDI controllers filter cutoff control and LFO filter depth */
	if(c->opt_channel_pressure) {
		cent += get_midi_controller_filter_cutoff(&(c->channel[ch].mod))
			+ get_midi_controller_filter_cutoff(&(c->channel[ch].bend))
			+ get_midi_controller_filter_cutoff(&(c->channel[ch].caf))
			+ get_midi_controller_filter_cutoff(&(c->channel[ch].paf))
			+ get_midi_controller_filter_cutoff(&(c->channel[ch].cc1))
			+ get_midi_controller_filter_cutoff(&(c->channel[ch].cc2));
		depth_cent += get_midi_controller_filter_depth(&(c->channel[ch].mod))
			+ get_midi_controller_filter_depth(&(c->channel[ch].bend))
			+ get_midi_controller_filter_depth(&(c->channel[ch].caf))
			+ get_midi_controller_filter_depth(&(c->channel[ch].paf))
			+ get_midi_controller_filter_depth(&(c->channel[ch].cc1))
			+ get_midi_controller_filter_depth(&(c->channel[ch].cc2));
	}

	if(sp->vel_to_fc) {	/* velocity to filter cutoff frequency */
		if(c->voice[v].velocity > sp->vel_to_fc_threshold)
			cent += sp->vel_to_fc * (double)(127 - c->voice[v].velocity) / 127.0f;
		else
			coef += sp->vel_to_fc * (double)(127 - sp->vel_to_fc_threshold) / 127.0f;
	}
	if(sp->vel_to_resonance) {	/* velocity to filter resonance */
		reso += (double)c->voice[v].velocity * sp->vel_to_resonance / 127.0f / 10.0f;
	}
	if(sp->key_to_fc) {	/* filter cutoff key-follow */
		cent += sp->key_to_fc * (double)(c->voice[v].note - sp->key_to_fc_bpo);
	}

	if(c->opt_modulation_envelope) {
		if(c->voice[v].sample->tremolo_to_fc + (int16)depth_cent) {
			cent += ((double)c->voice[v].sample->tremolo_to_fc + depth_cent) * lookup_triangular(c, c->voice[v].tremolo_phase >> RATE_SHIFT);
		}
		if(c->voice[v].sample->modenv_to_fc) {
			cent += (double)c->voice[v].sample->modenv_to_fc * c->voice[v].last_modenv_volume;
		}
	}

	if(cent != 0) {coef *= pow(2.0, cent / 1200.0f);}

	freq = (double)fc->orig_freq * coef;

	if (freq > play_mode->rate / 2) {freq = play_mode->rate / 2;}
	else if(freq < 5) {freq = 5;}
	fc->freq = (int32)freq;

	fc->reso_dB = fc->orig_reso_dB + c->channel[ch].resonance_dB + reso;
	if(fc->reso_dB < 0.0f) {fc->reso_dB = 0.0f;}
	else if(fc->reso_dB > 96.0f) {fc->reso_dB = 96.0f;}

	if(fc->type == 1) {	/* Chamberlin filter */
		if(fc->freq > play_mode->rate / 6) {
			if (fc->start_flag == 0) {fc->type = 0;}	/* turn off. */
			else {fc->freq = play_mode->rate / 6;}
		}
		if(fc->reso_dB > CHAMBERLIN_RESONANCE_MAX) {fc->reso_dB = CHAMBERLIN_RESONANCE_MAX;}
	} else if(fc->type == 2) {	/* Moog VCF */
		if(fc->reso_dB > fc->orig_reso_dB / 2) {
			fc->gain = pow(10.0f, (fc->reso_dB - fc->orig_reso_dB / 2) / 20.0f);
		}
	}
	fc->start_flag = 1;	/* filter is started. */
}

static float calc_drum_tva_level(struct timiditycontext_t *c, int ch, int note, int level)
{
	int def_level, nbank, nprog;
	ToneBank *bank;

	if(c->channel[ch].special_sample > 0) {return 1.0;}

	nbank = c->channel[ch].bank;
	nprog = note;
	instrument_map(c, c->channel[ch].mapID, &nbank, &nprog);

	if(ISDRUMCHANNEL(ch)) {
		bank = c->drumset[nbank];
		if(bank == NULL) {bank = c->drumset[0];}
	} else {
		return 1.0;
	}

	def_level = bank->tone[nprog].tva_level;

	if(def_level == -1 || def_level == 0) {def_level = 127;}
	else if(def_level > 127) {def_level = 127;}

	return (sc_drum_level_table[level] / sc_drum_level_table[def_level]);
}

static int32 calc_random_delay(struct timiditycontext_t *c, int ch, int note)
{
	int nbank, nprog;
	ToneBank *bank;

	if(c->channel[ch].special_sample > 0) {return 0;}

	nbank = c->channel[ch].bank;

	if(ISDRUMCHANNEL(ch)) {
		nprog = note;
		instrument_map(c, c->channel[ch].mapID, &nbank, &nprog);
		bank = c->drumset[nbank];
		if (bank == NULL) {bank = c->drumset[0];}
	} else {
		nprog = c->channel[ch].program;
		if(nprog == SPECIAL_PROGRAM) {return 0;}
		instrument_map(c, c->channel[ch].mapID, &nbank, &nprog);
		bank = c->tonebank[nbank];
		if(bank == NULL) {bank = c->tonebank[0];}
	}

	if (bank->tone[nprog].rnddelay == 0) {return 0;}
	else {return (int32)((double)bank->tone[nprog].rnddelay * play_mode->rate / 1000.0
		* (get_pink_noise_light(c, &c->global_pink_noise_light) + 1.0f) * 0.5);}
}

static void recompute_bank_parameter(struct timiditycontext_t *c, int ch, int note)
{
	int nbank, nprog;
	ToneBank *bank;
	struct DrumParts *drum;

	if(c->channel[ch].special_sample > 0) {return;}

	nbank = c->channel[ch].bank;

	if(ISDRUMCHANNEL(ch)) {
		nprog = note;
		instrument_map(c, c->channel[ch].mapID, &nbank, &nprog);
		bank = c->drumset[nbank];
		if (bank == NULL) {bank = c->drumset[0];}
		if (c->channel[ch].drums[note] == NULL)
				play_midi_setup_drums(c, ch, note);
		drum = c->channel[ch].drums[note];
		if (drum->reverb_level == -1 && bank->tone[nprog].reverb_send != -1) {
			drum->reverb_level = bank->tone[nprog].reverb_send;
		}
		if (drum->chorus_level == -1 && bank->tone[nprog].chorus_send != -1) {
			drum->chorus_level = bank->tone[nprog].chorus_send;
		}
		if (drum->delay_level == -1 && bank->tone[nprog].delay_send != -1) {
			drum->delay_level = bank->tone[nprog].delay_send;
		}
	} else {
		nprog = c->channel[ch].program;
		if (nprog == SPECIAL_PROGRAM) {return;}
		instrument_map(c, c->channel[ch].mapID, &nbank, &nprog);
		bank = c->tonebank[nbank];
		if (bank == NULL) {bank = c->tonebank[0];}
		c->channel[ch].legato = bank->tone[nprog].legato;
		c->channel[ch].damper_mode = bank->tone[nprog].damper_mode;
		c->channel[ch].loop_timeout = bank->tone[nprog].loop_timeout;
	}
}

Instrument *play_midi_load_instrument(struct timiditycontext_t *c, int dr, int bk, int prog)
{
	ToneBank **bank = (dr) ? c->drumset : c->tonebank;
	ToneBankElement *tone;
	Instrument *ip;
	int load_success = 0;

	if (bank[bk] == NULL)
		alloc_instrument_bank(c, dr, bk);

	tone = &bank[bk]->tone[prog];
	/* tone->name is NULL if "soundfont" directive is used, and ip is NULL when not preloaded */
	/* dr: not sure but only drumsets are concerned at the moment */
	if (dr && !tone->name && ((ip = tone->instrument) == MAGIC_LOAD_INSTRUMENT || ip == NULL)
		  && (ip = load_instrument(c, dr, bk, prog)) != NULL) {
		tone->instrument = ip;
		tone->name = safe_strdup(DYNAMIC_INSTRUMENT_NAME);
		load_success = 1;
	} else if (tone->name) {
		/* Instrument is found. */
		if ((ip = tone->instrument) == MAGIC_LOAD_INSTRUMENT
#ifndef SUPPRESS_CHANNEL_LAYER
			|| ip == NULL	/* see also readmidi.c: groom_list(). */
#endif
		) {ip = tone->instrument = load_instrument(c, dr, bk, prog);}
		if (ip == NULL || IS_MAGIC_INSTRUMENT(ip)) {
			tone->instrument = MAGIC_ERROR_INSTRUMENT;
		} else {
			load_success = 1;
		}
	} else {
		/* Instrument is not found.
		   Try to load the instrument from bank 0 */
		ToneBankElement *tone0 = &bank[0]->tone[prog];
		if ((ip = tone0->instrument) == NULL
			|| ip == MAGIC_LOAD_INSTRUMENT)
			ip = tone0->instrument = load_instrument(c, dr, 0, prog);
		if (ip == NULL || IS_MAGIC_INSTRUMENT(ip)) {
			tone0->instrument = MAGIC_ERROR_INSTRUMENT;
		} else {
			copy_tone_bank_element(tone, tone0);
			tone->instrument = ip;
			load_success = 1;
		}
	}

	if (load_success)
		aq_add(c, NULL, 0);	/* Update software buffer */

	if (ip == MAGIC_ERROR_INSTRUMENT)
		return NULL;

	return ip;
}

#if 0
/* reduce_voice_CPU() may not have any speed advantage over reduce_voice().
 * So this function is not used, now.
 */

/* The goal of this routine is to free as much CPU as possible without
   loosing too much sound quality.  We would like to know how long a note
   has been playing, but since we usually can't calculate this, we guess at
   the value instead.  A bad guess is better than nothing.  Notes which
   have been playing a short amount of time are killed first.  This causes
   decays and notes to be cut earlier, saving more CPU time.  It also causes
   notes which are closer to ending not to be cut as often, so it cuts
   a different note instead and saves more CPU in the long run.  ON voices
   are treated a little differently, since sound quality is more important
   than saving CPU at this point.  Duration guesses for loop regions are very
   crude, but are still better than nothing, they DO help.  Non-looping ON
   notes are cut before looping ON notes.  Since a looping ON note is more
   likely to have been playing for a long time, we want to keep it because it
   sounds better to keep long notes.
*/
static int reduce_voice_CPU(struct timiditycontext_t *c)
{
    int32 lv, v, vr;
    int i, j, lowest=-0x7FFFFFFF;
    int32 duration;

    i = c->upper_voices;
    lv = 0x7FFFFFFF;

    /* Look for the decaying note with the longest remaining decay time */
    /* Protect drum decays.  They do not take as much CPU (?) and truncating
       them early sounds bad, especially on snares and cymbals */
    for(j = 0; j < i; j++)
    {
	if(c->voice[j].status & VOICE_FREE || c->voice[j].cache != NULL)
	    continue;
	/* skip notes that don't need resampling (most drums) */
	if (c->voice[j].sample->note_to_use)
	    continue;
	if(c->voice[j].status & ~(VOICE_ON | VOICE_DIE | VOICE_SUSTAINED))
	{
	    /* Choose note with longest decay time remaining */
	    /* This frees more CPU than choosing lowest volume */
	    if (!c->voice[j].envelope_increment) duration = 0;
	    else duration =
		(c->voice[j].envelope_target - c->voice[j].envelope_volume) /
		c->voice[j].envelope_increment;
	    v = -duration;
	    if(v < lv)
	    {
		lv = v;
		lowest = j;
	    }
	}
    }
    if(lowest != -0x7FFFFFFF)
    {
	/* This can still cause a click, but if we had a free voice to
	   spare for ramping down this note, we wouldn't need to kill it
	   in the first place... Still, this needs to be fixed. Perhaps
	   we could use a reserve of voices to play dying notes only. */

	c->cut_notes++;
	return lowest;
    }

    /* try to remove VOICE_DIE before VOICE_ON */
    lv = 0x7FFFFFFF;
    lowest = -1;
    for(j = 0; j < i; j++)
    {
      if(c->voice[j].status & VOICE_FREE || c->voice[j].cache != NULL)
	    continue;
      if(c->voice[j].status & ~(VOICE_ON | VOICE_SUSTAINED))
      {
	/* continue protecting non-resample decays */
	if (c->voice[j].status & ~(VOICE_DIE) && c->voice[j].sample->note_to_use)
		continue;

	/* choose note which has been on the shortest amount of time */
	/* this is a VERY crude estimate... */
	if (c->voice[j].sample->modes & MODES_LOOPING)
	    duration = c->voice[j].sample_offset - c->voice[j].sample->loop_start;
	else
	    duration = c->voice[j].sample_offset;
	if (c->voice[j].sample_increment > 0)
	    duration /= c->voice[j].sample_increment;
	v = duration;
	if(v < lv)
	{
	    lv = v;
	    lowest = j;
	}
      }
    }
    if(lowest != -1)
    {
	c->cut_notes++;
	return lowest;
    }

    /* try to remove VOICE_SUSTAINED before VOICE_ON */
    lv = 0x7FFFFFFF;
    lowest = -0x7FFFFFFF;
    for(j = 0; j < i; j++)
    {
      if(c->voice[j].status & VOICE_FREE || c->voice[j].cache != NULL)
	    continue;
      if(c->voice[j].status & VOICE_SUSTAINED)
      {
	/* choose note which has been on the shortest amount of time */
	/* this is a VERY crude estimate... */
	if (c->voice[j].sample->modes & MODES_LOOPING)
	    duration = c->voice[j].sample_offset - c->voice[j].sample->loop_start;
	else
	    duration = c->voice[j].sample_offset;
	if (c->voice[j].sample_increment > 0)
	    duration /= c->voice[j].sample_increment;
	v = duration;
	if(v < lv)
	{
	    lv = v;
	    lowest = j;
	}
      }
    }
    if(lowest != -0x7FFFFFFF)
    {
	c->cut_notes++;
	return lowest;
    }

    /* try to remove chorus before VOICE_ON */
    lv = 0x7FFFFFFF;
    lowest = -0x7FFFFFFF;
    for(j = 0; j < i; j++)
    {
      if(c->voice[j].status & VOICE_FREE || c->voice[j].cache != NULL)
	    continue;
      if(c->voice[j].chorus_link < j)
      {
	/* score notes based on both volume AND duration */
	/* this scoring function needs some more tweaking... */
	if (c->voice[j].sample->modes & MODES_LOOPING)
	    duration = c->voice[j].sample_offset - c->voice[j].sample->loop_start;
	else
	    duration = c->voice[j].sample_offset;
	if (c->voice[j].sample_increment > 0)
	    duration /= c->voice[j].sample_increment;
	v = c->voice[j].left_mix * duration;
	vr = c->voice[j].right_mix * duration;
	if(c->voice[j].panned == PANNED_MYSTERY && vr > v)
	    v = vr;
	if(v < lv)
	{
	    lv = v;
	    lowest = j;
	}
      }
    }
    if(lowest != -0x7FFFFFFF)
    {
	c->cut_notes++;

	/* fix pan */
	j = c->voice[lowest].chorus_link;
	c->voice[j].panning = c->channel[c->voice[lowest].channel].panning;
	recompute_amp(c, j);
	apply_envelope_to_amp(c, j);

	return lowest;
    }

    c->lost_notes++;

    /* try to remove non-looping voices first */
    lv = 0x7FFFFFFF;
    lowest = -0x7FFFFFFF;
    for(j = 0; j < i; j++)
    {
      if(c->voice[j].status & VOICE_FREE || c->voice[j].cache != NULL)
	    continue;
      if(!(c->voice[j].sample->modes & MODES_LOOPING))
      {
	/* score notes based on both volume AND duration */
	/* this scoring function needs some more tweaking... */
	duration = c->voice[j].sample_offset;
	if (c->voice[j].sample_increment > 0)
	    duration /= c->voice[j].sample_increment;
	v = c->voice[j].left_mix * duration;
	vr = c->voice[j].right_mix * duration;
	if(c->voice[j].panned == PANNED_MYSTERY && vr > v)
	    v = vr;
	if(v < lv)
	{
	    lv = v;
	    lowest = j;
	}
      }
    }
    if(lowest != -0x7FFFFFFF)
    {
	return lowest;
    }

    lv = 0x7FFFFFFF;
    lowest = 0;
    for(j = 0; j < i; j++)
    {
	if(c->voice[j].status & VOICE_FREE || c->voice[j].cache != NULL)
	    continue;
	if (!(c->voice[j].sample->modes & MODES_LOOPING)) continue;

	/* score notes based on both volume AND duration */
	/* this scoring function needs some more tweaking... */
	duration = c->voice[j].sample_offset - c->voice[j].sample->loop_start;
	if (c->voice[j].sample_increment > 0)
	    duration /= c->voice[j].sample_increment;
	v = c->voice[j].left_mix * duration;
	vr = c->voice[j].right_mix * duration;
	if(c->voice[j].panned == PANNED_MYSTERY && vr > v)
	    v = vr;
	if(v < lv)
	{
	    lv = v;
	    lowest = j;
	}
    }

    return lowest;
}
#endif

/* this reduces voices while maintaining sound quality */
static int reduce_voice(struct timiditycontext_t *c)
{
    int32 lv, v;
    int i, j, lowest=-0x7FFFFFFF;

    i = c->upper_voices;
    lv = 0x7FFFFFFF;

    /* Look for the decaying note with the smallest volume */
    /* Protect drum decays.  Truncating them early sounds bad, especially on
       snares and cymbals */
    for(j = 0; j < i; j++)
    {
	if(c->voice[j].status & VOICE_FREE ||
	   (c->voice[j].sample->note_to_use && ISDRUMCHANNEL(c->voice[j].channel)))
	    continue;

	if(c->voice[j].status & ~(VOICE_ON | VOICE_DIE | VOICE_SUSTAINED))
	{
	    /* find lowest volume */
	    v = c->voice[j].left_mix;
	    if(c->voice[j].panned == PANNED_MYSTERY && c->voice[j].right_mix > v)
		v = c->voice[j].right_mix;
	    if(v < lv)
	    {
		lv = v;
		lowest = j;
	    }
	}
    }
    if(lowest != -0x7FFFFFFF)
    {
	/* This can still cause a click, but if we had a free voice to
	   spare for ramping down this note, we wouldn't need to kill it
	   in the first place... Still, this needs to be fixed. Perhaps
	   we could use a reserve of voices to play dying notes only. */

	c->cut_notes++;
	free_voice(c, lowest);
	if(!c->prescanning_flag)
	    ctl_note_event(c, lowest);
	return lowest;
    }

    /* try to remove VOICE_DIE before VOICE_ON */
    lv = 0x7FFFFFFF;
    lowest = -1;
    for(j = 0; j < i; j++)
    {
      if(c->voice[j].status & VOICE_FREE)
	    continue;
      if(c->voice[j].status & ~(VOICE_ON | VOICE_SUSTAINED))
      {
	/* continue protecting drum decays */
	if (c->voice[j].status & ~(VOICE_DIE) &&
	    (c->voice[j].sample->note_to_use && ISDRUMCHANNEL(c->voice[j].channel)))
		continue;
	/* find lowest volume */
	v = c->voice[j].left_mix;
	if(c->voice[j].panned == PANNED_MYSTERY && c->voice[j].right_mix > v)
	    v = c->voice[j].right_mix;
	if(v < lv)
	{
	    lv = v;
	    lowest = j;
	}
      }
    }
    if(lowest != -1)
    {
	c->cut_notes++;
	free_voice(c, lowest);
	if(!c->prescanning_flag)
	    ctl_note_event(c, lowest);
	return lowest;
    }

    /* try to remove VOICE_SUSTAINED before VOICE_ON */
    lv = 0x7FFFFFFF;
    lowest = -0x7FFFFFFF;
    for(j = 0; j < i; j++)
    {
      if(c->voice[j].status & VOICE_FREE)
	    continue;
      if(c->voice[j].status & VOICE_SUSTAINED)
      {
	/* find lowest volume */
	v = c->voice[j].left_mix;
	if(c->voice[j].panned == PANNED_MYSTERY && c->voice[j].right_mix > v)
	    v = c->voice[j].right_mix;
	if(v < lv)
	{
	    lv = v;
	    lowest = j;
	}
      }
    }
    if(lowest != -0x7FFFFFFF)
    {
	c->cut_notes++;
	free_voice(c, lowest);
	if(!c->prescanning_flag)
	    ctl_note_event(c, lowest);
	return lowest;
    }

    /* try to remove chorus before VOICE_ON */
    lv = 0x7FFFFFFF;
    lowest = -0x7FFFFFFF;
    for(j = 0; j < i; j++)
    {
      if(c->voice[j].status & VOICE_FREE)
	    continue;
      if(c->voice[j].chorus_link < j)
      {
	/* find lowest volume */
	v = c->voice[j].left_mix;
	if(c->voice[j].panned == PANNED_MYSTERY && c->voice[j].right_mix > v)
	    v = c->voice[j].right_mix;
	if(v < lv)
	{
	    lv = v;
	    lowest = j;
	}
      }
    }
    if(lowest != -0x7FFFFFFF)
    {
	c->cut_notes++;

	/* fix pan */
	j = c->voice[lowest].chorus_link;
	c->voice[j].panning = c->channel[c->voice[lowest].channel].panning;
	recompute_amp(c, j);
	apply_envelope_to_amp(c, j);

	free_voice(c, lowest);
	if(!c->prescanning_flag)
	    ctl_note_event(c, lowest);
	return lowest;
    }

    c->lost_notes++;

    /* remove non-drum VOICE_ON */
    lv = 0x7FFFFFFF;
    lowest = -0x7FFFFFFF;
    for(j = 0; j < i; j++)
    {
        if(c->voice[j].status & VOICE_FREE ||
	   (c->voice[j].sample->note_to_use && ISDRUMCHANNEL(c->voice[j].channel)))
		continue;

	/* find lowest volume */
	v = c->voice[j].left_mix;
	if(c->voice[j].panned == PANNED_MYSTERY && c->voice[j].right_mix > v)
	    v = c->voice[j].right_mix;
	if(v < lv)
	{
	    lv = v;
	    lowest = j;
	}
    }
    if(lowest != -0x7FFFFFFF)
    {
	free_voice(c, lowest);
	if(!c->prescanning_flag)
	    ctl_note_event(c, lowest);
	return lowest;
    }

    /* remove all other types of notes */
    lv = 0x7FFFFFFF;
    lowest = 0;
    for(j = 0; j < i; j++)
    {
	if(c->voice[j].status & VOICE_FREE)
	    continue;
	/* find lowest volume */
	v = c->voice[j].left_mix;
	if(c->voice[j].panned == PANNED_MYSTERY && c->voice[j].right_mix > v)
	    v = c->voice[j].right_mix;
	if(v < lv)
	{
	    lv = v;
	    lowest = j;
	}
    }

    free_voice(c, lowest);
    if(!c->prescanning_flag)
	ctl_note_event(c, lowest);
    return lowest;
}

void free_voice(struct timiditycontext_t *c, int v1)
{
    int v2;

#ifdef ENABLE_PAN_DELAY
	if (c->voice[v1].pan_delay_buf != NULL) {
		free(c->voice[v1].pan_delay_buf);
		c->voice[v1].pan_delay_buf = NULL;
	}
#endif /* ENABLE_PAN_DELAY */

    v2 = c->voice[v1].chorus_link;
    if(v1 != v2)
    {
	/* Unlink chorus link */
	c->voice[v1].chorus_link = v1;
	c->voice[v2].chorus_link = v2;
    }
    c->voice[v1].status = VOICE_FREE;
    c->voice[v1].temper_instant = 0;
}

static int find_free_voice(struct timiditycontext_t *c)
{
    int i, nv = c->voices, lowest;
    int32 lv, v;

    for(i = 0; i < nv; i++)
	if(c->voice[i].status == VOICE_FREE)
	{
	    if(c->upper_voices <= i)
		c->upper_voices = i + 1;
	    return i;
	}

    c->upper_voices = c->voices;

    /* Look for the decaying note with the lowest volume */
    lv = 0x7FFFFFFF;
    lowest = -1;
    for(i = 0; i < nv; i++)
    {
	if(c->voice[i].status & ~(VOICE_ON | VOICE_DIE) &&
	   !(c->voice[i].sample && c->voice[i].sample->note_to_use && ISDRUMCHANNEL(c->voice[i].channel)))
	{
	    v = c->voice[i].left_mix;
	    if((c->voice[i].panned==PANNED_MYSTERY) && (c->voice[i].right_mix>v))
		v = c->voice[i].right_mix;
	    if(v<lv)
	    {
		lv = v;
		lowest = i;
	    }
	}
    }
    if(lowest != -1 && !c->prescanning_flag)
    {
	free_voice(c, lowest);
	ctl_note_event(c, lowest);
    }
    return lowest;
}

static int find_samples(struct timiditycontext_t *c, MidiEvent *e, int *vlist)
{
	int i, j, ch, bank, prog, note, nv;
	SpecialPatch *s;
	Instrument *ip;

	ch = e->channel;
	if (c->channel[ch].special_sample > 0) {
		if ((s = c->special_patch[c->channel[ch].special_sample]) == NULL) {
			ctl->cmsg(CMSG_WARNING, VERB_VERBOSE,
					"Strange: Special patch %d is not installed",
					c->channel[ch].special_sample);
			return 0;
		}
		note = e->a + c->channel[ch].key_shift + c->note_key_offset;
		note = (note < 0) ? 0 : ((note > 127) ? 127 : note);
		return select_play_sample(c, s->sample, s->samples, &note, vlist, e,
					  (s->type == INST_GUS) ? 1 : 32);
	}
	bank = c->channel[ch].bank;
	if (ISDRUMCHANNEL(ch)) {
		note = e->a & 0x7f;
		instrument_map(c, c->channel[ch].mapID, &bank, &note);
		if (! (ip = play_midi_load_instrument(c, 1, bank, note)))
			return 0;	/* No instrument? Then we can't play. */
		/* if (ip->type == INST_GUS && ip->samples != 1)
			ctl->cmsg(CMSG_WARNING, VERB_VERBOSE,
					"Strange: percussion instrument with %d samples!",
					ip->samples); */
		/* "keynum" of SF2, and patch option "note=" */
		if (ip->sample->note_to_use)
			note = ip->sample->note_to_use;
	} else {
		if ((prog = c->channel[ch].program) == SPECIAL_PROGRAM)
			ip = c->default_instrument;
		else {
			instrument_map(c, c->channel[ch].mapID, &bank, &prog);
			if (! (ip = play_midi_load_instrument(c, 0, bank, prog)))
				return 0;	/* No instrument? Then we can't play. */
		}
		note = ((ip->sample->note_to_use) ? ip->sample->note_to_use : e->a)
				+ c->channel[ch].key_shift + c->note_key_offset;
		note = (note < 0) ? 0 : ((note > 127) ? 127 : note);
	}
	nv = select_play_sample(c, ip->sample, ip->samples, &note, vlist, e,
				(ip->type == INST_GUS) ? 1 : 32);
	/* Replace the sample if the sample is cached. */
	if (! c->prescanning_flag) {
		if (ip->sample->note_to_use)
			note = MIDI_EVENT_NOTE(e);
		for (i = 0; i < nv; i++) {
			j = vlist[i];
			if (! c->opt_realtime_playing && c->allocate_cache_size > 0
					&& ! c->channel[ch].portamento) {
				c->voice[j].cache = resamp_cache_fetch(c, c->voice[j].sample, note);
				if (c->voice[j].cache)	/* cache hit */
					c->voice[j].sample = c->voice[j].cache->resampled;
			} else
				c->voice[j].cache = NULL;
		}
	}
	return nv;
}

static int select_play_sample(struct timiditycontext_t *c, Sample *splist,
		int nsp, int *note, int *vlist, MidiEvent *e,
		int maxnv)
{
	int ch = e->channel, kn = e->a & 0x7f, vel = e->b;
	int32 f, fs, ft, fst, fc, fr, cdiff, diff, sample_link;
	int8 tt = c->channel[ch].temper_type;
	uint8 tp = c->channel[ch].rpnmap[RPN_ADDR_0003];
	Sample *sp, *spc, *spr;
	int16 sf, sn;
	double ratio;
	int i, j, k, nv, nvc;

	if (ISDRUMCHANNEL(ch))
		f = fs = c->freq_table[*note];
	else {
		if (c->opt_pure_intonation) {
			if (c->current_keysig < 8)
				f = c->freq_table_pureint[c->current_freq_table][*note];
			else
				f = c->freq_table_pureint[c->current_freq_table + 12][*note];
		} else if (c->opt_temper_control)
			switch (tt) {
			case 0:
				f = c->freq_table_tuning[tp][*note];
				break;
			case 1:
				if (c->current_temper_keysig < 8)
					f = c->freq_table_pytha[
							c->current_temper_freq_table][*note];
				else
					f = c->freq_table_pytha[
							c->current_temper_freq_table + 12][*note];
				break;
			case 2:
				if (c->current_temper_keysig < 8)
					f = c->freq_table_meantone[c->current_temper_freq_table
							+ ((c->temper_adj) ? 36 : 0)][*note];
				else
					f = c->freq_table_meantone[c->current_temper_freq_table
							+ ((c->temper_adj) ? 24 : 12)][*note];
				break;
			case 3:
				if (c->current_temper_keysig < 8)
					f = c->freq_table_pureint[c->current_temper_freq_table
							+ ((c->temper_adj) ? 36 : 0)][*note];
				else
					f = c->freq_table_pureint[c->current_temper_freq_table
							+ ((c->temper_adj) ? 24 : 12)][*note];
				break;
			default:	/* user-defined temperament */
				if ((tt -= 0x40) >= 0 && tt < 4) {
					if (c->current_temper_keysig < 8)
						f = c->freq_table_user[tt][c->current_temper_freq_table
								+ ((c->temper_adj) ? 36 : 0)][*note];
					else
						f = c->freq_table_user[tt][c->current_temper_freq_table
								+ ((c->temper_adj) ? 24 : 12)][*note];
				} else
					f = c->freq_table[*note];
				break;
			}
		else
			f = c->freq_table[*note];
		if (! c->opt_pure_intonation && c->opt_temper_control
				&& tt == 0 && f != c->freq_table[*note]) {
			*note = log(f / 440000.0) / log(2) * 12 + 69.5;
			*note = (*note < 0) ? 0 : ((*note > 127) ? 127 : *note);
			fs = c->freq_table[*note];
		} else
			fs = c->freq_table[*note];
	}
	nv = 0;
	for (i = 0, sp = splist; i < nsp; i++, sp++) {
		/* GUS/SF2 - Scale Tuning */
		if ((sf = sp->scale_factor) != 1024) {
			sn = sp->scale_freq;
			ratio = pow(2.0, (*note - sn) * (sf - 1024) / 12288.0);
			ft = f * ratio + 0.5, fst = fs * ratio + 0.5;
		} else
			ft = f, fst = fs;
		if (ISDRUMCHANNEL(ch) && c->channel[ch].drums[kn] != NULL)
			if ((ratio = get_play_note_ratio(c, ch, kn)) != 1.0)
				ft = ft * ratio + 0.5, fst = fst * ratio + 0.5;
		if (sp->low_freq <= fst && sp->high_freq >= fst
				&& sp->low_vel <= vel && sp->high_vel >= vel
				&& ! (sp->inst_type == INST_SF2
				&& sp->sample_type == SF_SAMPLETYPE_RIGHT)) {
			j = vlist[nv] = find_voice(c, e);
			c->voice[j].orig_frequency = ft;
			MYCHECK(c->voice[j].orig_frequency);
			c->voice[j].sample = sp;
			c->voice[j].status = VOICE_ON;
			if (++nv == maxnv) break;
		}
	}
	if (nv == 0) {	/* we must select at least one sample. */
		fr = fc = 0;
		spc = spr = NULL;
		cdiff = 0x7fffffff;
		for (i = 0, sp = splist; i < nsp; i++, sp++) {
			/* GUS/SF2 - Scale Tuning */
			if ((sf = sp->scale_factor) != 1024) {
				sn = sp->scale_freq;
				ratio = pow(2.0, (*note - sn) * (sf - 1024) / 12288.0);
				ft = f * ratio + 0.5, fst = fs * ratio + 0.5;
			} else
				ft = f, fst = fs;
			if (ISDRUMCHANNEL(ch) && c->channel[ch].drums[kn] != NULL)
				if ((ratio = get_play_note_ratio(c, ch, kn)) != 1.0)
					ft = ft * ratio + 0.5, fst = fst * ratio + 0.5;
			diff = abs(sp->root_freq - fst);
			if (diff < cdiff) {
				if (sp->inst_type == INST_SF2
						&& sp->sample_type == SF_SAMPLETYPE_RIGHT) {
					fr = ft;	/* reserve */
					spr = sp;	/* reserve */
				} else {
					fc = ft;
					spc = sp;
					cdiff = diff;
				}
			}
		}
		/* If spc is not NULL, a makeshift sample is found. */
		/* Otherwise, it's a lonely right sample, but better than nothing. */
		j = vlist[nv] = find_voice(c, e);
		c->voice[j].orig_frequency = (spc) ? fc : fr;
		MYCHECK(c->voice[j].orig_frequency);
		c->voice[j].sample = (spc) ? spc : spr;
		c->voice[j].status = VOICE_ON;
		nv++;
	}
	nvc = nv;
	for (i = 0; i < nvc; i++) {
		spc = c->voice[vlist[i]].sample;
		/* If it's left sample, there must be right sample. */
		if (spc->inst_type == INST_SF2
				&& spc->sample_type == SF_SAMPLETYPE_LEFT) {
			sample_link = spc->sf_sample_link;
			for (j = 0, sp = splist; j < nsp; j++, sp++)
				if (sp->inst_type == INST_SF2
						&& sp->sample_type == SF_SAMPLETYPE_RIGHT
						&& sp->sf_sample_index == sample_link) {
					/* right sample is found. */
					/* GUS/SF2 - Scale Tuning */
					if ((sf = sp->scale_factor) != 1024) {
						sn = sp->scale_freq;
						ratio = pow(2.0, (*note - sn) * (sf - 1024) / 12288.0);
						ft = f * ratio + 0.5;
					} else
						ft = f;
					if (ISDRUMCHANNEL(ch) && c->channel[ch].drums[kn] != NULL)
						if ((ratio = get_play_note_ratio(c, ch, kn)) != 1.0)
							ft = ft * ratio + 0.5;
					k = vlist[nv] = find_voice(c, e);
					c->voice[k].orig_frequency = ft;
					MYCHECK(c->voice[k].orig_frequency);
					c->voice[k].sample = sp;
					c->voice[k].status = VOICE_ON;
					nv++;
					break;
				}
		}
	}
	return nv;
}

static double get_play_note_ratio(struct timiditycontext_t *c, int ch, int note)
{
	int play_note = c->channel[ch].drums[note]->play_note;
	int bank = c->channel[ch].bank;
	ToneBank *dbank;
	int def_play_note;

	if (play_note == -1)
		return 1.0;
	instrument_map(c, c->channel[ch].mapID, &bank, &note);
	dbank = (c->drumset[bank]) ? c->drumset[bank] : c->drumset[0];
	if ((def_play_note = dbank->tone[note].play_note) == -1)
		return 1.0;
	if (play_note >= def_play_note)
		return c->bend_coarse[(play_note - def_play_note) & 0x7f];
	else
		return 1 / c->bend_coarse[(def_play_note - play_note) & 0x7f];
}

/* Only one instance of a note can be playing on a single channel. */
static int find_voice(struct timiditycontext_t *c, MidiEvent *e)
{
	int ch = e->channel;
	int note = MIDI_EVENT_NOTE(e);
	int status_check, mono_check;
	AlternateAssign *altassign;
	int i, lowest = -1;

	status_check = (c->opt_overlap_voice_allow)
			? (VOICE_OFF | VOICE_SUSTAINED) : 0xff;
	mono_check = c->channel[ch].mono;
	altassign = find_altassign(c->channel[ch].altassign, note);
	for (i = 0; i < c->upper_voices; i++)
		if (c->voice[i].status == VOICE_FREE) {
			lowest = i;	/* lower volume */
			break;
		}
	for (i = 0; i < c->upper_voices; i++)
		if (c->voice[i].status != VOICE_FREE && c->voice[i].channel == ch) {
			if (c->voice[i].note == note && (c->voice[i].status & status_check))
				kill_note(c, i);
			else if (mono_check)
				kill_note(c, i);
			else if (altassign && find_altassign(altassign, c->voice[i].note))
				kill_note(c, i);
			else if (c->voice[i].note == note && (c->channel[ch].assign_mode == 0
					|| (c->channel[ch].assign_mode == 1 &&
					    c->voice[i].proximate_flag == 0)))
				kill_note(c, i);
		}
	for (i = 0; i < c->upper_voices; i++)
		if (c->voice[i].channel == ch && c->voice[i].note == note)
			c->voice[i].proximate_flag = 0;
	if (lowest != -1)	/* Found a free voice. */
		return lowest;
	if (c->upper_voices < c->voices)
		return c->upper_voices++;
	return reduce_voice(c);
}

int32 get_note_freq(struct timiditycontext_t *c, Sample *sp, int note)
{
	int32 f;
	int16 sf, sn;
	double ratio;

	f = c->freq_table[note];
	/* GUS/SF2 - Scale Tuning */
	if ((sf = sp->scale_factor) != 1024) {
		sn = sp->scale_freq;
		ratio = pow(2.0, (note - sn) * (sf - 1024) / 12288.0);
		f = f * ratio + 0.5;
	}
	return f;
}

static int get_panning(struct timiditycontext_t *c, int ch, int note,int v)
{
    int pan;

	if(c->channel[ch].panning != NO_PANNING) {pan = (int)c->channel[ch].panning - 64;}
	else {pan = 0;}
	if(ISDRUMCHANNEL(ch) &&
	 c->channel[ch].drums[note] != NULL &&
	 c->channel[ch].drums[note]->drum_panning != NO_PANNING) {
		pan += c->channel[ch].drums[note]->drum_panning;
	} else {
		pan += c->voice[v].sample->panning;
	}

	if (pan > 127) pan = 127;
	else if (pan < 0) pan = 0;

	return pan;
}

/*! initialize vibrato parameters for a voice. */
static void init_voice_vibrato(struct timiditycontext_t *c, int v)
{
	Voice *vp = &(c->voice[v]);
	int ch = vp->channel, j, nrpn_vib_flag;
	double ratio;

	/* if NRPN vibrato is set, it's believed that there must be vibrato. */
	nrpn_vib_flag = c->opt_nrpn_vibrato
		&& (c->channel[ch].vibrato_ratio != 1.0 || c->channel[ch].vibrato_depth != 0);

	/* vibrato sweep */
	vp->vibrato_sweep = vp->sample->vibrato_sweep_increment;
	vp->vibrato_sweep_position = 0;

	/* vibrato rate */
	if (nrpn_vib_flag) {
		if(vp->sample->vibrato_control_ratio == 0) {
			ratio = cnv_Hz_to_vib_ratio(5.0) * c->channel[ch].vibrato_ratio;
		} else {
			ratio = (double)vp->sample->vibrato_control_ratio * c->channel[ch].vibrato_ratio;
		}
		if (ratio < 0) {ratio = 0;}
		vp->vibrato_control_ratio = (int)ratio;
	} else {
		vp->vibrato_control_ratio = vp->sample->vibrato_control_ratio;
	}

	/* vibrato depth */
	if (nrpn_vib_flag) {
		vp->vibrato_depth = vp->sample->vibrato_depth + c->channel[ch].vibrato_depth;
		if (vp->vibrato_depth > VIBRATO_DEPTH_MAX) {vp->vibrato_depth = VIBRATO_DEPTH_MAX;}
		else if (vp->vibrato_depth < 1) {vp->vibrato_depth = 1;}
		if (vp->sample->vibrato_depth < 0) {	/* in opposite phase */
			vp->vibrato_depth = -vp->vibrato_depth;
		}
	} else {
		vp->vibrato_depth = vp->sample->vibrato_depth;
	}

	/* vibrato delay */
	vp->vibrato_delay = vp->sample->vibrato_delay + c->channel[ch].vibrato_delay;

	/* internal parameters */
	vp->orig_vibrato_control_ratio = vp->vibrato_control_ratio;
	vp->vibrato_control_counter = vp->vibrato_phase = 0;
	for (j = 0; j < VIBRATO_SAMPLE_INCREMENTS; j++) {
		vp->vibrato_sample_increment[j] = 0;
	}
}

/*! initialize panning-delay for a voice. */
static void init_voice_pan_delay(struct timiditycontext_t *c, int v)
{
#ifdef ENABLE_PAN_DELAY
	Voice *vp = &(c->voice[v]);
	int ch = vp->channel;
	double pan_delay_diff;

	if (vp->pan_delay_buf != NULL) {
		free(vp->pan_delay_buf);
		vp->pan_delay_buf = NULL;
	}
	vp->pan_delay_rpt = 0;
	if (c->opt_pan_delay && c->channel[ch].insertion_effect == 0 && !c->opt_surround_chorus) {
		if (vp->panning == 64) {vp->delay += pan_delay_table[64] * play_mode->rate / 1000;}
		else {
			if(pan_delay_table[vp->panning] > pan_delay_table[127 - vp->panning]) {
				pan_delay_diff = pan_delay_table[vp->panning] - pan_delay_table[127 - vp->panning];
				vp->delay += (pan_delay_table[vp->panning] - pan_delay_diff) * play_mode->rate / 1000;
			} else {
				pan_delay_diff = pan_delay_table[127 - vp->panning] - pan_delay_table[vp->panning];
				vp->delay += (pan_delay_table[127 - vp->panning] - pan_delay_diff) * play_mode->rate / 1000;
			}
			vp->pan_delay_rpt = pan_delay_diff * play_mode->rate / 1000;
		}
		if(vp->pan_delay_rpt < 1) {vp->pan_delay_rpt = 0;}
		vp->pan_delay_wpt = 0;
		vp->pan_delay_spt = vp->pan_delay_wpt - vp->pan_delay_rpt;
		if (vp->pan_delay_spt < 0) {vp->pan_delay_spt += PAN_DELAY_BUF_MAX;}
		vp->pan_delay_buf = (int32 *)safe_malloc(sizeof(int32) * PAN_DELAY_BUF_MAX);
		memset(vp->pan_delay_buf, 0, sizeof(int32) * PAN_DELAY_BUF_MAX);
	}
#endif	/* ENABLE_PAN_DELAY */
}

/*! initialize portamento or legato for a voice. */
static void init_voice_portamento(struct timiditycontext_t *c, int v)
{
	Voice *vp = &(c->voice[v]);
	int ch = vp->channel;

  vp->porta_control_counter = 0;
  if(c->channel[ch].legato && c->channel[ch].legato_flag) {
	  update_legato_controls(c, ch);
  } else if(c->channel[ch].portamento && !c->channel[ch].porta_control_ratio) {
      update_portamento_controls(c, ch);
  }
  vp->porta_control_ratio = 0;
  if(c->channel[ch].porta_control_ratio)
  {
      if(c->channel[ch].last_note_fine == -1) {
	  /* first on */
	  c->channel[ch].last_note_fine = vp->note * 256;
	  c->channel[ch].porta_control_ratio = 0;
      } else {
	  vp->porta_control_ratio = c->channel[ch].porta_control_ratio;
	  vp->porta_dpb = c->channel[ch].porta_dpb;
	  vp->porta_pb = c->channel[ch].last_note_fine -
	      vp->note * 256;
	  if(vp->porta_pb == 0) {vp->porta_control_ratio = 0;}
      }
  }
}

/*! initialize tremolo for a voice. */
static void init_voice_tremolo(struct timiditycontext_t *c, int v)
{
	Voice *vp = &(c->voice[v]);

  vp->tremolo_delay = vp->sample->tremolo_delay;
  vp->tremolo_phase = 0;
  vp->tremolo_phase_increment = vp->sample->tremolo_phase_increment;
  vp->tremolo_sweep = vp->sample->tremolo_sweep_increment;
  vp->tremolo_sweep_position = 0;
  vp->tremolo_depth = vp->sample->tremolo_depth;
}

static void start_note(struct timiditycontext_t *c, MidiEvent *e, int i, int vid, int cnt)
{
  int j, ch, note;

  ch = e->channel;

  note = MIDI_EVENT_NOTE(e);
  c->voice[i].status = VOICE_ON;
  c->voice[i].channel = ch;
  c->voice[i].note = note;
  c->voice[i].velocity = e->b;
  c->voice[i].chorus_link = i;	/* No link */
  c->voice[i].proximate_flag = 1;

  j = c->channel[ch].special_sample;
  if(j == 0 || c->special_patch[j] == NULL)
      c->voice[i].sample_offset = 0;
  else
  {
      c->voice[i].sample_offset =
	  c->special_patch[j]->sample_offset << FRACTION_BITS;
      if(c->voice[i].sample->modes & MODES_LOOPING)
      {
	  if(c->voice[i].sample_offset > c->voice[i].sample->loop_end)
	      c->voice[i].sample_offset = c->voice[i].sample->loop_start;
      }
      else if(c->voice[i].sample_offset > c->voice[i].sample->data_length)
      {
	  free_voice(c, i);
	  return;
      }
  }
  c->voice[i].sample_increment = 0; /* make sure it isn't negative */
  c->voice[i].vid = vid;
  c->voice[i].delay = c->voice[i].sample->envelope_delay;
  c->voice[i].modenv_delay = c->voice[i].sample->modenv_delay;
  c->voice[i].delay_counter = 0;

  init_voice_tremolo(c, i);	/* tremolo */
  init_voice_filter(c, i);		/* resonant lowpass filter */
  init_voice_vibrato(c, i);	/* vibrato */
  c->voice[i].panning = get_panning(c, ch, note, i);	/* pan */
  init_voice_pan_delay(c, i);	/* panning-delay */
  init_voice_portamento(c, i);	/* portamento or legato */

  if(cnt == 0)
      c->channel[ch].last_note_fine = c->voice[i].note * 256;

  /* initialize modulation envelope */
  if (c->voice[i].sample->modes & MODES_ENVELOPE)
    {
	  c->voice[i].modenv_stage = EG_GUS_ATTACK;
      c->voice[i].modenv_volume = 0;
      recompute_modulation_envelope(c, i);
	  apply_modulation_envelope(c, i);
    }
  else
    {
	  c->voice[i].modenv_increment=0;
	  apply_modulation_envelope(c, i);
    }
  recompute_freq(c, i);
  recompute_voice_filter(c, i);

  recompute_amp(c, i);
  /* initialize volume envelope */
  if (c->voice[i].sample->modes & MODES_ENVELOPE)
    {
      /* Ramp up from 0 */
	  c->voice[i].envelope_stage = EG_GUS_ATTACK;
      c->voice[i].envelope_volume = 0;
      c->voice[i].control_counter = 0;
      recompute_envelope(c, i);
	  apply_envelope_to_amp(c, i);
    }
  else
    {
      c->voice[i].envelope_increment = 0;
      apply_envelope_to_amp(c, i);
    }

  c->voice[i].timeout = -1;
  if(!c->prescanning_flag)
      ctl_note_event(c, i);
}

static void finish_note(struct timiditycontext_t *c, int i)
{
    if (c->voice[i].sample->modes & MODES_ENVELOPE)
    {
		/* We need to get the envelope out of Sustain stage. */
		/* Note that voice[i].envelope_stage < EG_GUS_RELEASE1 */
		c->voice[i].status = VOICE_OFF;
		c->voice[i].envelope_stage = EG_GUS_RELEASE1;
		recompute_envelope(c, i);
		c->voice[i].modenv_stage = EG_GUS_RELEASE1;
		recompute_modulation_envelope(c, i);
		apply_modulation_envelope(c, i);
		apply_envelope_to_amp(c, i);
		ctl_note_event(c, i);
	}
    else
    {
		if(c->current_file_info->pcm_mode != PCM_MODE_NON)
		{
			free_voice(c, i);
			ctl_note_event(c, i);
		}
		else
		{
			/* Set status to OFF so resample_voice() will let this voice out
			of its loop, if any. In any case, this voice dies when it
				hits the end of its data (ofs>=data_length). */
			if(c->voice[i].status != VOICE_OFF)
			{
			c->voice[i].status = VOICE_OFF;
			ctl_note_event(c, i);
			}
		}
    }
}

static void set_envelope_time(struct timiditycontext_t *c, int ch, int val, int stage)
{
	val = val & 0x7F;
	switch(stage) {
	case EG_ATTACK:	/* Attack */
		ctl->cmsg(CMSG_INFO,VERB_NOISY,"Attack Time (CH:%d VALUE:%d)", ch, val);
		break;
	case EG_DECAY: /* Decay */
		ctl->cmsg(CMSG_INFO,VERB_NOISY,"Decay Time (CH:%d VALUE:%d)", ch, val);
		break;
	case EG_RELEASE:	/* Release */
		ctl->cmsg(CMSG_INFO,VERB_NOISY,"Release Time (CH:%d VALUE:%d)", ch, val);
		break;
	default:
		ctl->cmsg(CMSG_INFO,VERB_NOISY,"? Time (CH:%d VALUE:%d)", ch, val);
	}
	c->channel[ch].envelope_rate[stage] = val;
}

/*! pseudo Delay Effect without DSP */
#ifndef USE_DSP_EFFECT
static void new_delay_voice(struct timiditycontext_t *c, int v1, int level)
{
    int v2, ch = c->voice[v1].channel;
	FLOAT_T delay, vol;
	FLOAT_T threshold = 1.0;

	/* NRPN Delay Send Level of Drum */
	if(ISDRUMCHANNEL(ch) &&	c->channel[ch].drums[c->voice[v1].note] != NULL) {
		level *= (FLOAT_T)c->channel[ch].drums[c->voice[v1].note]->delay_level / 127.0;
	}

	vol = c->voice[v1].velocity * level / 127.0 * c->delay_status_gs.level_ratio_c;

	if (vol > threshold) {
		delay = 0;
		if((v2 = find_free_voice(c)) == -1) {return;}
		c->voice[v2].cache = NULL;
		delay += c->delay_status_gs.time_center;
		c->voice[v2] = c->voice[v1];	/* copy all parameters */
		c->voice[v2].velocity = (uint8)vol;
		c->voice[v2].delay += (int32)(play_mode->rate * delay / 1000);
		init_voice_pan_delay(c, v2);
		recompute_amp(c, v2);
		apply_envelope_to_amp(c, v2);
		recompute_freq(c, v2);
	}
}


static void new_chorus_voice(struct timiditycontext_t *c, int v1, int level)
{
    int v2, ch;

    if((v2 = find_free_voice(c)) == -1)
	return;
    ch = c->voice[v1].channel;
    c->voice[v2] = c->voice[v1];	/* copy all parameters */

    /* NRPN Chorus Send Level of Drum */
    if(ISDRUMCHANNEL(ch) && c->channel[ch].drums[c->voice[v1].note] != NULL) {
	level *= (FLOAT_T)c->channel[ch].drums[c->voice[v1].note]->chorus_level / 127.0;
    }

    /* Choose lower voice index for base voice (v1) */
    if(v1 > v2)
    {
	v1 ^= v2;
	v2 ^= v1;
	v1 ^= v2;
    }

    /* v1: Base churos voice
     * v2: Sub chorus voice (detuned)
     */

    /* Make doubled link v1 and v2 */
    c->voice[v1].chorus_link = v2;
    c->voice[v2].chorus_link = v1;

    level >>= 2;		     /* scale level to a "better" value */
    if(c->channel[ch].pitchbend + level < 0x2000)
        c->voice[v2].orig_frequency *= c->bend_fine[level];
    else
	c->voice[v2].orig_frequency /= c->bend_fine[level];

    MYCHECK(c->voice[v2].orig_frequency);

    c->voice[v2].cache = NULL;

    /* set panning & delay */
    if(!(play_mode->encoding & PE_MONO))
    {
	double delay;

	if(c->voice[v2].panned == PANNED_CENTER)
	{
	    c->voice[v2].panning = 64 + int_rand(40) - 20; /* 64 +- rand(20) */
	    delay = 0;
	}
	else
	{
	    int panning = c->voice[v2].panning;

	    if(panning < CHORUS_OPPOSITE_THRESHOLD)
	    {
		c->voice[v2].panning = 127;
		delay = DEFAULT_CHORUS_DELAY1;
	    }
	    else if(panning > 127 - CHORUS_OPPOSITE_THRESHOLD)
	    {
		c->voice[v2].panning = 0;
		delay = DEFAULT_CHORUS_DELAY1;
	    }
	    else
	    {
		c->voice[v2].panning = (panning < 64 ? 0 : 127);
		delay = DEFAULT_CHORUS_DELAY2;
	    }
	}
	c->voice[v2].delay += (int)(play_mode->rate * delay);
    }

    init_voice_pan_delay(c, v1);
    init_voice_pan_delay(c, v2);

    recompute_amp(c, v1);
    apply_envelope_to_amp(c, v1);
    recompute_amp(c, v2);
    apply_envelope_to_amp(c, v2);

    /* voice[v2].orig_frequency is changed.
     * Update the depened parameters.
     */
    recompute_freq(c, v2);
}
#endif /* !USE_DSP_EFFECT */


/* Yet another chorus implementation
 *	by Eric A. Welsh <ewelsh@gpc.wustl.edu>.
 */
static void new_chorus_voice_alternate(struct timiditycontext_t *c, int v1, int level)
{
    int v2, ch, panlevel;
    uint8 pan;
    double delay;
    double freq, frac;
    int note_adjusted;

    if((v2 = find_free_voice(c)) == -1)
	return;
    ch = c->voice[v1].channel;
    c->voice[v2] = c->voice[v1];

    /* NRPN Chorus Send Level of Drum */
    if(ISDRUMCHANNEL(ch) && c->channel[ch].drums[c->voice[v1].note] != NULL) {
	level *= (FLOAT_T)c->channel[ch].drums[c->voice[v1].note]->chorus_level / 127.0;
    }

    /* for our purposes, hard left will be equal to 1 instead of 0 */
    pan = c->voice[v1].panning;
    if (!pan) pan = 1;

    /* Choose lower voice index for base voice (v1) */
    if(v1 > v2)
    {
	v1 ^= v2;
	v2 ^= v1;
	v1 ^= v2;
    }

    /* Make doubled link v1 and v2 */
    c->voice[v1].chorus_link = v2;
    c->voice[v2].chorus_link = v1;

    /* detune notes for chorus effect */
    level >>= 2;		/* scale to a "better" value */
    if (level)
    {
        if(c->channel[ch].pitchbend + level < 0x2000)
            c->voice[v2].orig_frequency *= c->bend_fine[level];
        else
	    c->voice[v2].orig_frequency /= c->bend_fine[level];
        c->voice[v2].cache = NULL;
    }

    MYCHECK(c->voice[v2].orig_frequency);

    delay = 0.003;

    /* Try to keep the delayed voice from cancelling out the other voice */
    /* Pitch detection is used to find the real pitches for drums and MODs */
    note_adjusted = c->voice[v1].note + c->voice[v1].sample->transpose_detected;
    if (note_adjusted > 127) note_adjusted = 127;
    else if (note_adjusted < 0) note_adjusted = 0;
    freq = pitch_freq_table[note_adjusted];
    delay *= freq;
    frac = delay - floor(delay);

    /* force the delay away from 0.5 period */
    if (frac < 0.5 && frac > 0.40)
    {
	delay = (floor(delay) + 0.40) / freq;
	if (!(play_mode->encoding & PE_MONO))
	    delay += (0.5 - frac) * (1.0 - labs(64 - pan) / 63.0) / freq;
    }
    else if (frac >= 0.5 && frac < 0.60)
    {
	delay = (floor(delay) + 0.60) / freq;
	if (!(play_mode->encoding & PE_MONO))
	    delay += (0.5 - frac) * (1.0 - labs(64 - pan) / 63.0) / freq;
    }
    else
	delay = 0.003;

    /* set panning & delay for pseudo-surround effect */
    if(play_mode->encoding & PE_MONO)    /* delay sounds good */
        c->voice[v2].delay += (int)(play_mode->rate * delay);
    else
    {
        panlevel = 63;
        if (pan - panlevel < 1) panlevel = pan - 1;
        if (pan + panlevel > 127) panlevel = 127 - pan;
        c->voice[v1].panning -= panlevel;
        c->voice[v2].panning += panlevel;

        /* choose which voice is delayed based on panning */
        if (c->voice[v1].panned == PANNED_CENTER) {
            /* randomly choose which voice is delayed */
            if (int_rand(2))
                c->voice[v1].delay += (int)(play_mode->rate * delay);
            else
                c->voice[v2].delay += (int)(play_mode->rate * delay);
        }
        else if (pan - 64 < 0) {
            c->voice[v2].delay += (int)(play_mode->rate * delay);
        }
        else {
            c->voice[v1].delay += (int)(play_mode->rate * delay);
        }
    }

    /* check for similar drums playing simultaneously with center pans */
    if (!(play_mode->encoding & PE_MONO) &&
	ISDRUMCHANNEL(ch) && c->voice[v1].panned == PANNED_CENTER)
    {
	int i, j;

	/* force Rimshot (37), Snare1 (38), Snare2 (40), and XG #34 to have
	 * the same delay, otherwise there will be bad voice cancellation.
	 */
	if (c->voice[v1].note == 37 ||
	    c->voice[v1].note == 38 ||
	    c->voice[v1].note == 40 ||
	    (c->voice[v1].note == 34 && c->play_system_mode == XG_SYSTEM_MODE))
	{
	    for (i = 0; i < c->upper_voices; i++)
	    {
		if (c->voice[i].status & (VOICE_DIE | VOICE_FREE))
		    continue;

		if (!ISDRUMCHANNEL(c->voice[i].channel))
		    continue;

		if (i == v1 || i == v2)
		    continue;

		if (c->voice[i].note == 37 ||
		    c->voice[i].note == 38 ||
		    c->voice[i].note == 40 ||
		    (c->voice[i].note == 34 &&
		     c->play_system_mode == XG_SYSTEM_MODE))
		{
		    j = c->voice[i].chorus_link;

		    if (c->voice[i].panned == PANNED_LEFT &&
		        c->voice[j].panned == PANNED_RIGHT)
		    {
			c->voice[v1].delay = c->voice[i].delay;
			c->voice[v2].delay = c->voice[j].delay;

			break;
		    }
		}
	    }
	}

	/* force Kick1 (35), Kick2 (36), and XG Kick #33 to have the same
	 * delay, otherwise there will be bad voice cancellation.
	 */
	if (c->voice[v1].note == 35 ||
	    c->voice[v1].note == 36 ||
	    (c->voice[v1].note == 33 && c->play_system_mode == XG_SYSTEM_MODE))
	{
	    for (i = 0; i < c->upper_voices; i++)
	    {
		if (c->voice[i].status & (VOICE_DIE | VOICE_FREE))
		    continue;

		if (!ISDRUMCHANNEL(c->voice[i].channel))
		    continue;

		if (i == v1 || i == v2)
		    continue;

		if (c->voice[i].note == 35 ||
		    c->voice[i].note == 36 ||
		    (c->voice[i].note == 33 &&
		     c->play_system_mode == XG_SYSTEM_MODE))
		{
		    j = c->voice[i].chorus_link;

		    if (c->voice[i].panned == PANNED_LEFT &&
		        c->voice[j].panned == PANNED_RIGHT)
		    {
			c->voice[v1].delay = c->voice[i].delay;
			c->voice[v2].delay = c->voice[j].delay;

			break;
		    }
		}
	    }
	}
    }

    init_voice_pan_delay(c, v1);
    init_voice_pan_delay(c, v2);

    recompute_amp(c, v1);
    apply_envelope_to_amp(c, v1);
    recompute_amp(c, v2);
    apply_envelope_to_amp(c, v2);
    if (level) recompute_freq(c, v2);
}

/*! note_on() (prescanning) */
static void note_on_prescan(struct timiditycontext_t *c, MidiEvent *ev)
{
	int i, ch = ev->channel, note = MIDI_EVENT_NOTE(ev);
	int32 random_delay = 0;

	if(ISDRUMCHANNEL(ch) &&
	   c->channel[ch].drums[note] != NULL &&
	   !get_rx_drum(c->channel[ch].drums[note], RX_NOTE_ON)) {	/* Rx. Note On */
		return;
	}
	if(c->channel[ch].note_limit_low > note ||
		c->channel[ch].note_limit_high < note ||
		c->channel[ch].vel_limit_low > ev->b ||
		c->channel[ch].vel_limit_high < ev->b) {
		return;
	}

    if((c->channel[ch].portamento_time_msb |
		c->channel[ch].portamento_time_lsb) == 0 ||
	    c->channel[ch].portamento == 0)
	{
		int nv;
		int vlist[32];
		Voice *vp;

		nv = find_samples(c, ev, vlist);

		for(i = 0; i < nv; i++)
		{
		    vp = c->voice + vlist[i];
		    start_note(c, ev, vlist[i], 0, nv - i - 1);
			vp->delay += random_delay;
			vp->modenv_delay += random_delay;
		    resamp_cache_refer_on(c, vp, ev->time);
		    vp->status = VOICE_FREE;
		    vp->temper_instant = 0;
		}
	}
}

static void note_on(struct timiditycontext_t *c, MidiEvent *e)
{
    int i, nv, v, ch, note;
    int vlist[32];
    int vid;
	int32 random_delay = 0;

	ch = e->channel;
	note = MIDI_EVENT_NOTE(e);

	if(ISDRUMCHANNEL(ch) &&
	   c->channel[ch].drums[note] != NULL &&
	   !get_rx_drum(c->channel[ch].drums[note], RX_NOTE_ON)) {	/* Rx. Note On */
		return;
	}
	if(c->channel[ch].note_limit_low > note ||
		c->channel[ch].note_limit_high < note ||
		c->channel[ch].vel_limit_low > e->b ||
		c->channel[ch].vel_limit_high < e->b) {
		return;
	}
    if((nv = find_samples(c, e, vlist)) == 0)
	return;

    vid = new_vidq(c, e->channel, note);

	recompute_bank_parameter(c, ch, note);
	recompute_channel_filter(c, ch, note);
	random_delay = calc_random_delay(c, ch, note);

    for(i = 0; i < nv; i++)
    {
	v = vlist[i];
	if(ISDRUMCHANNEL(ch) &&
	   c->channel[ch].drums[note] != NULL &&
	   c->channel[ch].drums[note]->pan_random)
	    c->channel[ch].drums[note]->drum_panning = int_rand(128);
	else if(c->channel[ch].pan_random)
	{
	    c->channel[ch].panning = int_rand(128);
	    ctl_mode_event(c, CTLE_PANNING, 1, ch, c->channel[ch].panning);
	}
	start_note(c, e, v, vid, nv - i - 1);
	c->voice[v].delay += random_delay;
	c->voice[v].modenv_delay += random_delay;
#ifdef SMOOTH_MIXING
	c->voice[v].old_left_mix = c->voice[v].old_right_mix =
	c->voice[v].left_mix_inc = c->voice[v].left_mix_offset =
	c->voice[v].right_mix_inc = c->voice[v].right_mix_offset = 0;
#endif
#ifdef USE_DSP_EFFECT
	if(c->opt_surround_chorus)
	    new_chorus_voice_alternate(c, v, 0);
#else
	if((c->channel[ch].chorus_level || c->opt_surround_chorus))
	{
	    if(c->opt_surround_chorus)
		new_chorus_voice_alternate(c, v, c->channel[ch].chorus_level);
	    else
		new_chorus_voice(c, v, c->channel[ch].chorus_level);
	}
	if(c->channel[ch].delay_level)
	{
	    new_delay_voice(c, v, c->channel[ch].delay_level);
	}
#endif
    }

    c->channel[ch].legato_flag = 1;
}

/*! sostenuto is now implemented as an instant sustain */
static void update_sostenuto_controls(struct timiditycontext_t *c, int ch)
{
  int uv = c->upper_voices, i;

  if(ISDRUMCHANNEL(ch) || c->channel[ch].sostenuto == 0) {return;}

  for(i = 0; i < uv; i++)
  {
	if ((c->voice[i].status & (VOICE_ON | VOICE_OFF))
			&& c->voice[i].channel == ch)
	 {
		  c->voice[i].status = VOICE_SUSTAINED;
		  ctl_note_event(c, i);
		  c->voice[i].envelope_stage = EG_GUS_RELEASE1;
		  recompute_envelope(c, i);
	 }
  }
}

/*! redamper effect for piano instruments */
static void update_redamper_controls(struct timiditycontext_t *c, int ch)
{
  int uv = c->upper_voices, i;

  if(ISDRUMCHANNEL(ch) || c->channel[ch].damper_mode == 0) {return;}

  for(i = 0; i < uv; i++)
  {
	if ((c->voice[i].status & (VOICE_ON | VOICE_OFF))
			&& c->voice[i].channel == ch)
	  {
		  c->voice[i].status = VOICE_SUSTAINED;
		  ctl_note_event(c, i);
		  c->voice[i].envelope_stage = EG_GUS_RELEASE1;
		  recompute_envelope(c, i);
	  }
  }
}

static void note_off(struct timiditycontext_t *c, MidiEvent *e)
{
  int uv = c->upper_voices, i;
  int ch, note, vid, sustain;

  ch = e->channel;
  note = MIDI_EVENT_NOTE(e);

  if(ISDRUMCHANNEL(ch))
  {
      int nbank, nprog;

      nbank = c->channel[ch].bank;
      nprog = note;
      instrument_map(c, c->channel[ch].mapID, &nbank, &nprog);

      if (c->channel[ch].drums[nprog] != NULL &&
          get_rx_drum(c->channel[ch].drums[nprog], RX_NOTE_OFF))
      {
          ToneBank *bank;
          bank = c->drumset[nbank];
          if(bank == NULL) bank = c->drumset[0];

          /* uh oh, this drum doesn't have an instrument loaded yet */
          if (bank->tone[nprog].instrument == NULL)
              return;

          /* this drum is not loaded for some reason (error occured?) */
          if (IS_MAGIC_INSTRUMENT(bank->tone[nprog].instrument))
              return;

          /* only disallow Note Off if the drum sample is not looped */
          if (!(bank->tone[nprog].instrument->sample->modes & MODES_LOOPING))
              return;	/* Note Off is not allowed. */
      }
  }

  if ((vid = last_vidq(c, ch, note)) == -1)
      return;
  sustain = c->channel[ch].sustain;
  for (i = 0; i < uv; i++)
  {
      if(c->voice[i].status == VOICE_ON &&
	 c->voice[i].channel == ch &&
	 c->voice[i].note == note &&
	 c->voice[i].vid == vid)
      {
	  if(sustain)
	  {
	      c->voice[i].status = VOICE_SUSTAINED;
	      ctl_note_event(c, i);
	  }
	  else
	      finish_note(c, i);
      }
  }

  c->channel[ch].legato_flag = 0;
}

/* Process the All Notes Off event */
static void all_notes_off(struct timiditycontext_t *c, int ch)
{
  int i, uv = c->upper_voices;
  ctl->cmsg(CMSG_INFO, VERB_DEBUG, "All notes off on channel %d", ch);
  for(i = 0; i < uv; i++)
    if (c->voice[i].status==VOICE_ON &&
	c->voice[i].channel==ch)
      {
	if (c->channel[ch].sustain)
	  {
	    c->voice[i].status=VOICE_SUSTAINED;
	    ctl_note_event(c, i);
	  }
	else
	  finish_note(c, i);
      }
  for(i = 0; i < 128; i++)
      c->vidq_head[ch * 128 + i] = c->vidq_tail[ch * 128 + i] = 0;
}

/* Process the All Sounds Off event */
static void all_sounds_off(struct timiditycontext_t *c, int ch)
{
  int i, uv = c->upper_voices;
  for(i = 0; i < uv; i++)
    if (c->voice[i].channel==ch &&
	(c->voice[i].status & ~(VOICE_FREE | VOICE_DIE)))
      {
	kill_note(c, i);
      }
  for(i = 0; i < 128; i++)
      c->vidq_head[ch * 128 + i] = c->vidq_tail[ch * 128 + i] = 0;
}

/*! adjust polyphonic key pressure (PAf, PAT) */
static void adjust_pressure(struct timiditycontext_t *c, MidiEvent *e)
{
    int i, uv = c->upper_voices;
    int note, ch;

    if(c->opt_channel_pressure)
    {
	ch = e->channel;
    note = MIDI_EVENT_NOTE(e);
	c->channel[ch].paf.val = e->b;
	if(c->channel[ch].paf.pitch != 0) {c->channel[ch].pitchfactor = 0;}

    for(i = 0; i < uv; i++)
    if(c->voice[i].status == VOICE_ON &&
       c->voice[i].channel == ch &&
       c->voice[i].note == note)
    {
		recompute_amp(c, i);
		apply_envelope_to_amp(c, i);
		recompute_freq(c, i);
		recompute_voice_filter(c, i);
    }
	}
}

/*! adjust channel pressure (channel aftertouch, CAf, CAT) */
static void adjust_channel_pressure(struct timiditycontext_t *c, MidiEvent *e)
{
    if(c->opt_channel_pressure)
    {
	int i, uv = c->upper_voices;
	int ch;

	ch = e->channel;
	c->channel[ch].caf.val = e->a;
	if(c->channel[ch].caf.pitch != 0) {c->channel[ch].pitchfactor = 0;}

	for(i = 0; i < uv; i++)
	{
	    if(c->voice[i].status == VOICE_ON && c->voice[i].channel == ch)
	    {
		recompute_amp(c, i);
		apply_envelope_to_amp(c, i);
		recompute_freq(c, i);
		recompute_voice_filter(c, i);
		}
	}
    }
}

static void adjust_panning(struct timiditycontext_t *c, int ch)
{
    int i, uv = c->upper_voices, pan = c->channel[ch].panning;
    for(i = 0; i < uv; i++)
    {
	if ((c->voice[i].channel==ch) &&
	    (c->voice[i].status & (VOICE_ON | VOICE_SUSTAINED)))
	{
            /* adjust pan to include drum/sample pan offsets */
            pan = get_panning(c, ch, c->voice[i].note, i);

	    /* Hack to handle -EFchorus=2 in a "reasonable" way */
#ifdef USE_DSP_EFFECT
	    if(c->opt_surround_chorus && c->voice[i].chorus_link != i)
#else
	    if((c->channel[ch].chorus_level || c->opt_surround_chorus) &&
	       c->voice[i].chorus_link != i)
#endif
	    {
		int v1, v2;

		if(i >= c->voice[i].chorus_link)
		    /* `i' is not base chorus voice.
		     *  This sub voice is already updated.
		     */
		    continue;

		v1 = i;				/* base voice */
		v2 = c->voice[i].chorus_link;	/* sub voice (detuned) */

		if(c->opt_surround_chorus) /* Surround chorus mode by Eric. */
		{
		    int panlevel;

		    if (!pan) pan = 1;	/* make hard left be 1 instead of 0 */
		    panlevel = 63;
		    if (pan - panlevel < 1) panlevel = pan - 1;
		    if (pan + panlevel > 127) panlevel = 127 - pan;
		    c->voice[v1].panning = pan - panlevel;
		    c->voice[v2].panning = pan + panlevel;
		}
		else
		{
		    c->voice[v1].panning = pan;
		    if(pan > 60 && pan < 68) /* PANNED_CENTER */
			c->voice[v2].panning =
			    64 + int_rand(40) - 20; /* 64 +- rand(20) */
		    else if(pan < CHORUS_OPPOSITE_THRESHOLD)
			c->voice[v2].panning = 127;
		    else if(pan > 127 - CHORUS_OPPOSITE_THRESHOLD)
			c->voice[v2].panning = 0;
		    else
			c->voice[v2].panning = (pan < 64 ? 0 : 127);
		}
		recompute_amp(c, v2);
		apply_envelope_to_amp(c, v2);
		/* v1 == i, so v1 will be updated next */
	    }
	    else
		c->voice[i].panning = pan;

	    recompute_amp(c, i);
	    apply_envelope_to_amp(c, i);
	}
    }
}

static void play_midi_setup_drums(struct timiditycontext_t *c, int ch, int note)
{
    c->channel[ch].drums[note] = (struct DrumParts *)
	new_segment(c, &c->playmidi_pool, sizeof(struct DrumParts));
    reset_drum_controllers(c->channel[ch].drums, note);
}

static void adjust_drum_panning(struct timiditycontext_t *c, int ch, int note)
{
    int i, uv = c->upper_voices;

    for(i = 0; i < uv; i++) {
		if(c->voice[i].channel == ch &&
		   c->voice[i].note == note &&
		   (c->voice[i].status & (VOICE_ON | VOICE_SUSTAINED)))
		{
			c->voice[i].panning = get_panning(c, ch, note, i);
			recompute_amp(c, i);
			apply_envelope_to_amp(c, i);
		}
	}
}

static void drop_sustain(struct timiditycontext_t *c, int ch)
{
  int i, uv = c->upper_voices;
  for(i = 0; i < uv; i++)
    if (c->voice[i].status == VOICE_SUSTAINED && c->voice[i].channel == ch)
      finish_note(c, i);
}

static void adjust_all_pitch(struct timiditycontext_t *c)
{
	int ch, i, uv = c->upper_voices;

	for (ch = 0; ch < MAX_CHANNELS; ch++)
		c->channel[ch].pitchfactor = 0;
	for (i = 0; i < uv; i++)
		if (c->voice[i].status != VOICE_FREE)
			recompute_freq(c, i);
}

static void adjust_pitch(struct timiditycontext_t *c, int ch)
{
  int i, uv = c->upper_voices;
  for(i = 0; i < uv; i++)
    if (c->voice[i].status != VOICE_FREE && c->voice[i].channel == ch)
	recompute_freq(c, i);
}

static void adjust_volume(struct timiditycontext_t *c, int ch)
{
  int i, uv = c->upper_voices;
  for(i = 0; i < uv; i++)
    if (c->voice[i].channel == ch &&
	(c->voice[i].status & (VOICE_ON | VOICE_SUSTAINED)))
      {
	recompute_amp(c, i);
	apply_envelope_to_amp(c, i);
      }
}

static void set_reverb_level(struct timiditycontext_t *c, int ch, int level)
{
	if (level == -1) {
		c->channel[ch].reverb_level = c->channel[ch].reverb_id =
				(c->opt_reverb_control < 0)
				? -c->opt_reverb_control & 0x7f : DEFAULT_REVERB_SEND_LEVEL;
		c->make_rvid_flag = 1;
		return;
	}
	c->channel[ch].reverb_level = level;
	c->make_rvid_flag = 0;	/* to update reverb_id */
}

int get_reverb_level(struct timiditycontext_t *c, int ch)
{
	if (c->channel[ch].reverb_level == -1)
		return (c->opt_reverb_control < 0)
			? -c->opt_reverb_control & 0x7f : DEFAULT_REVERB_SEND_LEVEL;
	return c->channel[ch].reverb_level;
}

int get_chorus_level(struct timiditycontext_t *c, int ch)
{
#ifdef DISALLOW_DRUM_BENDS
    if(ISDRUMCHANNEL(ch))
	return 0; /* Not supported drum channel chorus */
#endif
    if(c->opt_chorus_control == 1)
	return c->channel[ch].chorus_level;
    return -c->opt_chorus_control;
}

#ifndef USE_DSP_EFFECT
static void make_rvid(struct timiditycontext_t *c)
{
    int i, j, lv, maxrv;

    for(maxrv = MAX_CHANNELS - 1; maxrv >= 0; maxrv--)
    {
	if(c->channel[maxrv].reverb_level == -1)
	    c->channel[maxrv].reverb_id = -1;
	else if(c->channel[maxrv].reverb_level >= 0)
	    break;
    }

    /* collect same reverb level. */
    for(i = 0; i <= maxrv; i++)
    {
	if((lv = c->channel[i].reverb_level) == -1)
	{
	    c->channel[i].reverb_id = -1;
	    continue;
	}
	c->channel[i].reverb_id = i;
	for(j = 0; j < i; j++)
	{
	    if(c->channel[j].reverb_level == lv)
	    {
		c->channel[i].reverb_id = j;
		break;
	    }
	}
    }
}
#endif /* !USE_DSP_EFFECT */

void free_drum_effect(struct timiditycontext_t *c, int ch)
{
	int i;
	if (c->channel[ch].drum_effect != NULL) {
		for (i = 0; i < c->channel[ch].drum_effect_num; i++) {
			if (c->channel[ch].drum_effect[i].buf != NULL) {
				free(c->channel[ch].drum_effect[i].buf);
				c->channel[ch].drum_effect[i].buf = NULL;
			}
		}
		free(c->channel[ch].drum_effect);
		c->channel[ch].drum_effect = NULL;
	}
	c->channel[ch].drum_effect_num = 0;
	c->channel[ch].drum_effect_flag = 0;
}

static void make_drum_effect(struct timiditycontext_t *c, int ch)
{
	int i, note, num = 0;
	int8 note_table[128];
	struct DrumParts *drum;
	struct DrumPartEffect *de;

	if (c->channel[ch].drum_effect_flag == 0) {
		free_drum_effect(c, ch);
		memset(note_table, 0, sizeof(int8) * 128);

		for(i = 0; i < 128; i++) {
			if ((drum = c->channel[ch].drums[i]) != NULL)
			{
				if (drum->reverb_level != -1
				|| drum->chorus_level != -1 || drum->delay_level != -1) {
					note_table[num++] = i;
				}
			}
		}

		c->channel[ch].drum_effect = (struct DrumPartEffect *)safe_malloc(sizeof(struct DrumPartEffect) * num);

		for(i = 0; i < num; i++) {
			de = &(c->channel[ch].drum_effect[i]);
			de->note = note = note_table[i];
			drum = c->channel[ch].drums[note];
			de->reverb_send = (int32)drum->reverb_level * (int32)get_reverb_level(c, ch) / 127;
			de->chorus_send = (int32)drum->chorus_level * (int32)c->channel[ch].chorus_level / 127;
			de->delay_send = (int32)drum->delay_level * (int32)c->channel[ch].delay_level / 127;
			de->buf = (int32 *)safe_malloc(AUDIO_BUFFER_SIZE * 8);
			memset(de->buf, 0, AUDIO_BUFFER_SIZE * 8);
		}

		c->channel[ch].drum_effect_num = num;
		c->channel[ch].drum_effect_flag = 1;
	}
}

static void adjust_master_volume(struct timiditycontext_t *c)
{
  int i, uv = c->upper_voices;
  adjust_amplification(c);
  for(i = 0; i < uv; i++)
      if(c->voice[i].status & (VOICE_ON | VOICE_SUSTAINED))
      {
	  recompute_amp(c, i);
	  apply_envelope_to_amp(c, i);
      }
}

int midi_drumpart_change(struct timiditycontext_t *c, int ch, int isdrum)
{
	if (IS_SET_CHANNELMASK(c->drumchannel_mask, ch))
		return 0;
	if (isdrum) {
		SET_CHANNELMASK(c->drumchannels, ch);
		SET_CHANNELMASK(c->current_file_info->drumchannels, ch);
	} else {
		UNSET_CHANNELMASK(c->drumchannels, ch);
		UNSET_CHANNELMASK(c->current_file_info->drumchannels, ch);
	}
	return 1;
}

void midi_program_change(struct timiditycontext_t *c, int ch, int prog)
{
	int dr = ISDRUMCHANNEL(ch);
	int newbank, b, p, map;

	switch (c->play_system_mode) {
	case GS_SYSTEM_MODE:	/* GS */
		if ((map = c->channel[ch].bank_lsb) == 0) {
			map = c->channel[ch].tone_map0_number;
		}
		switch (map) {
		case 0:		/* No change */
			break;
		case 1:
			c->channel[ch].mapID = (dr) ? SC_55_DRUM_MAP : SC_55_TONE_MAP;
			break;
		case 2:
			c->channel[ch].mapID = (dr) ? SC_88_DRUM_MAP : SC_88_TONE_MAP;
			break;
		case 3:
			c->channel[ch].mapID = (dr) ? SC_88PRO_DRUM_MAP : SC_88PRO_TONE_MAP;
			break;
		case 4:
			c->channel[ch].mapID = (dr) ? SC_8850_DRUM_MAP : SC_8850_TONE_MAP;
			break;
		default:
			break;
		}
		newbank = c->channel[ch].bank_msb;
		break;
	case XG_SYSTEM_MODE:	/* XG */
		switch (c->channel[ch].bank_msb) {
		case 0:		/* Normal */
#if 0
			if (ch == 9 && c->channel[ch].bank_lsb == 127
					&& c->channel[ch].mapID == XG_DRUM_MAP)
				/* FIXME: Why this part is drum?  Is this correct? */
				break;
#endif
/* Eric's explanation for the FIXME (March 2004):
 *
 * I don't have the original email from my archived inbox, but I found a
 * reply I made in my archived sent-mail from 1999.  A September 5th message
 * to Masanao Izumo is discussing a problem with a "reapxg.mid", a file which
 * I still have, and how it issues an MSB=0 with a program change on ch 9,
 * thus turning it into a melodic channel.  The strange thing is, this doesn't
 * happen on XG hardware, nor on the XG softsynth.  It continues to play as a
 * normal drum.  The author of the midi file obviously intended it to be
 * drumset 16 too.  The original fix was to detect LSB == -1, then break so
 * as to not set it to a melodic channel.  I'm guessing that this somehow got
 * mutated into checking for 127 instead, and the current FIXME is related to
 * the original hack from Sept 1999.  The Sept 5th email discusses patches
 * being applied to version 2.5.1 to get XG drums to work properly, and a
 * Sept 7th email to someone else discusses the fixes being part of the
 * latest 2.6.0-beta3.  A September 23rd email to Masanao Izumo specifically
 * mentions the LSB == -1 hack (and reapxg.mid not playing "correctly"
 * anymore), as well as new changes in 2.6.0 that broke a lot of other XG
 * files (XG drum support was extremely buggy in 1999 and we were still trying
 * to figure out how to initialize things to reproduce hardware behavior).  An
 * October 5th email says that 2.5.1 was correct, 2.6.0 had very broken XG
 * drum changes, and 2.6.1 still has problems.  Further discussions ensued
 * over what was "correct": to follow the XG spec, or to reproduce
 * "features" / bugs in the hardware.  I can't find the rest of the
 * discussions, but I think it ended with us agreeing to just follow the spec
 * and not try to reproduce the hardware strangeness.  I don't know how the
 * current FIXME wound up the way it is now.  I'm still going to guess it is
 * related to the old reapxg.mid hack.
 *
 * Now that reset_midi() initializes channel[ch].bank_lsb to 0 instead of -1,
 * checking for LSB == -1 won't do anything anymore, so changing the above
 * FIXME to the original == -1 won't do any good.  It is best to just #if 0
 * it out and leave it here as a reminder that there is at least one XG
 * hardware / softsynth "bug" that is not reproduced by timidity at the
 * moment.
 *
 * If the current FIXME actually reproduces some other XG hadware bug that
 * I don't know about, then it may have a valid purpose.  I just don't know
 * what that purpose is at the moment.  Perhaps someone else does?  I still
 * have src going back to 2.10.4, and the FIXME comment was already there by
 * then.  I don't see any entries in the Changelog that could explain it
 * either.  If someone has src from 2.5.1 through 2.10.3 and wants to
 * investigate this further, go for it :)
 */
			midi_drumpart_change(c, ch, 0);
			c->channel[ch].mapID = XG_NORMAL_MAP;
			dr = ISDRUMCHANNEL(ch);
			break;
		case 64:	/* SFX voice */
			midi_drumpart_change(c, ch, 0);
			c->channel[ch].mapID = XG_SFX64_MAP;
			dr = ISDRUMCHANNEL(ch);
			break;
		case 126:	/* SFX kit */
			midi_drumpart_change(c, ch, 1);
			c->channel[ch].mapID = XG_SFX126_MAP;
			dr = ISDRUMCHANNEL(ch);
			break;
		case 127:	/* Drum kit */
			midi_drumpart_change(c, ch, 1);
			c->channel[ch].mapID = XG_DRUM_MAP;
			dr = ISDRUMCHANNEL(ch);
			break;
		default:
			break;
		}
		newbank = c->channel[ch].bank_lsb;
		break;
	case GM2_SYSTEM_MODE:	/* GM2 */
		if ((c->channel[ch].bank_msb & 0xfe) == 0x78) {	/* 0x78/0x79 */
			midi_drumpart_change(c, ch, c->channel[ch].bank_msb == 0x78);
			dr = ISDRUMCHANNEL(ch);
		}
		c->channel[ch].mapID = (dr) ? GM2_DRUM_MAP : GM2_TONE_MAP;
		newbank = c->channel[ch].bank_lsb;
		break;
	default:
		newbank = c->channel[ch].bank_msb;
		break;
	}
	if (dr) {
		c->channel[ch].bank = prog;	/* newbank is ignored */
		c->channel[ch].program = prog;
		if (c->drumset[prog] == NULL || c->drumset[prog]->alt == NULL)
			c->channel[ch].altassign = c->drumset[0]->alt;
		else
			c->channel[ch].altassign = c->drumset[prog]->alt;
		ctl_mode_event(c, CTLE_DRUMPART, 1, ch, 1);
	} else {
		c->channel[ch].bank = (c->special_tonebank >= 0)
				? c->special_tonebank : newbank;
		c->channel[ch].program = (c->default_program[ch] == SPECIAL_PROGRAM)
				? SPECIAL_PROGRAM : prog;
		c->channel[ch].altassign = NULL;
		ctl_mode_event(c, CTLE_DRUMPART, 1, ch, 0);
		if (c->opt_realtime_playing && (play_mode->flag & PF_PCM_STREAM)) {
			b = c->channel[ch].bank, p = prog;
			instrument_map(c, c->channel[ch].mapID, &b, &p);
			play_midi_load_instrument(c, 0, b, p);
		}
	}
}

static int16 conv_lfo_pitch_depth(float val)
{
	return (int16)(0.0318f * val * val + 0.6858f * val + 0.5f);
}

static int16 conv_lfo_filter_depth(float val)
{
	return (int16)((0.0318f * val * val + 0.6858f * val) * 4.0f + 0.5f);
}

/*! process system exclusive sent from parse_sysex_event_multi(). */
static void process_sysex_event(struct timiditycontext_t *c, int ev, int ch, int val, int b)
{
	int temp, msb, note;

	if (ch >= MAX_CHANNELS)
		return;
	if (ev == ME_SYSEX_MSB) {
		c->channel[ch].sysex_msb_addr = b;
		c->channel[ch].sysex_msb_val = val;
	} else if(ev == ME_SYSEX_GS_MSB) {
		c->channel[ch].sysex_gs_msb_addr = b;
		c->channel[ch].sysex_gs_msb_val = val;
	} else if(ev == ME_SYSEX_XG_MSB) {
		c->channel[ch].sysex_xg_msb_addr = b;
		c->channel[ch].sysex_xg_msb_val = val;
	} else if(ev == ME_SYSEX_LSB) {	/* Universal system exclusive message */
		msb = c->channel[ch].sysex_msb_addr;
		note = c->channel[ch].sysex_msb_val;
		c->channel[ch].sysex_msb_addr = c->channel[ch].sysex_msb_val = 0;
		switch(b)
		{
		case 0x00:	/* CAf Pitch Control */
			if(val > 0x58) {val = 0x58;}
			else if(val < 0x28) {val = 0x28;}
			c->channel[ch].caf.pitch = val - 64;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CAf Pitch Control (CH:%d %d semitones)", ch, c->channel[ch].caf.pitch);
			break;
		case 0x01:	/* CAf Filter Cutoff Control */
			c->channel[ch].caf.cutoff = (val - 64) * 150;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CAf Filter Cutoff Control (CH:%d %d cents)", ch, c->channel[ch].caf.cutoff);
			break;
		case 0x02:	/* CAf Amplitude Control */
			c->channel[ch].caf.amp = (float)val / 64.0f - 1.0f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CAf Amplitude Control (CH:%d %.2f)", ch, c->channel[ch].caf.amp);
			break;
		case 0x03:	/* CAf LFO1 Rate Control */
			c->channel[ch].caf.lfo1_rate = (float)(val - 64) / 6.4f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CAf LFO1 Rate Control (CH:%d %.1f Hz)", ch, c->channel[ch].caf.lfo1_rate);
			break;
		case 0x04:	/* CAf LFO1 Pitch Depth */
			c->channel[ch].caf.lfo1_pitch_depth = conv_lfo_pitch_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CAf LFO1 Pitch Depth (CH:%d %d cents)", ch, c->channel[ch].caf.lfo1_pitch_depth);
			break;
		case 0x05:	/* CAf LFO1 Filter Depth */
			c->channel[ch].caf.lfo1_tvf_depth = conv_lfo_filter_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CAf LFO1 Filter Depth (CH:%d %d cents)", ch, c->channel[ch].caf.lfo1_tvf_depth);
			break;
		case 0x06:	/* CAf LFO1 Amplitude Depth */
			c->channel[ch].caf.lfo1_tva_depth = (float)val / 127.0f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CAf LFO1 Amplitude Depth (CH:%d %.2f)", ch, c->channel[ch].caf.lfo1_tva_depth);
			break;
		case 0x07:	/* CAf LFO2 Rate Control */
			c->channel[ch].caf.lfo2_rate = (float)(val - 64) / 6.4f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CAf LFO2 Rate Control (CH:%d %.1f Hz)", ch, c->channel[ch].caf.lfo2_rate);
			break;
		case 0x08:	/* CAf LFO2 Pitch Depth */
			c->channel[ch].caf.lfo2_pitch_depth = conv_lfo_pitch_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CAf LFO2 Pitch Depth (CH:%d %d cents)", ch, c->channel[ch].caf.lfo2_pitch_depth);
			break;
		case 0x09:	/* CAf LFO2 Filter Depth */
			c->channel[ch].caf.lfo2_tvf_depth = conv_lfo_filter_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CAf LFO2 Filter Depth (CH:%d %d cents)", ch, c->channel[ch].caf.lfo2_tvf_depth);
			break;
		case 0x0A:	/* CAf LFO2 Amplitude Depth */
			c->channel[ch].caf.lfo2_tva_depth = (float)val / 127.0f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CAf LFO2 Amplitude Depth (CH:%d %.2f)", ch, c->channel[ch].caf.lfo2_tva_depth);
			break;
		case 0x0B:	/* PAf Pitch Control */
			if(val > 0x58) {val = 0x58;}
			else if(val < 0x28) {val = 0x28;}
			c->channel[ch].paf.pitch = val - 64;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "PAf Pitch Control (CH:%d %d semitones)", ch, c->channel[ch].paf.pitch);
			break;
		case 0x0C:	/* PAf Filter Cutoff Control */
			c->channel[ch].paf.cutoff = (val - 64) * 150;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "PAf Filter Cutoff Control (CH:%d %d cents)", ch, c->channel[ch].paf.cutoff);
			break;
		case 0x0D:	/* PAf Amplitude Control */
			c->channel[ch].paf.amp = (float)val / 64.0f - 1.0f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "PAf Amplitude Control (CH:%d %.2f)", ch, c->channel[ch].paf.amp);
			break;
		case 0x0E:	/* PAf LFO1 Rate Control */
			c->channel[ch].paf.lfo1_rate = (float)(val - 64) / 6.4f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "PAf LFO1 Rate Control (CH:%d %.1f Hz)", ch, c->channel[ch].paf.lfo1_rate);
			break;
		case 0x0F:	/* PAf LFO1 Pitch Depth */
			c->channel[ch].paf.lfo1_pitch_depth = conv_lfo_pitch_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "PAf LFO1 Pitch Depth (CH:%d %d cents)", ch, c->channel[ch].paf.lfo1_pitch_depth);
			break;
		case 0x10:	/* PAf LFO1 Filter Depth */
			c->channel[ch].paf.lfo1_tvf_depth = conv_lfo_filter_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "PAf LFO1 Filter Depth (CH:%d %d cents)", ch, c->channel[ch].paf.lfo1_tvf_depth);
			break;
		case 0x11:	/* PAf LFO1 Amplitude Depth */
			c->channel[ch].paf.lfo1_tva_depth = (float)val / 127.0f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "PAf LFO1 Amplitude Depth (CH:%d %.2f)", ch, c->channel[ch].paf.lfo1_tva_depth);
			break;
		case 0x12:	/* PAf LFO2 Rate Control */
			c->channel[ch].paf.lfo2_rate = (float)(val - 64) / 6.4f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "PAf LFO2 Rate Control (CH:%d %.1f Hz)", ch, c->channel[ch].paf.lfo2_rate);
			break;
		case 0x13:	/* PAf LFO2 Pitch Depth */
			c->channel[ch].paf.lfo2_pitch_depth = conv_lfo_pitch_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "PAf LFO2 Pitch Depth (CH:%d %d cents)", ch, c->channel[ch].paf.lfo2_pitch_depth);
			break;
		case 0x14:	/* PAf LFO2 Filter Depth */
			c->channel[ch].paf.lfo2_tvf_depth = conv_lfo_filter_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "PAf LFO2 Filter Depth (CH:%d %d cents)", ch, c->channel[ch].paf.lfo2_tvf_depth);
			break;
		case 0x15:	/* PAf LFO2 Amplitude Depth */
			c->channel[ch].paf.lfo2_tva_depth = (float)val / 127.0f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "PAf LFO2 Amplitude Depth (CH:%d %.2f)", ch, c->channel[ch].paf.lfo2_tva_depth);
			break;
		case 0x16:	/* MOD Pitch Control */
			if(val > 0x58) {val = 0x58;}
			else if(val < 0x28) {val = 0x28;}
			c->channel[ch].mod.pitch = val - 64;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "MOD Pitch Control (CH:%d %d semitones)", ch, c->channel[ch].mod.pitch);
			break;
		case 0x17:	/* MOD Filter Cutoff Control */
			c->channel[ch].mod.cutoff = (val - 64) * 150;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "MOD Filter Cutoff Control (CH:%d %d cents)", ch, c->channel[ch].mod.cutoff);
			break;
		case 0x18:	/* MOD Amplitude Control */
			c->channel[ch].mod.amp = (float)val / 64.0f - 1.0f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "MOD Amplitude Control (CH:%d %.2f)", ch, c->channel[ch].mod.amp);
			break;
		case 0x19:	/* MOD LFO1 Rate Control */
			c->channel[ch].mod.lfo1_rate = (float)(val - 64) / 6.4f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "MOD LFO1 Rate Control (CH:%d %.1f Hz)", ch, c->channel[ch].mod.lfo1_rate);
			break;
		case 0x1A:	/* MOD LFO1 Pitch Depth */
			c->channel[ch].mod.lfo1_pitch_depth = conv_lfo_pitch_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "MOD LFO1 Pitch Depth (CH:%d %d cents)", ch, c->channel[ch].mod.lfo1_pitch_depth);
			break;
		case 0x1B:	/* MOD LFO1 Filter Depth */
			c->channel[ch].mod.lfo1_tvf_depth = conv_lfo_filter_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "MOD LFO1 Filter Depth (CH:%d %d cents)", ch, c->channel[ch].mod.lfo1_tvf_depth);
			break;
		case 0x1C:	/* MOD LFO1 Amplitude Depth */
			c->channel[ch].mod.lfo1_tva_depth = (float)val / 127.0f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "MOD LFO1 Amplitude Depth (CH:%d %.2f)", ch, c->channel[ch].mod.lfo1_tva_depth);
			break;
		case 0x1D:	/* MOD LFO2 Rate Control */
			c->channel[ch].mod.lfo2_rate = (float)(val - 64) / 6.4f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "MOD LFO2 Rate Control (CH:%d %.1f Hz)", ch, c->channel[ch].mod.lfo2_rate);
			break;
		case 0x1E:	/* MOD LFO2 Pitch Depth */
			c->channel[ch].mod.lfo2_pitch_depth = conv_lfo_pitch_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "MOD LFO2 Pitch Depth (CH:%d %d cents)", ch, c->channel[ch].mod.lfo2_pitch_depth);
			break;
		case 0x1F:	/* MOD LFO2 Filter Depth */
			c->channel[ch].mod.lfo2_tvf_depth = conv_lfo_filter_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "MOD LFO2 Filter Depth (CH:%d %d cents)", ch, c->channel[ch].mod.lfo2_tvf_depth);
			break;
		case 0x20:	/* MOD LFO2 Amplitude Depth */
			c->channel[ch].mod.lfo2_tva_depth = (float)val / 127.0f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "MOD LFO2 Amplitude Depth (CH:%d %.2f)", ch, c->channel[ch].mod.lfo2_tva_depth);
			break;
		case 0x21:	/* BEND Pitch Control */
			if(val > 0x58) {val = 0x58;}
			else if(val < 0x28) {val = 0x28;}
			c->channel[ch].bend.pitch = val - 64;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "BEND Pitch Control (CH:%d %d semitones)", ch, c->channel[ch].bend.pitch);
			break;
		case 0x22:	/* BEND Filter Cutoff Control */
			c->channel[ch].bend.cutoff = (val - 64) * 150;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "BEND Filter Cutoff Control (CH:%d %d cents)", ch, c->channel[ch].bend.cutoff);
			break;
		case 0x23:	/* BEND Amplitude Control */
			c->channel[ch].bend.amp = (float)val / 64.0f - 1.0f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "BEND Amplitude Control (CH:%d %.2f)", ch, c->channel[ch].bend.amp);
			break;
		case 0x24:	/* BEND LFO1 Rate Control */
			c->channel[ch].bend.lfo1_rate = (float)(val - 64) / 6.4f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "BEND LFO1 Rate Control (CH:%d %.1f Hz)", ch, c->channel[ch].bend.lfo1_rate);
			break;
		case 0x25:	/* BEND LFO1 Pitch Depth */
			c->channel[ch].bend.lfo1_pitch_depth = conv_lfo_pitch_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "BEND LFO1 Pitch Depth (CH:%d %d cents)", ch, c->channel[ch].bend.lfo1_pitch_depth);
			break;
		case 0x26:	/* BEND LFO1 Filter Depth */
			c->channel[ch].bend.lfo1_tvf_depth = conv_lfo_filter_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "BEND LFO1 Filter Depth (CH:%d %d cents)", ch, c->channel[ch].bend.lfo1_tvf_depth);
			break;
		case 0x27:	/* BEND LFO1 Amplitude Depth */
			c->channel[ch].bend.lfo1_tva_depth = (float)val / 127.0f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "BEND LFO1 Amplitude Depth (CH:%d %.2f)", ch, c->channel[ch].bend.lfo1_tva_depth);
			break;
		case 0x28:	/* BEND LFO2 Rate Control */
			c->channel[ch].bend.lfo2_rate = (float)(val - 64) / 6.4f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "BEND LFO2 Rate Control (CH:%d %.1f Hz)", ch, c->channel[ch].bend.lfo2_rate);
			break;
		case 0x29:	/* BEND LFO2 Pitch Depth */
			c->channel[ch].bend.lfo2_pitch_depth = conv_lfo_pitch_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "BEND LFO2 Pitch Depth (CH:%d %d cents)", ch, c->channel[ch].bend.lfo2_pitch_depth);
			break;
		case 0x2A:	/* BEND LFO2 Filter Depth */
			c->channel[ch].bend.lfo2_tvf_depth = conv_lfo_filter_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "BEND LFO2 Filter Depth (CH:%d %d cents)", ch, c->channel[ch].bend.lfo2_tvf_depth);
			break;
		case 0x2B:	/* BEND LFO2 Amplitude Depth */
			c->channel[ch].bend.lfo2_tva_depth = (float)val / 127.0f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "BEND LFO2 Amplitude Depth (CH:%d %.2f)", ch, c->channel[ch].bend.lfo2_tva_depth);
			break;
		case 0x2C:	/* CC1 Pitch Control */
			if(val > 0x58) {val = 0x58;}
			else if(val < 0x28) {val = 0x28;}
			c->channel[ch].cc1.pitch = val - 64;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC1 Pitch Control (CH:%d %d semitones)", ch, c->channel[ch].cc1.pitch);
			break;
		case 0x2D:	/* CC1 Filter Cutoff Control */
			c->channel[ch].cc1.cutoff = (val - 64) * 150;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC1 Filter Cutoff Control (CH:%d %d cents)", ch, c->channel[ch].cc1.cutoff);
			break;
		case 0x2E:	/* CC1 Amplitude Control */
			c->channel[ch].cc1.amp = (float)val / 64.0f - 1.0f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC1 Amplitude Control (CH:%d %.2f)", ch, c->channel[ch].cc1.amp);
			break;
		case 0x2F:	/* CC1 LFO1 Rate Control */
			c->channel[ch].cc1.lfo1_rate = (float)(val - 64) / 6.4f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC1 LFO1 Rate Control (CH:%d %.1f Hz)", ch, c->channel[ch].cc1.lfo1_rate);
			break;
		case 0x30:	/* CC1 LFO1 Pitch Depth */
			c->channel[ch].cc1.lfo1_pitch_depth = conv_lfo_pitch_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC1 LFO1 Pitch Depth (CH:%d %d cents)", ch, c->channel[ch].cc1.lfo1_pitch_depth);
			break;
		case 0x31:	/* CC1 LFO1 Filter Depth */
			c->channel[ch].cc1.lfo1_tvf_depth = conv_lfo_filter_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC1 LFO1 Filter Depth (CH:%d %d cents)", ch, c->channel[ch].cc1.lfo1_tvf_depth);
			break;
		case 0x32:	/* CC1 LFO1 Amplitude Depth */
			c->channel[ch].cc1.lfo1_tva_depth = (float)val / 127.0f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC1 LFO1 Amplitude Depth (CH:%d %.2f)", ch, c->channel[ch].cc1.lfo1_tva_depth);
			break;
		case 0x33:	/* CC1 LFO2 Rate Control */
			c->channel[ch].cc1.lfo2_rate = (float)(val - 64) / 6.4f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC1 LFO2 Rate Control (CH:%d %.1f Hz)", ch, c->channel[ch].cc1.lfo2_rate);
			break;
		case 0x34:	/* CC1 LFO2 Pitch Depth */
			c->channel[ch].cc1.lfo2_pitch_depth = conv_lfo_pitch_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC1 LFO2 Pitch Depth (CH:%d %d cents)", ch, c->channel[ch].cc1.lfo2_pitch_depth);
			break;
		case 0x35:	/* CC1 LFO2 Filter Depth */
			c->channel[ch].cc1.lfo2_tvf_depth = conv_lfo_filter_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC1 LFO2 Filter Depth (CH:%d %d cents)", ch, c->channel[ch].cc1.lfo2_tvf_depth);
			break;
		case 0x36:	/* CC1 LFO2 Amplitude Depth */
			c->channel[ch].cc1.lfo2_tva_depth = (float)val / 127.0f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC1 LFO2 Amplitude Depth (CH:%d %.2f)", ch, c->channel[ch].cc1.lfo2_tva_depth);
			break;
		case 0x37:	/* CC2 Pitch Control */
			if(val > 0x58) {val = 0x58;}
			else if(val < 0x28) {val = 0x28;}
			c->channel[ch].cc2.pitch = val - 64;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC2 Pitch Control (CH:%d %d semitones)", ch, c->channel[ch].cc2.pitch);
			break;
		case 0x38:	/* CC2 Filter Cutoff Control */
			c->channel[ch].cc2.cutoff = (val - 64) * 150;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC2 Filter Cutoff Control (CH:%d %d cents)", ch, c->channel[ch].cc2.cutoff);
			break;
		case 0x39:	/* CC2 Amplitude Control */
			c->channel[ch].cc2.amp = (float)val / 64.0f - 1.0f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC2 Amplitude Control (CH:%d %.2f)", ch, c->channel[ch].cc2.amp);
			break;
		case 0x3A:	/* CC2 LFO1 Rate Control */
			c->channel[ch].cc2.lfo1_rate = (float)(val - 64) / 6.4f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC2 LFO1 Rate Control (CH:%d %.1f Hz)", ch, c->channel[ch].cc2.lfo1_rate);
			break;
		case 0x3B:	/* CC2 LFO1 Pitch Depth */
			c->channel[ch].cc2.lfo1_pitch_depth = conv_lfo_pitch_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC2 LFO1 Pitch Depth (CH:%d %d cents)", ch, c->channel[ch].cc2.lfo1_pitch_depth);
			break;
		case 0x3C:	/* CC2 LFO1 Filter Depth */
			c->channel[ch].cc2.lfo1_tvf_depth = conv_lfo_filter_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC2 LFO1 Filter Depth (CH:%d %d cents)", ch, c->channel[ch].cc2.lfo1_tvf_depth);
			break;
		case 0x3D:	/* CC2 LFO1 Amplitude Depth */
			c->channel[ch].cc2.lfo1_tva_depth = (float)val / 127.0f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC2 LFO1 Amplitude Depth (CH:%d %.2f)", ch, c->channel[ch].cc2.lfo1_tva_depth);
			break;
		case 0x3E:	/* CC2 LFO2 Rate Control */
			c->channel[ch].cc2.lfo2_rate = (float)(val - 64) / 6.4f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC2 LFO2 Rate Control (CH:%d %.1f Hz)", ch, c->channel[ch].cc2.lfo2_rate);
			break;
		case 0x3F:	/* CC2 LFO2 Pitch Depth */
			c->channel[ch].cc2.lfo2_pitch_depth = conv_lfo_pitch_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC2 LFO2 Pitch Depth (CH:%d %d cents)", ch, c->channel[ch].cc2.lfo2_pitch_depth);
			break;
		case 0x40:	/* CC2 LFO2 Filter Depth */
			c->channel[ch].cc2.lfo2_tvf_depth = conv_lfo_filter_depth(val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC2 LFO2 Filter Depth (CH:%d %d cents)", ch, c->channel[ch].cc2.lfo2_tvf_depth);
			break;
		case 0x41:	/* CC2 LFO2 Amplitude Depth */
			c->channel[ch].cc2.lfo2_tva_depth = (float)val / 127.0f;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CC2 LFO2 Amplitude Depth (CH:%d %.2f)", ch, c->channel[ch].cc2.lfo2_tva_depth);
			break;
		case 0x42:	/* Note Limit Low */
			c->channel[ch].note_limit_low = val;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Note Limit Low (CH:%d VAL:%d)", ch, val);
			break;
		case 0x43:	/* Note Limit High */
			c->channel[ch].note_limit_high = val;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Note Limit High (CH:%d VAL:%d)", ch, val);
			break;
		case 0x44:	/* Velocity Limit Low */
			c->channel[ch].vel_limit_low = val;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Velocity Limit Low (CH:%d VAL:%d)", ch, val);
			break;
		case 0x45:	/* Velocity Limit High */
			c->channel[ch].vel_limit_high = val;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Velocity Limit High (CH:%d VAL:%d)", ch, val);
			break;
		case 0x46:	/* Rx. Note Off */
			if (c->channel[ch].drums[note] == NULL)
				play_midi_setup_drums(c, ch, note);
			set_rx_drum(c->channel[ch].drums[note], RX_NOTE_OFF, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY,
				"Drum Instrument Rx. Note Off (CH:%d NOTE:%d VAL:%d)",
				ch, note, val);
			break;
		case 0x47:	/* Rx. Note On */
			if (c->channel[ch].drums[note] == NULL)
				play_midi_setup_drums(c, ch, note);
			set_rx_drum(c->channel[ch].drums[note], RX_NOTE_ON, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY,
				"Drum Instrument Rx. Note On (CH:%d NOTE:%d VAL:%d)",
				ch, note, val);
			break;
		case 0x48:	/* Rx. Pitch Bend */
			set_rx(c, ch, RX_PITCH_BEND, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Rx. Pitch Bend (CH:%d VAL:%d)", ch, val);
			break;
		case 0x49:	/* Rx. Channel Pressure */
			set_rx(c, ch, RX_CH_PRESSURE, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Rx. Channel Pressure (CH:%d VAL:%d)", ch, val);
			break;
		case 0x4A:	/* Rx. Program Change */
			set_rx(c, ch, RX_PROGRAM_CHANGE, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Rx. Program Change (CH:%d VAL:%d)", ch, val);
			break;
		case 0x4B:	/* Rx. Control Change */
			set_rx(c, ch, RX_CONTROL_CHANGE, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Rx. Control Change (CH:%d VAL:%d)", ch, val);
			break;
		case 0x4C:	/* Rx. Poly Pressure */
			set_rx(c, ch, RX_POLY_PRESSURE, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Rx. Poly Pressure (CH:%d VAL:%d)", ch, val);
			break;
		case 0x4D:	/* Rx. Note Message */
			set_rx(c, ch, RX_NOTE_MESSAGE, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Rx. Note Message (CH:%d VAL:%d)", ch, val);
			break;
		case 0x4E:	/* Rx. RPN */
			set_rx(c, ch, RX_RPN, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Rx. RPN (CH:%d VAL:%d)", ch, val);
			break;
		case 0x4F:	/* Rx. NRPN */
			set_rx(c, ch, RX_NRPN, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Rx. NRPN (CH:%d VAL:%d)", ch, val);
			break;
		case 0x50:	/* Rx. Modulation */
			set_rx(c, ch, RX_MODULATION, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Rx. Modulation (CH:%d VAL:%d)", ch, val);
			break;
		case 0x51:	/* Rx. Volume */
			set_rx(c, ch, RX_VOLUME, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Rx. Volume (CH:%d VAL:%d)", ch, val);
			break;
		case 0x52:	/* Rx. Panpot */
			set_rx(c, ch, RX_PANPOT, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Rx. Panpot (CH:%d VAL:%d)", ch, val);
			break;
		case 0x53:	/* Rx. Expression */
			set_rx(c, ch, RX_EXPRESSION, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Rx. Expression (CH:%d VAL:%d)", ch, val);
			break;
		case 0x54:	/* Rx. Hold1 */
			set_rx(c, ch, RX_HOLD1, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Rx. Hold1 (CH:%d VAL:%d)", ch, val);
			break;
		case 0x55:	/* Rx. Portamento */
			set_rx(c, ch, RX_PORTAMENTO, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Rx. Portamento (CH:%d VAL:%d)", ch, val);
			break;
		case 0x56:	/* Rx. Sostenuto */
			set_rx(c, ch, RX_SOSTENUTO, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Rx. Sostenuto (CH:%d VAL:%d)", ch, val);
			break;
		case 0x57:	/* Rx. Soft */
			set_rx(c, ch, RX_SOFT, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Rx. Soft (CH:%d VAL:%d)", ch, val);
			break;
		case 0x58:	/* Rx. Bank Select */
			set_rx(c, ch, RX_BANK_SELECT, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Rx. Bank Select (CH:%d VAL:%d)", ch, val);
			break;
		case 0x59:	/* Rx. Bank Select LSB */
			set_rx(c, ch, RX_BANK_SELECT_LSB, val);
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Rx. Bank Select LSB (CH:%d VAL:%d)", ch, val);
			break;
		case 0x60:	/* Reverb Type (GM2) */
			if (val > 8) {val = 8;}
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Reverb Type (%d)", val);
			set_reverb_macro_gm2(c, val);
			recompute_reverb_status_gs(c);
			init_reverb(c);
			break;
		case 0x61:	/* Chorus Type (GM2) */
			if (val > 5) {val = 5;}
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Chorus Type (%d)", val);
			set_chorus_macro_gs(c, val);
			recompute_chorus_status_gs(c);
			init_ch_chorus(c);
			break;
		default:
			break;
		}
		return;
	} else if(ev == ME_SYSEX_GS_LSB) {	/* GS system exclusive message */
		msb = c->channel[ch].sysex_gs_msb_addr;
		note = c->channel[ch].sysex_gs_msb_val;
		c->channel[ch].sysex_gs_msb_addr = c->channel[ch].sysex_gs_msb_val = 0;
		switch(b)
		{
		case 0x00:	/* EQ ON/OFF */
			if(!c->opt_eq_control) {break;}
			if(c->channel[ch].eq_gs != val) {
				if(val) {
					ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ ON (CH:%d)",ch);
				} else {
					ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ OFF (CH:%d)",ch);
				}
			}
			c->channel[ch].eq_gs = val;
			break;
		case 0x01:	/* EQ LOW FREQ */
			if(!c->opt_eq_control) {break;}
			c->eq_status_gs.low_freq = val;
			recompute_eq_status_gs(c);
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ LOW FREQ (%d)",val);
			break;
		case 0x02:	/* EQ LOW GAIN */
			if(!c->opt_eq_control) {break;}
			c->eq_status_gs.low_gain = val;
			recompute_eq_status_gs(c);
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ LOW GAIN (%d dB)",val - 0x40);
			break;
		case 0x03:	/* EQ HIGH FREQ */
			if(!c->opt_eq_control) {break;}
			c->eq_status_gs.high_freq = val;
			recompute_eq_status_gs(c);
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ HIGH FREQ (%d)",val);
			break;
		case 0x04:	/* EQ HIGH GAIN */
			if(!c->opt_eq_control) {break;}
			c->eq_status_gs.high_gain = val;
			recompute_eq_status_gs(c);
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ HIGH GAIN (%d dB)",val - 0x40);
			break;
		case 0x05:	/* Reverb Macro */
			if (val > 7) {val = 7;}
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Reverb Macro (%d)",val);
			set_reverb_macro_gs(c, val);
			recompute_reverb_status_gs(c);
			init_reverb(c);
			break;
		case 0x06:	/* Reverb Character */
			if (val > 7) {val = 7;}
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Reverb Character (%d)",val);
			if (c->reverb_status_gs.character != val) {
				c->reverb_status_gs.character = val;
				recompute_reverb_status_gs(c);
				init_reverb(c);
			}
			break;
		case 0x07:	/* Reverb Pre-LPF */
			if (val > 7) {val = 7;}
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Reverb Pre-LPF (%d)",val);
			if(c->reverb_status_gs.pre_lpf != val) {
				c->reverb_status_gs.pre_lpf = val;
				recompute_reverb_status_gs(c);
			}
			break;
		case 0x08:	/* Reverb Level */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Reverb Level (%d)",val);
			if(c->reverb_status_gs.level != val) {
				c->reverb_status_gs.level = val;
				recompute_reverb_status_gs(c);
				init_reverb(c);
			}
			break;
		case 0x09:	/* Reverb Time */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Reverb Time (%d)",val);
			if(c->reverb_status_gs.time != val) {
				c->reverb_status_gs.time = val;
				recompute_reverb_status_gs(c);
				init_reverb(c);
			}
			break;
		case 0x0A:	/* Reverb Delay Feedback */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Reverb Delay Feedback (%d)",val);
			if(c->reverb_status_gs.delay_feedback != val) {
				c->reverb_status_gs.delay_feedback = val;
				recompute_reverb_status_gs(c);
				init_reverb(c);
			}
			break;
		case 0x0C:	/* Reverb Predelay Time */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Reverb Predelay Time (%d)",val);
			if(c->reverb_status_gs.pre_delay_time != val) {
				c->reverb_status_gs.pre_delay_time = val;
				recompute_reverb_status_gs(c);
				init_reverb(c);
			}
			break;
		case 0x0D:	/* Chorus Macro */
			if (val > 7) {val = 7;}
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Chorus Macro (%d)",val);
			set_chorus_macro_gs(c, val);
			recompute_chorus_status_gs(c);
			init_ch_chorus(c);
			break;
		case 0x0E:	/* Chorus Pre-LPF */
			if (val > 7) {val = 7;}
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Chorus Pre-LPF (%d)",val);
			if (c->chorus_status_gs.pre_lpf != val) {
				c->chorus_status_gs.pre_lpf = val;
				recompute_chorus_status_gs(c);
			}
			break;
		case 0x0F:	/* Chorus Level */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Chorus Level (%d)",val);
			if (c->chorus_status_gs.level != val) {
				c->chorus_status_gs.level = val;
				recompute_chorus_status_gs(c);
				init_ch_chorus(c);
			}
			break;
		case 0x10:	/* Chorus Feedback */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Chorus Feedback (%d)",val);
			if (c->chorus_status_gs.feedback != val) {
				c->chorus_status_gs.feedback = val;
				recompute_chorus_status_gs(c);
				init_ch_chorus(c);
			}
			break;
		case 0x11:	/* Chorus Delay */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Chorus Delay (%d)",val);
			if (c->chorus_status_gs.delay != val) {
				c->chorus_status_gs.delay = val;
				recompute_chorus_status_gs(c);
				init_ch_chorus(c);
			}
			break;
		case 0x12:	/* Chorus Rate */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Chorus Rate (%d)",val);
			if (c->chorus_status_gs.rate != val) {
				c->chorus_status_gs.rate = val;
				recompute_chorus_status_gs(c);
				init_ch_chorus(c);
			}
			break;
		case 0x13:	/* Chorus Depth */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Chorus Depth (%d)",val);
			if (c->chorus_status_gs.depth != val) {
				c->chorus_status_gs.depth = val;
				recompute_chorus_status_gs(c);
				init_ch_chorus(c);
			}
			break;
		case 0x14:	/* Chorus Send Level to Reverb */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Chorus Send Level to Reverb (%d)",val);
			if (c->chorus_status_gs.send_reverb != val) {
				c->chorus_status_gs.send_reverb = val;
				recompute_chorus_status_gs(c);
				init_ch_chorus(c);
			}
			break;
		case 0x15:	/* Chorus Send Level to Delay */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Chorus Send Level to Delay (%d)",val);
			if (c->chorus_status_gs.send_delay != val) {
				c->chorus_status_gs.send_delay = val;
				recompute_chorus_status_gs(c);
				init_ch_chorus(c);
			}
			break;
		case 0x16:	/* Delay Macro */
			if (val > 7) {val = 7;}
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Delay Macro (%d)",val);
			set_delay_macro_gs(c, val);
			recompute_delay_status_gs(c);
			init_ch_delay(c);
			break;
		case 0x17:	/* Delay Pre-LPF */
			if (val > 7) {val = 7;}
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Delay Pre-LPF (%d)",val);
			val &= 0x7;
			if (c->delay_status_gs.pre_lpf != val) {
				c->delay_status_gs.pre_lpf = val;
				recompute_delay_status_gs(c);
			}
			break;
		case 0x18:	/* Delay Time Center */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Delay Time Center (%d)",val);
			if (c->delay_status_gs.time_c != val) {
				c->delay_status_gs.time_c = val;
				recompute_delay_status_gs(c);
				init_ch_delay(c);
			}
			break;
		case 0x19:	/* Delay Time Ratio Left */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Delay Time Ratio Left (%d)",val);
			if (val == 0) {val = 1;}
			if (c->delay_status_gs.time_l != val) {
				c->delay_status_gs.time_l = val;
				recompute_delay_status_gs(c);
				init_ch_delay(c);
			}
			break;
		case 0x1A:	/* Delay Time Ratio Right */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Delay Time Ratio Right (%d)",val);
			if (val == 0) {val = 1;}
			if (c->delay_status_gs.time_r != val) {
				c->delay_status_gs.time_r = val;
				recompute_delay_status_gs(c);
				init_ch_delay(c);
			}
			break;
		case 0x1B:	/* Delay Level Center */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Delay Level Center (%d)",val);
			if (c->delay_status_gs.level_center != val) {
				c->delay_status_gs.level_center = val;
				recompute_delay_status_gs(c);
				init_ch_delay(c);
			}
			break;
		case 0x1C:	/* Delay Level Left */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Delay Level Left (%d)",val);
			if (c->delay_status_gs.level_left != val) {
				c->delay_status_gs.level_left = val;
				recompute_delay_status_gs(c);
				init_ch_delay(c);
			}
			break;
		case 0x1D:	/* Delay Level Right */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Delay Level Right (%d)",val);
			if (c->delay_status_gs.level_right != val) {
				c->delay_status_gs.level_right = val;
				recompute_delay_status_gs(c);
				init_ch_delay(c);
			}
			break;
		case 0x1E:	/* Delay Level */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Delay Level (%d)",val);
			if (c->delay_status_gs.level != val) {
				c->delay_status_gs.level = val;
				recompute_delay_status_gs(c);
				init_ch_delay(c);
			}
			break;
		case 0x1F:	/* Delay Feedback */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Delay Feedback (%d)",val);
			if (c->delay_status_gs.feedback != val) {
				c->delay_status_gs.feedback = val;
				recompute_delay_status_gs(c);
				init_ch_delay(c);
			}
			break;
		case 0x20:	/* Delay Send Level to Reverb */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Delay Send Level to Reverb (%d)",val);
			if (c->delay_status_gs.send_reverb != val) {
				c->delay_status_gs.send_reverb = val;
				recompute_delay_status_gs(c);
				init_ch_delay(c);
			}
			break;
		case 0x21:	/* Velocity Sense Depth */
			c->channel[ch].velocity_sense_depth = val;
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Velocity Sense Depth (CH:%d VAL:%d)",ch,val);
			break;
		case 0x22:	/* Velocity Sense Offset */
			c->channel[ch].velocity_sense_offset = val;
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Velocity Sense Offset (CH:%d VAL:%d)",ch,val);
			break;
		case 0x23:	/* Insertion Effect ON/OFF */
			if(!c->opt_insertion_effect) {break;}
			if(c->channel[ch].insertion_effect != val) {
				if(val) {ctl->cmsg(CMSG_INFO,VERB_NOISY,"EFX ON (CH:%d)",ch);}
				else {ctl->cmsg(CMSG_INFO,VERB_NOISY,"EFX OFF (CH:%d)",ch);}
			}
			c->channel[ch].insertion_effect = val;
			break;
		case 0x24:	/* Assign Mode */
			c->channel[ch].assign_mode = val;
			if(val == 0) {
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"Assign Mode: Single (CH:%d)",ch);
			} else if(val == 1) {
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"Assign Mode: Limited-Multi (CH:%d)",ch);
			} else if(val == 2) {
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"Assign Mode: Full-Multi (CH:%d)",ch);
			}
			break;
		case 0x25:	/* TONE MAP-0 NUMBER */
			c->channel[ch].tone_map0_number = val;
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Tone Map-0 Number (CH:%d VAL:%d)",ch,val);
			break;
		case 0x26:	/* Pitch Offset Fine */
			c->channel[ch].pitch_offset_fine = (FLOAT_T)((((int32)val << 4) | (int32)val) - 0x80) / 10.0;
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Pitch Offset Fine (CH:%d %3fHz)",ch,c->channel[ch].pitch_offset_fine);
			break;
		case 0x27:	/* Insertion Effect Parameter */
			if(!c->opt_insertion_effect) {break;}
			temp = c->insertion_effect_gs.type;
			c->insertion_effect_gs.type_msb = val;
			c->insertion_effect_gs.type = ((int32)c->insertion_effect_gs.type_msb << 8) | (int32)c->insertion_effect_gs.type_lsb;
			if(temp == c->insertion_effect_gs.type) {
				recompute_insertion_effect_gs(c);
			} else {
				realloc_insertion_effect_gs(c);
			}
			break;
		case 0x28:	/* Insertion Effect Parameter */
			if(!c->opt_insertion_effect) {break;}
			temp = c->insertion_effect_gs.type;
			c->insertion_effect_gs.type_lsb = val;
			c->insertion_effect_gs.type = ((int32)c->insertion_effect_gs.type_msb << 8) | (int32)c->insertion_effect_gs.type_lsb;
			if(temp == c->insertion_effect_gs.type) {
				recompute_insertion_effect_gs(c);
			} else {
				ctl->cmsg(CMSG_INFO, VERB_NOISY, "EFX TYPE (%02X %02X)", c->insertion_effect_gs.type_msb, c->insertion_effect_gs.type_lsb);
				realloc_insertion_effect_gs(c);
			}
			break;
		case 0x29:
			c->insertion_effect_gs.parameter[0] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x2A:
			c->insertion_effect_gs.parameter[1] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x2B:
			c->insertion_effect_gs.parameter[2] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x2C:
			c->insertion_effect_gs.parameter[3] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x2D:
			c->insertion_effect_gs.parameter[4] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x2E:
			c->insertion_effect_gs.parameter[5] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x2F:
			c->insertion_effect_gs.parameter[6] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x30:
			c->insertion_effect_gs.parameter[7] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x31:
			c->insertion_effect_gs.parameter[8] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x32:
			c->insertion_effect_gs.parameter[9] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x33:
			c->insertion_effect_gs.parameter[10] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x34:
			c->insertion_effect_gs.parameter[11] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x35:
			c->insertion_effect_gs.parameter[12] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x36:
			c->insertion_effect_gs.parameter[13] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x37:
			c->insertion_effect_gs.parameter[14] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x38:
			c->insertion_effect_gs.parameter[15] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x39:
			c->insertion_effect_gs.parameter[16] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x3A:
			c->insertion_effect_gs.parameter[17] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x3B:
			c->insertion_effect_gs.parameter[18] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x3C:
			c->insertion_effect_gs.parameter[19] = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x3D:
			c->insertion_effect_gs.send_reverb = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x3E:
			c->insertion_effect_gs.send_chorus = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x3F:
			c->insertion_effect_gs.send_delay = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x40:
			c->insertion_effect_gs.control_source1 = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x41:
			c->insertion_effect_gs.control_depth1 = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x42:
			c->insertion_effect_gs.control_source2 = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x43:
			c->insertion_effect_gs.control_depth2 = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x44:
			c->insertion_effect_gs.send_eq_switch = val;
			recompute_insertion_effect_gs(c);
			break;
		case 0x45:	/* Rx. Channel */
			reset_controllers(c, ch);
			redraw_controllers(c, ch);
			all_notes_off(c, ch);
			if (val == 0x80)
				remove_channel_layer(c, ch);
			else
				add_channel_layer(c, ch, val);
			break;
		case 0x46:	/* Channel Msg Rx Port */
			reset_controllers(c, ch);
			redraw_controllers(c, ch);
			all_notes_off(c, ch);
			c->channel[ch].port_select = val;
			break;
		case 0x47:	/* Play Note Number */
			if (c->channel[ch].drums[note] == NULL)
				play_midi_setup_drums(c, ch, note);
			c->channel[ch].drums[note]->play_note = val;
			ctl->cmsg(CMSG_INFO, VERB_NOISY,
				"Drum Instrument Play Note (CH:%d NOTE:%d VAL:%d)",
				ch, note, c->channel[ch].drums[note]->play_note);
			c->channel[ch].pitchfactor = 0;
			break;
		default:
			break;
		}
		return;
	} else if(ev == ME_SYSEX_XG_LSB) {	/* XG system exclusive message */
		msb = c->channel[ch].sysex_xg_msb_addr;
		note = c->channel[ch].sysex_xg_msb_val;
		if (note == 3 && msb == 0) {	/* Effect 2 */
		note = 0;	/* force insertion effect num 0 ?? */
		if (note >= XG_INSERTION_EFFECT_NUM || note < 0) {return;}
		switch(b)
		{
		case 0x00:	/* Insertion Effect Type MSB */
			if (c->insertion_effect_xg[note].type_msb != val) {
				ctl->cmsg(CMSG_INFO, VERB_NOISY, "Insertion Effect Type MSB (%d %02X)", note, val);
				c->insertion_effect_xg[note].type_msb = val;
				realloc_effect_xg(c, &c->insertion_effect_xg[note]);
			}
			break;
		case 0x01:	/* Insertion Effect Type LSB */
			if (c->insertion_effect_xg[note].type_lsb != val) {
				ctl->cmsg(CMSG_INFO, VERB_NOISY, "Insertion Effect Type LSB (%d %02X)", note, val);
				c->insertion_effect_xg[note].type_lsb = val;
				realloc_effect_xg(c, &c->insertion_effect_xg[note]);
			}
			break;
		case 0x02:	/* Insertion Effect Parameter 1 - 10 */
		case 0x03:
		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07:
		case 0x08:
		case 0x09:
		case 0x0A:
		case 0x0B:
			if (c->insertion_effect_xg[note].use_msb) {break;}
			temp = b - 0x02;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Insertion Effect Parameter %d (%d %d)", temp + 1, note, val);
			if (c->insertion_effect_xg[note].param_lsb[temp] != val) {
				c->insertion_effect_xg[note].param_lsb[temp] = val;
				recompute_effect_xg(c, &c->insertion_effect_xg[note]);
			}
			break;
		case 0x0C:	/* Insertion Effect Part */
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Insertion Effect Part (%d %d)", note, val);
			if (c->insertion_effect_xg[note].part != val) {
				c->insertion_effect_xg[note].part = val;
				recompute_effect_xg(c, &c->insertion_effect_xg[note]);
			}
			break;
		case 0x0D:	/* MW Insertion Control Depth */
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "MW Insertion Control Depth (%d %d)", note, val);
			if (c->insertion_effect_xg[note].mw_depth != val) {
				c->insertion_effect_xg[note].mw_depth = val;
				recompute_effect_xg(c, &c->insertion_effect_xg[note]);
			}
			break;
		case 0x0E:	/* BEND Insertion Control Depth */
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "BEND Insertion Control Depth (%d %d)", note, val);
			if (c->insertion_effect_xg[note].bend_depth != val) {
				c->insertion_effect_xg[note].bend_depth = val;
				recompute_effect_xg(c, &c->insertion_effect_xg[note]);
			}
			break;
		case 0x0F:	/* CAT Insertion Control Depth */
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CAT Insertion Control Depth (%d %d)", note, val);
			if (c->insertion_effect_xg[note].cat_depth != val) {
				c->insertion_effect_xg[note].cat_depth = val;
				recompute_effect_xg(c, &c->insertion_effect_xg[note]);
			}
			break;
		case 0x10:	/* AC1 Insertion Control Depth */
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "AC1 Insertion Control Depth (%d %d)", note, val);
			if (c->insertion_effect_xg[note].ac1_depth != val) {
				c->insertion_effect_xg[note].ac1_depth = val;
				recompute_effect_xg(c, &c->insertion_effect_xg[note]);
			}
			break;
		case 0x11:	/* AC2 Insertion Control Depth */
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "AC2 Insertion Control Depth (%d %d)", note, val);
			if (c->insertion_effect_xg[note].ac2_depth != val) {
				c->insertion_effect_xg[note].ac2_depth = val;
				recompute_effect_xg(c, &c->insertion_effect_xg[note]);
			}
			break;
		case 0x12:	/* CBC1 Insertion Control Depth */
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CBC1 Insertion Control Depth (%d %d)", note, val);
			if (c->insertion_effect_xg[note].cbc1_depth != val) {
				c->insertion_effect_xg[note].cbc1_depth = val;
				recompute_effect_xg(c, &c->insertion_effect_xg[note]);
			}
			break;
		case 0x13:	/* CBC2 Insertion Control Depth */
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CBC2 Insertion Control Depth (%d %d)", note, val);
			if (c->insertion_effect_xg[note].cbc2_depth != val) {
				c->insertion_effect_xg[note].cbc2_depth = val;
				recompute_effect_xg(c, &c->insertion_effect_xg[note]);
			}
			break;
		case 0x20:	/* Insertion Effect Parameter 11 - 16 */
		case 0x21:
		case 0x22:
		case 0x23:
		case 0x24:
		case 0x25:
			temp = b - 0x20 + 10;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Insertion Effect Parameter %d (%d %d)", temp + 1, note, val);
			if (c->insertion_effect_xg[note].param_lsb[temp] != val) {
				c->insertion_effect_xg[note].param_lsb[temp] = val;
				recompute_effect_xg(c, &c->insertion_effect_xg[note]);
			}
			break;
		case 0x30:	/* Insertion Effect Parameter 1 - 10 MSB */
		case 0x32:
		case 0x34:
		case 0x36:
		case 0x38:
		case 0x3A:
		case 0x3C:
		case 0x3E:
		case 0x40:
		case 0x42:
			if (!c->insertion_effect_xg[note].use_msb) {break;}
			temp = (b - 0x30) / 2;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Insertion Effect Parameter %d MSB (%d %d)", temp + 1, note, val);
			if (c->insertion_effect_xg[note].param_msb[temp] != val) {
				c->insertion_effect_xg[note].param_msb[temp] = val;
				recompute_effect_xg(c, &c->insertion_effect_xg[note]);
			}
			break;
		case 0x31:	/* Insertion Effect Parameter 1 - 10 LSB */
		case 0x33:
		case 0x35:
		case 0x37:
		case 0x39:
		case 0x3B:
		case 0x3D:
		case 0x3F:
		case 0x41:
		case 0x43:
			if (!c->insertion_effect_xg[note].use_msb) {break;}
			temp = (b - 0x31) / 2;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Insertion Effect Parameter %d LSB (%d %d)", temp + 1, note, val);
			if (c->insertion_effect_xg[note].param_lsb[temp] != val) {
				c->insertion_effect_xg[note].param_lsb[temp] = val;
				recompute_effect_xg(c, &c->insertion_effect_xg[note]);
			}
			break;
		default:
			break;
		}
		} else if (note == 2 && msb == 1) {	/* Effect 1 */
		note = 0;	/* force variation effect num 0 ?? */
		switch(b)
		{
		case 0x00:	/* Reverb Type MSB */
			if (c->reverb_status_xg.type_msb != val) {
				ctl->cmsg(CMSG_INFO, VERB_NOISY, "Reverb Type MSB (%02X)", val);
				c->reverb_status_xg.type_msb = val;
				realloc_effect_xg(c, &c->reverb_status_xg);
			}
			break;
		case 0x01:	/* Reverb Type LSB */
			if (c->reverb_status_xg.type_lsb != val) {
				ctl->cmsg(CMSG_INFO, VERB_NOISY, "Reverb Type LSB (%02X)", val);
				c->reverb_status_xg.type_lsb = val;
				realloc_effect_xg(c, &c->reverb_status_xg);
			}
			break;
		case 0x02:	/* Reverb Parameter 1 - 10 */
		case 0x03:
		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07:
		case 0x08:
		case 0x09:
		case 0x0A:
		case 0x0B:
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Reverb Parameter %d (%d)", b - 0x02 + 1, val);
			if (c->reverb_status_xg.param_lsb[b - 0x02] != val) {
				c->reverb_status_xg.param_lsb[b - 0x02] = val;
				recompute_effect_xg(c, &c->reverb_status_xg);
			}
			break;
		case 0x0C:	/* Reverb Return */
#if 0	/* XG specific reverb is not currently implemented */
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Reverb Return (%d)", val);
			if (c->reverb_status_xg.ret != val) {
				c->reverb_status_xg.ret = val;
				recompute_effect_xg(c, &c->reverb_status_xg);
			}
#else	/* use GS reverb instead */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Reverb Return (%d)", val);
			if (c->reverb_status_gs.level != val) {
				c->reverb_status_gs.level = val;
				recompute_reverb_status_gs(c);
				init_reverb(c);
			}
#endif
			break;
		case 0x0D:	/* Reverb Pan */
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Reverb Pan (%d)", val);
			if (c->reverb_status_xg.pan != val) {
				c->reverb_status_xg.pan = val;
				recompute_effect_xg(c, &c->reverb_status_xg);
			}
			break;
		case 0x10:	/* Reverb Parameter 11 - 16 */
		case 0x11:
		case 0x12:
		case 0x13:
		case 0x14:
		case 0x15:
			temp = b - 0x10 + 10;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Reverb Parameter %d (%d)", temp + 1, val);
			if (c->reverb_status_xg.param_lsb[temp] != val) {
				c->reverb_status_xg.param_lsb[temp] = val;
				recompute_effect_xg(c, &c->reverb_status_xg);
			}
			break;
		case 0x20:	/* Chorus Type MSB */
			if (c->chorus_status_xg.type_msb != val) {
				ctl->cmsg(CMSG_INFO, VERB_NOISY, "Chorus Type MSB (%02X)", val);
				c->chorus_status_xg.type_msb = val;
				realloc_effect_xg(c, &c->chorus_status_xg);
			}
			break;
		case 0x21:	/* Chorus Type LSB */
			if (c->chorus_status_xg.type_lsb != val) {
				ctl->cmsg(CMSG_INFO, VERB_NOISY, "Chorus Type LSB (%02X)", val);
				c->chorus_status_xg.type_lsb = val;
				realloc_effect_xg(c, &c->chorus_status_xg);
			}
			break;
		case 0x22:	/* Chorus Parameter 1 - 10 */
		case 0x23:
		case 0x24:
		case 0x25:
		case 0x26:
		case 0x27:
		case 0x28:
		case 0x29:
		case 0x2A:
		case 0x2B:
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Chorus Parameter %d (%d)", b - 0x22 + 1, val);
			if (c->chorus_status_xg.param_lsb[b - 0x22] != val) {
				c->chorus_status_xg.param_lsb[b - 0x22] = val;
				recompute_effect_xg(c, &c->chorus_status_xg);
			}
			break;
		case 0x2C:	/* Chorus Return */
#if 0	/* XG specific chorus is not currently implemented */
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Chorus Return (%d)", val);
			if (c->chorus_status_xg.ret != val) {
				c->chorus_status_xg.ret = val;
				recompute_effect_xg(c, &c->chorus_status_xg);
			}
#else	/* use GS chorus instead */
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Chorus Return (%d)", val);
			if (c->chorus_status_gs.level != val) {
				c->chorus_status_gs.level = val;
				recompute_chorus_status_gs(c);
				init_ch_chorus(c);
			}
#endif
			break;
		case 0x2D:	/* Chorus Pan */
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Chorus Pan (%d)", val);
			if (c->chorus_status_xg.pan != val) {
				c->chorus_status_xg.pan = val;
				recompute_effect_xg(c, &c->chorus_status_xg);
			}
			break;
		case 0x2E:	/* Send Chorus To Reverb */
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Send Chorus To Reverb (%d)", val);
			if (c->chorus_status_xg.send_reverb != val) {
				c->chorus_status_xg.send_reverb = val;
				recompute_effect_xg(c, &c->chorus_status_xg);
			}
			break;
		case 0x30:	/* Chorus Parameter 11 - 16 */
		case 0x31:
		case 0x32:
		case 0x33:
		case 0x34:
		case 0x35:
			temp = b - 0x30 + 10;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Chorus Parameter %d (%d)", temp + 1, val);
			if (c->chorus_status_xg.param_lsb[temp] != val) {
				c->chorus_status_xg.param_lsb[temp] = val;
				recompute_effect_xg(c, &c->chorus_status_xg);
			}
			break;
		case 0x40:	/* Variation Type MSB */
			if (note >= XG_VARIATION_EFFECT_NUM || note < 0) {break;}
			if (c->variation_effect_xg[note].type_msb != val) {
				ctl->cmsg(CMSG_INFO, VERB_NOISY, "Variation Type MSB (%02X)", val);
				c->variation_effect_xg[note].type_msb = val;
				realloc_effect_xg(c, &c->variation_effect_xg[note]);
			}
			break;
		case 0x41:	/* Variation Type LSB */
			if (note >= XG_VARIATION_EFFECT_NUM || note < 0) {break;}
			if (c->variation_effect_xg[note].type_lsb != val) {
				ctl->cmsg(CMSG_INFO, VERB_NOISY, "Variation Type LSB (%02X)", val);
				c->variation_effect_xg[note].type_lsb = val;
				realloc_effect_xg(c, &c->variation_effect_xg[note]);
			}
			break;
		case 0x42:	/* Variation Parameter 1 - 10 MSB */
		case 0x44:
		case 0x46:
		case 0x48:
		case 0x4A:
		case 0x4C:
		case 0x4E:
		case 0x50:
		case 0x52:
		case 0x54:
			if (note >= XG_VARIATION_EFFECT_NUM || note < 0) {break;}
			temp = (b - 0x42) / 2;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Variation Parameter %d MSB (%d)", temp, val);
			if (c->variation_effect_xg[note].param_msb[temp] != val) {
				c->variation_effect_xg[note].param_msb[temp] = val;
				recompute_effect_xg(c, &c->variation_effect_xg[note]);
			}
			break;
		case 0x43:	/* Variation Parameter 1 - 10 LSB */
		case 0x45:
		case 0x47:
		case 0x49:
		case 0x4B:
		case 0x4D:
		case 0x4F:
		case 0x51:
		case 0x53:
		case 0x55:
			if (note >= XG_VARIATION_EFFECT_NUM || note < 0) {break;}
			temp = (b - 0x43) / 2;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Variation Parameter %d LSB (%d)", temp, val);
			if (c->variation_effect_xg[note].param_lsb[temp] != val) {
				c->variation_effect_xg[note].param_lsb[temp] = val;
				recompute_effect_xg(c, &c->variation_effect_xg[note]);
			}
			break;
		case 0x56:	/* Variation Return */
			if (note >= XG_VARIATION_EFFECT_NUM || note < 0) {break;}
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Variation Return (%d)", val);
			if (c->variation_effect_xg[note].ret != val) {
				c->variation_effect_xg[note].ret = val;
				recompute_effect_xg(c, &c->variation_effect_xg[note]);
			}
			break;
		case 0x57:	/* Variation Pan */
			if (note >= XG_VARIATION_EFFECT_NUM || note < 0) {break;}
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Variation Pan (%d)", val);
			if (c->variation_effect_xg[note].pan != val) {
				c->variation_effect_xg[note].pan = val;
				recompute_effect_xg(c, &c->variation_effect_xg[note]);
			}
			break;
		case 0x58:	/* Send Variation To Reverb */
			if (note >= XG_VARIATION_EFFECT_NUM || note < 0) {break;}
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Send Variation To Reverb (%d)", val);
			if (c->variation_effect_xg[note].send_reverb != val) {
				c->variation_effect_xg[note].send_reverb = val;
				recompute_effect_xg(c, &c->variation_effect_xg[note]);
			}
			break;
		case 0x59:	/* Send Variation To Chorus */
			if (note >= XG_VARIATION_EFFECT_NUM || note < 0) {break;}
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Send Variation To Chorus (%d)", val);
			if (c->variation_effect_xg[note].send_chorus != val) {
				c->variation_effect_xg[note].send_chorus = val;
				recompute_effect_xg(c, &c->variation_effect_xg[note]);
			}
			break;
		case 0x5A:	/* Variation Connection */
			if (note >= XG_VARIATION_EFFECT_NUM || note < 0) {break;}
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Variation Connection (%d)", val);
			if (c->variation_effect_xg[note].connection != val) {
				c->variation_effect_xg[note].connection = val;
				recompute_effect_xg(c, &c->variation_effect_xg[note]);
			}
			break;
		case 0x5B:	/* Variation Part */
			if (note >= XG_VARIATION_EFFECT_NUM || note < 0) {break;}
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Variation Part (%d)", val);
			if (c->variation_effect_xg[note].part != val) {
				c->variation_effect_xg[note].part = val;
				recompute_effect_xg(c, &c->variation_effect_xg[note]);
			}
			break;
		case 0x5C:	/* MW Variation Control Depth */
			if (note >= XG_VARIATION_EFFECT_NUM || note < 0) {break;}
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "MW Variation Control Depth (%d)", val);
			if (c->variation_effect_xg[note].mw_depth != val) {
				c->variation_effect_xg[note].mw_depth = val;
				recompute_effect_xg(c, &c->variation_effect_xg[note]);
			}
			break;
		case 0x5D:	/* BEND Variation Control Depth */
			if (note >= XG_VARIATION_EFFECT_NUM || note < 0) {break;}
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "BEND Variation Control Depth (%d)", val);
			if (c->variation_effect_xg[note].bend_depth != val) {
				c->variation_effect_xg[note].bend_depth = val;
				recompute_effect_xg(c, &c->variation_effect_xg[note]);
			}
			break;
		case 0x5E:	/* CAT Variation Control Depth */
			if (note >= XG_VARIATION_EFFECT_NUM || note < 0) {break;}
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CAT Variation Control Depth (%d)", val);
			if (c->variation_effect_xg[note].cat_depth != val) {
				c->variation_effect_xg[note].cat_depth = val;
				recompute_effect_xg(c, &c->variation_effect_xg[note]);
			}
			break;
		case 0x5F:	/* AC1 Variation Control Depth */
			if (note >= XG_VARIATION_EFFECT_NUM || note < 0) {break;}
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "AC1 Variation Control Depth (%d)", val);
			if (c->variation_effect_xg[note].ac1_depth != val) {
				c->variation_effect_xg[note].ac1_depth = val;
				recompute_effect_xg(c, &c->variation_effect_xg[note]);
			}
			break;
		case 0x60:	/* AC2 Variation Control Depth */
			if (note >= XG_VARIATION_EFFECT_NUM || note < 0) {break;}
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "AC2 Variation Control Depth (%d)", val);
			if (c->variation_effect_xg[note].ac2_depth != val) {
				c->variation_effect_xg[note].ac2_depth = val;
				recompute_effect_xg(c, &c->variation_effect_xg[note]);
			}
			break;
		case 0x61:	/* CBC1 Variation Control Depth */
			if (note >= XG_VARIATION_EFFECT_NUM || note < 0) {break;}
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CBC1 Variation Control Depth (%d)", val);
			if (c->variation_effect_xg[note].cbc1_depth != val) {
				c->variation_effect_xg[note].cbc1_depth = val;
				recompute_effect_xg(c, &c->variation_effect_xg[note]);
			}
			break;
		case 0x62:	/* CBC2 Variation Control Depth */
			if (note >= XG_VARIATION_EFFECT_NUM || note < 0) {break;}
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "CBC2 Variation Control Depth (%d)", val);
			if (c->variation_effect_xg[note].cbc2_depth != val) {
				c->variation_effect_xg[note].cbc2_depth = val;
				recompute_effect_xg(c, &c->variation_effect_xg[note]);
			}
			break;
		case 0x70:	/* Variation Parameter 11 - 16 */
		case 0x71:
		case 0x72:
		case 0x73:
		case 0x74:
		case 0x75:
			temp = b - 0x70 + 10;
			if (note >= XG_VARIATION_EFFECT_NUM || note < 0) {break;}
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Variation Parameter %d (%d)", temp + 1, val);
			if (c->variation_effect_xg[note].param_lsb[temp] != val) {
				c->variation_effect_xg[note].param_lsb[temp] = val;
				recompute_effect_xg(c, &c->variation_effect_xg[note]);
			}
			break;
		default:
			break;
		}
		} else if (note == 2 && msb == 40) {	/* Multi EQ */
		switch(b)
		{
		case 0x00:	/* EQ type */
			if(c->opt_eq_control) {
				if(val == 0) {ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ type (0: Flat)");}
				else if(val == 1) {ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ type (1: Jazz)");}
				else if(val == 2) {ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ type (2: Pops)");}
				else if(val == 3) {ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ type (3: Rock)");}
				else if(val == 4) {ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ type (4: Concert)");}
				c->multi_eq_xg.type = val;
				set_multi_eq_type_xg(c, val);
				recompute_multi_eq_xg(c);
			}
			break;
		case 0x01:	/* EQ gain1 */
			if(c->opt_eq_control) {
				if(val > 0x4C) {val = 0x4C;}
				else if(val < 0x34) {val = 0x34;}
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ gain1 (%d dB)", val - 0x40);
				c->multi_eq_xg.gain1 = val;
				recompute_multi_eq_xg(c);
			}
			break;
		case 0x02:	/* EQ frequency1 */
			if(c->opt_eq_control) {
				if(val > 60) {val = 60;}
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ frequency1 (%d Hz)", (int32)eq_freq_table_xg[val]);
				c->multi_eq_xg.freq1 = val;
				recompute_multi_eq_xg(c);
			}
			break;
		case 0x03:	/* EQ Q1 */
			if(c->opt_eq_control) {
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ Q1 (%f)", (double)val / 10.0);
				c->multi_eq_xg.q1 = val;
				recompute_multi_eq_xg(c);
			}
			break;
		case 0x04:	/* EQ shape1 */
			if(c->opt_eq_control) {
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ shape1 (%d)", val);
				c->multi_eq_xg.shape1 = val;
				recompute_multi_eq_xg(c);
			}
			break;
		case 0x05:	/* EQ gain2 */
			if(c->opt_eq_control) {
				if(val > 0x4C) {val = 0x4C;}
				else if(val < 0x34) {val = 0x34;}
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ gain2 (%d dB)", val - 0x40);
				c->multi_eq_xg.gain2 = val;
				recompute_multi_eq_xg(c);
			}
			break;
		case 0x06:	/* EQ frequency2 */
			if(c->opt_eq_control) {
				if(val > 60) {val = 60;}
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ frequency2 (%d Hz)", (int32)eq_freq_table_xg[val]);
				c->multi_eq_xg.freq2 = val;
				recompute_multi_eq_xg(c);
			}
			break;
		case 0x07:	/* EQ Q2 */
			if(c->opt_eq_control) {
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ Q2 (%f)", (double)val / 10.0);
				c->multi_eq_xg.q2 = val;
				recompute_multi_eq_xg(c);
			}
			break;
		case 0x09:	/* EQ gain3 */
			if(c->opt_eq_control) {
				if(val > 0x4C) {val = 0x4C;}
				else if(val < 0x34) {val = 0x34;}
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ gain3 (%d dB)", val - 0x40);
				c->multi_eq_xg.gain3 = val;
				recompute_multi_eq_xg(c);
			}
			break;
		case 0x0A:	/* EQ frequency3 */
			if(c->opt_eq_control) {
				if(val > 60) {val = 60;}
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ frequency3 (%d Hz)", (int32)eq_freq_table_xg[val]);
				c->multi_eq_xg.freq3 = val;
				recompute_multi_eq_xg(c);
			}
			break;
		case 0x0B:	/* EQ Q3 */
			if(c->opt_eq_control) {
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ Q3 (%f)", (double)val / 10.0);
				c->multi_eq_xg.q3 = val;
				recompute_multi_eq_xg(c);
			}
			break;
		case 0x0D:	/* EQ gain4 */
			if(c->opt_eq_control) {
				if(val > 0x4C) {val = 0x4C;}
				else if(val < 0x34) {val = 0x34;}
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ gain4 (%d dB)", val - 0x40);
				c->multi_eq_xg.gain4 = val;
				recompute_multi_eq_xg(c);
			}
			break;
		case 0x0E:	/* EQ frequency4 */
			if(c->opt_eq_control) {
				if(val > 60) {val = 60;}
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ frequency4 (%d Hz)", (int32)eq_freq_table_xg[val]);
				c->multi_eq_xg.freq4 = val;
				recompute_multi_eq_xg(c);
			}
			break;
		case 0x0F:	/* EQ Q4 */
			if(c->opt_eq_control) {
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ Q4 (%f)", (double)val / 10.0);
				c->multi_eq_xg.q4 = val;
				recompute_multi_eq_xg(c);
			}
			break;
		case 0x11:	/* EQ gain5 */
			if(c->opt_eq_control) {
				if(val > 0x4C) {val = 0x4C;}
				else if(val < 0x34) {val = 0x34;}
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ gain5 (%d dB)", val - 0x40);
				c->multi_eq_xg.gain5 = val;
				recompute_multi_eq_xg(c);
			}
			break;
		case 0x12:	/* EQ frequency5 */
			if(c->opt_eq_control) {
				if(val > 60) {val = 60;}
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ frequency5 (%d Hz)", (int32)eq_freq_table_xg[val]);
				c->multi_eq_xg.freq5 = val;
				recompute_multi_eq_xg(c);
			}
			break;
		case 0x13:	/* EQ Q5 */
			if(c->opt_eq_control) {
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ Q5 (%f)", (double)val / 10.0);
				c->multi_eq_xg.q5 = val;
				recompute_multi_eq_xg(c);
			}
			break;
		case 0x14:	/* EQ shape5 */
			if(c->opt_eq_control) {
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ shape5 (%d)", val);
				c->multi_eq_xg.shape5 = val;
				recompute_multi_eq_xg(c);
			}
			break;
		}
		} else if (note == 8 && msb == 0) {	/* Multi Part */
		switch(b)
		{
		case 0x99:	/* Rcv CHANNEL, remapped from 0x04 */
			reset_controllers(c, ch);
			redraw_controllers(c, ch);
			all_notes_off(c, ch);
			if (val == 0x7f)
				remove_channel_layer(c, ch);
			else {
				if((ch < REDUCE_CHANNELS) != (val < REDUCE_CHANNELS)) {
					c->channel[ch].port_select = ch < REDUCE_CHANNELS ? 1 : 0;
				}
				if((ch % REDUCE_CHANNELS) != (val % REDUCE_CHANNELS)) {
					add_channel_layer(c, ch, val);
				}
			}
			break;
		case 0x06:	/* Same Note Number Key On Assign */
			if(val == 0) {
				c->channel[ch].assign_mode = 0;
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"Same Note Number Key On Assign: Single (CH:%d)",ch);
			} else if(val == 1) {
				c->channel[ch].assign_mode = 2;
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"Same Note Number Key On Assign: Multi (CH:%d)",ch);
			} else if(val == 2) {
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"Same Note Number Key On Assign: Inst is not supported. (CH:%d)",ch);
			}
			break;
		case 0x11:	/* Dry Level */
			c->channel[ch].dry_level = val;
			ctl->cmsg(CMSG_INFO, VERB_NOISY, "Dry Level (CH:%d VAL:%d)", ch, val);
			break;
		}
		} else if ((note & 0xF0) == 0x30) {	/* Drum Setup */
		note = msb;
		switch(b)
		{
		case 0x0E:	/* EG Decay1 */
			if (c->channel[ch].drums[note] == NULL)
				play_midi_setup_drums(c, ch, note);
			ctl->cmsg(CMSG_INFO, VERB_NOISY,
				"Drum Instrument EG Decay1 (CH:%d NOTE:%d VAL:%d)",
				ch, note, val);
			c->channel[ch].drums[note]->drum_envelope_rate[EG_DECAY1] = val;
			break;
		case 0x0F:	/* EG Decay2 */
			if (c->channel[ch].drums[note] == NULL)
				play_midi_setup_drums(c, ch, note);
			ctl->cmsg(CMSG_INFO, VERB_NOISY,
				"Drum Instrument EG Decay2 (CH:%d NOTE:%d VAL:%d)",
				ch, note, val);
			c->channel[ch].drums[note]->drum_envelope_rate[EG_DECAY2] = val;
			break;
		default:
			break;
		}
		}
		return;
	}
}

static void play_midi_prescan(struct timiditycontext_t *c, MidiEvent *ev)
{
    int i, j, k, ch, orig_ch, port_ch, offset, layered;

    if(c->opt_amp_compensation) {c->mainvolume_max = 0;}
    else {c->mainvolume_max = 0x7f;}
    c->compensation_ratio = 1.0;

    c->prescanning_flag = 1;
    change_system_mode(c, DEFAULT_SYSTEM_MODE);
    reset_midi(c, 0);
    resamp_cache_reset(c);

	while (ev->type != ME_EOT) {
#ifndef SUPPRESS_CHANNEL_LAYER
		orig_ch = ev->channel;
		layered = ! IS_SYSEX_EVENT_TYPE(ev);
		for (j = 0; j < MAX_CHANNELS; j += 16) {
			port_ch = (orig_ch + j) % MAX_CHANNELS;
			offset = port_ch & ~0xf;
			for (k = offset; k < offset + 16; k++) {
				if (! layered && (j || k != offset))
					continue;
				if (layered) {
					if (! IS_SET_CHANNELMASK(
							c->channel[k].channel_layer, port_ch)
							|| c->channel[k].port_select != (orig_ch >> 4))
						continue;
					ev->channel = k;
				}
#endif
	ch = ev->channel;

	switch(ev->type)
	{
	  case ME_NOTEON:
		note_on_prescan(c, ev);
	    break;

	  case ME_NOTEOFF:
	    resamp_cache_refer_off(c, ch, MIDI_EVENT_NOTE(ev), ev->time);
	    break;

	  case ME_PORTAMENTO_TIME_MSB:
	    c->channel[ch].portamento_time_msb = ev->a;
	    break;

	  case ME_PORTAMENTO_TIME_LSB:
	    c->channel[ch].portamento_time_lsb = ev->a;
	    break;

	  case ME_PORTAMENTO:
	    c->channel[ch].portamento = (ev->a >= 64);

	  case ME_RESET_CONTROLLERS:
	    reset_controllers(c, ch);
	    resamp_cache_refer_alloff(c, ch, ev->time);
	    break;

	  case ME_PROGRAM:
	    midi_program_change(c, ch, ev->a);
	    break;

	  case ME_TONE_BANK_MSB:
	    c->channel[ch].bank_msb = ev->a;
	    break;

	  case ME_TONE_BANK_LSB:
	    c->channel[ch].bank_lsb = ev->a;
	    break;

	  case ME_RESET:
	    change_system_mode(c, ev->a);
	    reset_midi(c, 0);
	    break;

	  case ME_MASTER_TUNING:
	  case ME_PITCHWHEEL:
	  case ME_ALL_NOTES_OFF:
	  case ME_ALL_SOUNDS_OFF:
	  case ME_MONO:
	  case ME_POLY:
	    resamp_cache_refer_alloff(c, ch, ev->time);
	    break;

	  case ME_DRUMPART:
	    if(midi_drumpart_change(c, ch, ev->a))
		midi_program_change(c, ch, c->channel[ch].program);
	    break;

	  case ME_KEYSHIFT:
	    resamp_cache_refer_alloff(c, ch, ev->time);
	    c->channel[ch].key_shift = (int)ev->a - 0x40;
	    break;

	  case ME_SCALE_TUNING:
		resamp_cache_refer_alloff(c, ch, ev->time);
		c->channel[ch].scale_tuning[ev->a] = ev->b;
		break;

	  case ME_MAINVOLUME:
	    if (ev->a > c->mainvolume_max) {
	      c->mainvolume_max = ev->a;
	      ctl->cmsg(CMSG_INFO,VERB_DEBUG,"ME_MAINVOLUME/max (CH:%d VAL:%#x)",ev->channel,ev->a);
	    }
	    break;
	}
#ifndef SUPPRESS_CHANNEL_LAYER
			}
		}
		ev->channel = orig_ch;
#endif
	ev++;
    }

    /* calculate compensation ratio */
    if (0 < c->mainvolume_max && c->mainvolume_max < 0x7f) {
      c->compensation_ratio = pow((double)0x7f/(double)c->mainvolume_max, 4);
      ctl->cmsg(CMSG_INFO,VERB_DEBUG,"Compensation ratio:%lf",c->compensation_ratio);
    }

    for(i = 0; i < MAX_CHANNELS; i++)
	resamp_cache_refer_alloff(c, i, ev->time);
    resamp_cache_create(c);
    c->prescanning_flag = 0;
}

/*! convert GS NRPN to vibrato rate ratio. */
/* from 0 to 3.0. */
static double gs_cnv_vib_rate(int rate)
{
	double ratio;

	if(rate == 0) {
		ratio = 1.6 / 100.0;
	} else if(rate == 64) {
		ratio = 1.0;
	} else if(rate <= 100) {
		ratio = (double)rate * 1.6 / 100.0;
	} else {
		ratio = (double)(rate - 101) * 1.33 / 26.0 + 1.67;
	}
	return (1.0 / ratio);
}

/*! convert GS NRPN to vibrato depth. */
/* from -9.6 cents to +9.45 cents. */
static int32 gs_cnv_vib_depth(int depth)
{
	double cent;
	cent = (double)(depth - 64) * 0.15;

	return (int32)(cent * 256.0 / 400.0);
}

/*! convert GS NRPN to vibrato delay. */
/* from 0 ms to 5074 ms. */
static int32 gs_cnv_vib_delay(int delay)
{
	double ms;
	ms = 0.2092 * exp(0.0795 * (double)delay);
	if(delay == 0) {ms = 0;}
	return (int32)((double)play_mode->rate * ms * 0.001);
}

static int last_rpn_addr(struct timiditycontext_t *c, int ch)
{
	int lsb, msb, addr, i;
	const struct rpn_tag_map_t *addrmap;
	struct rpn_tag_map_t {
		int addr, mask, tag;
	};
	static const struct rpn_tag_map_t nrpn_addr_map[] = {
		{0x0108, 0xffff, NRPN_ADDR_0108},
		{0x0109, 0xffff, NRPN_ADDR_0109},
		{0x010a, 0xffff, NRPN_ADDR_010A},
		{0x0120, 0xffff, NRPN_ADDR_0120},
		{0x0121, 0xffff, NRPN_ADDR_0121},
		{0x0130, 0xffff, NRPN_ADDR_0130},
		{0x0131, 0xffff, NRPN_ADDR_0131},
		{0x0134, 0xffff, NRPN_ADDR_0134},
		{0x0135, 0xffff, NRPN_ADDR_0135},
		{0x0163, 0xffff, NRPN_ADDR_0163},
		{0x0164, 0xffff, NRPN_ADDR_0164},
		{0x0166, 0xffff, NRPN_ADDR_0166},
		{0x1400, 0xff00, NRPN_ADDR_1400},
		{0x1500, 0xff00, NRPN_ADDR_1500},
		{0x1600, 0xff00, NRPN_ADDR_1600},
		{0x1700, 0xff00, NRPN_ADDR_1700},
		{0x1800, 0xff00, NRPN_ADDR_1800},
		{0x1900, 0xff00, NRPN_ADDR_1900},
		{0x1a00, 0xff00, NRPN_ADDR_1A00},
		{0x1c00, 0xff00, NRPN_ADDR_1C00},
		{0x1d00, 0xff00, NRPN_ADDR_1D00},
		{0x1e00, 0xff00, NRPN_ADDR_1E00},
		{0x1f00, 0xff00, NRPN_ADDR_1F00},
		{0x3000, 0xff00, NRPN_ADDR_3000},
		{0x3100, 0xff00, NRPN_ADDR_3100},
		{0x3400, 0xff00, NRPN_ADDR_3400},
		{0x3500, 0xff00, NRPN_ADDR_3500},
		{-1, -1, 0}
	};
	static const struct rpn_tag_map_t rpn_addr_map[] = {
		{0x0000, 0xffff, RPN_ADDR_0000},
		{0x0001, 0xffff, RPN_ADDR_0001},
		{0x0002, 0xffff, RPN_ADDR_0002},
		{0x0003, 0xffff, RPN_ADDR_0003},
		{0x0004, 0xffff, RPN_ADDR_0004},
		{0x0005, 0xffff, RPN_ADDR_0005},
		{0x7f7f, 0xffff, RPN_ADDR_7F7F},
		{0xffff, 0xffff, RPN_ADDR_FFFF},
		{-1, -1}
	};

	if (c->channel[ch].nrpn == -1)
		return -1;
	lsb = c->channel[ch].lastlrpn;
	msb = c->channel[ch].lastmrpn;
	if (lsb == 0xff || msb == 0xff)
		return -1;
	addr = (msb << 8 | lsb);
	if (c->channel[ch].nrpn)
		addrmap = nrpn_addr_map;
	else
		addrmap = rpn_addr_map;
	for (i = 0; addrmap[i].addr != -1; i++)
		if (addrmap[i].addr == (addr & addrmap[i].mask))
			return addrmap[i].tag;
	return -1;
}

static void update_rpn_map(struct timiditycontext_t *c, int ch, int addr, int update_now)
{
	int val, drumflag, i, note;

	val = c->channel[ch].rpnmap[addr];
	drumflag = 0;
	switch (addr) {
	case NRPN_ADDR_0108:	/* Vibrato Rate */
		if (c->opt_nrpn_vibrato) {
			ctl->cmsg(CMSG_INFO, VERB_NOISY,
					"Vibrato Rate (CH:%d VAL:%d)", ch, val - 64);
			c->channel[ch].vibrato_ratio = gs_cnv_vib_rate(val);
		}
		if (update_now)
			adjust_pitch(c, ch);
		break;
	case NRPN_ADDR_0109:	/* Vibrato Depth */
		if (c->opt_nrpn_vibrato) {
			ctl->cmsg(CMSG_INFO, VERB_NOISY,
					"Vibrato Depth (CH:%d VAL:%d)", ch, val - 64);
			c->channel[ch].vibrato_depth = gs_cnv_vib_depth(val);
		}
		if (update_now)
			adjust_pitch(c, ch);
		break;
	case NRPN_ADDR_010A:	/* Vibrato Delay */
		if (c->opt_nrpn_vibrato) {
			ctl->cmsg(CMSG_INFO, VERB_NOISY,
					"Vibrato Delay (CH:%d VAL:%d)", ch, val);
			c->channel[ch].vibrato_delay = gs_cnv_vib_delay(val);
		}
		if (update_now)
			adjust_pitch(c, ch);
		break;
	case NRPN_ADDR_0120:	/* Filter Cutoff Frequency */
		if (c->opt_lpf_def) {
			ctl->cmsg(CMSG_INFO, VERB_NOISY,
					"Filter Cutoff (CH:%d VAL:%d)", ch, val - 64);
			c->channel[ch].param_cutoff_freq = val - 64;
		}
		break;
	case NRPN_ADDR_0121:	/* Filter Resonance */
		if (c->opt_lpf_def) {
			ctl->cmsg(CMSG_INFO, VERB_NOISY,
					"Filter Resonance (CH:%d VAL:%d)", ch, val - 64);
			c->channel[ch].param_resonance = val - 64;
		}
		break;
	case NRPN_ADDR_0130:	/* EQ BASS */
		if (c->opt_eq_control) {
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ BASS (CH:%d %.2f dB)", ch, 0.19 * (double)(val - 0x40));
			c->channel[ch].eq_xg.bass = val;
			recompute_part_eq_xg(&(c->channel[ch].eq_xg));
		}
		break;
	case NRPN_ADDR_0131:	/* EQ TREBLE */
		if (c->opt_eq_control) {
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ TREBLE (CH:%d %.2f dB)", ch, 0.19 * (double)(val - 0x40));
			c->channel[ch].eq_xg.treble = val;
			recompute_part_eq_xg(&(c->channel[ch].eq_xg));
		}
		break;
	case NRPN_ADDR_0134:	/* EQ BASS frequency */
		if (c->opt_eq_control) {
			if(val < 4) {val = 4;}
			else if(val > 40) {val = 40;}
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ BASS frequency (CH:%d %d Hz)", ch, (int32)eq_freq_table_xg[val]);
			c->channel[ch].eq_xg.bass_freq = val;
			recompute_part_eq_xg(&(c->channel[ch].eq_xg));
		}
		break;
	case NRPN_ADDR_0135:	/* EQ TREBLE frequency */
		if (c->opt_eq_control) {
			if(val < 28) {val = 28;}
			else if(val > 58) {val = 58;}
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"EQ TREBLE frequency (CH:%d %d Hz)", ch, (int32)eq_freq_table_xg[val]);
			c->channel[ch].eq_xg.treble_freq = val;
			recompute_part_eq_xg(&(c->channel[ch].eq_xg));
		}
		break;
	case NRPN_ADDR_0163:	/* Attack Time */
		if (c->opt_tva_attack) {set_envelope_time(c, ch, val, EG_ATTACK);}
		break;
	case NRPN_ADDR_0164:	/* EG Decay Time */
		if (c->opt_tva_decay) {set_envelope_time(c, ch, val, EG_DECAY);}
		break;
	case NRPN_ADDR_0166:	/* EG Release Time */
		if (c->opt_tva_release) {set_envelope_time(c, ch, val, EG_RELEASE);}
		break;
	case NRPN_ADDR_1400:	/* Drum Filter Cutoff (XG) */
		drumflag = 1;
		note = c->channel[ch].lastlrpn;
		if (c->channel[ch].drums[note] == NULL)
			play_midi_setup_drums(c, ch, note);
		ctl->cmsg(CMSG_INFO, VERB_NOISY,
				"Drum Instrument Filter Cutoff (CH:%d NOTE:%d VAL:%d)",
				ch, note, val);
		c->channel[ch].drums[note]->drum_cutoff_freq = val - 64;
		break;
	case NRPN_ADDR_1500:	/* Drum Filter Resonance (XG) */
		drumflag = 1;
		note = c->channel[ch].lastlrpn;
		if (c->channel[ch].drums[note] == NULL)
			play_midi_setup_drums(c, ch, note);
		ctl->cmsg(CMSG_INFO, VERB_NOISY,
				"Drum Instrument Filter Resonance (CH:%d NOTE:%d VAL:%d)",
				ch, note, val);
		c->channel[ch].drums[note]->drum_resonance = val - 64;
		break;
	case NRPN_ADDR_1600:	/* Drum EG Attack Time (XG) */
		drumflag = 1;
		if (c->opt_tva_attack) {
			val = val & 0x7f;
			note = c->channel[ch].lastlrpn;
			if (c->channel[ch].drums[note] == NULL)
				play_midi_setup_drums(c, ch, note);
			val	-= 64;
			ctl->cmsg(CMSG_INFO, VERB_NOISY,
					"Drum Instrument Attack Time (CH:%d NOTE:%d VAL:%d)",
					ch, note, val);
			c->channel[ch].drums[note]->drum_envelope_rate[EG_ATTACK] = val;
		}
		break;
	case NRPN_ADDR_1700:	/* Drum EG Decay Time (XG) */
		drumflag = 1;
		if (c->opt_tva_decay) {
			val = val & 0x7f;
			note = c->channel[ch].lastlrpn;
			if (c->channel[ch].drums[note] == NULL)
				play_midi_setup_drums(c, ch, note);
			val	-= 64;
			ctl->cmsg(CMSG_INFO, VERB_NOISY,
					"Drum Instrument Decay Time (CH:%d NOTE:%d VAL:%d)",
					ch, note, val);
			c->channel[ch].drums[note]->drum_envelope_rate[EG_DECAY1] =
				c->channel[ch].drums[note]->drum_envelope_rate[EG_DECAY2] = val;
		}
		break;
	case NRPN_ADDR_1800:	/* Coarse Pitch of Drum (GS) */
		drumflag = 1;
		note = c->channel[ch].lastlrpn;
		if (c->channel[ch].drums[note] == NULL)
			play_midi_setup_drums(c, ch, note);
		c->channel[ch].drums[note]->coarse = val - 64;
		ctl->cmsg(CMSG_INFO, VERB_NOISY,
			"Drum Instrument Pitch Coarse (CH:%d NOTE:%d VAL:%d)",
			ch, note, c->channel[ch].drums[note]->coarse);
		c->channel[ch].pitchfactor = 0;
		break;
	case NRPN_ADDR_1900:	/* Fine Pitch of Drum (XG) */
		drumflag = 1;
		note = c->channel[ch].lastlrpn;
		if (c->channel[ch].drums[note] == NULL)
			play_midi_setup_drums(c, ch, note);
		c->channel[ch].drums[note]->fine = val - 64;
		ctl->cmsg(CMSG_INFO, VERB_NOISY,
				"Drum Instrument Pitch Fine (CH:%d NOTE:%d VAL:%d)",
				ch, note, c->channel[ch].drums[note]->fine);
		c->channel[ch].pitchfactor = 0;
		break;
	case NRPN_ADDR_1A00:	/* Level of Drum */
		drumflag = 1;
		note = c->channel[ch].lastlrpn;
		if (c->channel[ch].drums[note] == NULL)
			play_midi_setup_drums(c, ch, note);
		ctl->cmsg(CMSG_INFO, VERB_NOISY,
				"Drum Instrument TVA Level (CH:%d NOTE:%d VAL:%d)",
				ch, note, val);
		c->channel[ch].drums[note]->drum_level =
				calc_drum_tva_level(c, ch, note, val);
		break;
	case NRPN_ADDR_1C00:	/* Panpot of Drum */
		drumflag = 1;
		note = c->channel[ch].lastlrpn;
		if (c->channel[ch].drums[note] == NULL)
			play_midi_setup_drums(c, ch, note);
		if(val == 0) {
			val = int_rand(128);
			c->channel[ch].drums[note]->pan_random = 1;
		} else
			c->channel[ch].drums[note]->pan_random = 0;
		c->channel[ch].drums[note]->drum_panning = val;
		if (update_now && c->adjust_panning_immediately
				&& ! c->channel[ch].pan_random)
			adjust_drum_panning(c, ch, note);
		break;
	case NRPN_ADDR_1D00:	/* Reverb Send Level of Drum */
		drumflag = 1;
		note = c->channel[ch].lastlrpn;
		if (c->channel[ch].drums[note] == NULL)
			play_midi_setup_drums(c, ch, note);
		ctl->cmsg(CMSG_INFO, VERB_NOISY,
				"Reverb Send Level of Drum (CH:%d NOTE:%d VALUE:%d)",
				ch, note, val);
		if (c->channel[ch].drums[note]->reverb_level != val) {
			c->channel[ch].drum_effect_flag = 0;
		}
		c->channel[ch].drums[note]->reverb_level = val;
		break;
	case NRPN_ADDR_1E00:	/* Chorus Send Level of Drum */
		drumflag = 1;
		note = c->channel[ch].lastlrpn;
		if (c->channel[ch].drums[note] == NULL)
			play_midi_setup_drums(c, ch, note);
		ctl->cmsg(CMSG_INFO, VERB_NOISY,
				"Chorus Send Level of Drum (CH:%d NOTE:%d VALUE:%d)",
				ch, note, val);
		if (c->channel[ch].drums[note]->chorus_level != val) {
			c->channel[ch].drum_effect_flag = 0;
		}
		c->channel[ch].drums[note]->chorus_level = val;

		break;
	case NRPN_ADDR_1F00:	/* Variation Send Level of Drum */
		drumflag = 1;
		note = c->channel[ch].lastlrpn;
		if (c->channel[ch].drums[note] == NULL)
			play_midi_setup_drums(c, ch, note);
		ctl->cmsg(CMSG_INFO, VERB_NOISY,
				"Delay Send Level of Drum (CH:%d NOTE:%d VALUE:%d)",
				ch, note, val);
		if (c->channel[ch].drums[note]->delay_level != val) {
			c->channel[ch].drum_effect_flag = 0;
		}
		c->channel[ch].drums[note]->delay_level = val;
		break;
	case NRPN_ADDR_3000:	/* Drum EQ BASS */
		drumflag = 1;
		note = c->channel[ch].lastlrpn;
		if (c->channel[ch].drums[note] == NULL)
			play_midi_setup_drums(c, ch, note);
		break;
	case NRPN_ADDR_3100:	/* Drum EQ TREBLE */
		drumflag = 1;
		note = c->channel[ch].lastlrpn;
		if (c->channel[ch].drums[note] == NULL)
			play_midi_setup_drums(c, ch, note);
		break;
	case NRPN_ADDR_3400:	/* Drum EQ BASS frequency */
		drumflag = 1;
		note = c->channel[ch].lastlrpn;
		if (c->channel[ch].drums[note] == NULL)
			play_midi_setup_drums(c, ch, note);
		break;
	case NRPN_ADDR_3500:	/* Drum EQ TREBLE frequency */
		drumflag = 1;
		note = c->channel[ch].lastlrpn;
		if (c->channel[ch].drums[note] == NULL)
			play_midi_setup_drums(c, ch, note);
		break;
	case RPN_ADDR_0000:		/* Pitch bend sensitivity */
		ctl->cmsg(CMSG_INFO, VERB_DEBUG,
				"Pitch Bend Sensitivity (CH:%d VALUE:%d)", ch, val);
		/* for mod2mid.c, arpeggio */
		if (! IS_CURRENT_MOD_FILE && c->channel[ch].rpnmap[RPN_ADDR_0000] > 24)
			c->channel[ch].rpnmap[RPN_ADDR_0000] = 24;
		c->channel[ch].pitchfactor = 0;
		break;
	case RPN_ADDR_0001:		/* Master Fine Tuning */
		ctl->cmsg(CMSG_INFO, VERB_DEBUG,
				"Master Fine Tuning (CH:%d VALUE:%d)", ch, val);
		c->channel[ch].pitchfactor = 0;
		break;
	case RPN_ADDR_0002:		/* Master Coarse Tuning */
		ctl->cmsg(CMSG_INFO, VERB_DEBUG,
				"Master Coarse Tuning (CH:%d VALUE:%d)", ch, val);
		c->channel[ch].pitchfactor = 0;
		break;
	case RPN_ADDR_0003:		/* Tuning Program Select */
		ctl->cmsg(CMSG_INFO, VERB_DEBUG,
				"Tuning Program Select (CH:%d VALUE:%d)", ch, val);
		for (i = 0; i < c->upper_voices; i++)
			if (c->voice[i].status != VOICE_FREE) {
				c->voice[i].temper_instant = 1;
				recompute_freq(c, i);
			}
		break;
	case RPN_ADDR_0004:		/* Tuning Bank Select */
		ctl->cmsg(CMSG_INFO, VERB_DEBUG,
				"Tuning Bank Select (CH:%d VALUE:%d)", ch, val);
		for (i = 0; i < c->upper_voices; i++)
			if (c->voice[i].status != VOICE_FREE) {
				c->voice[i].temper_instant = 1;
				recompute_freq(c, i);
			}
		break;
	case RPN_ADDR_0005:		/* GM2: Modulation Depth Range */
		c->channel[ch].mod.lfo1_pitch_depth = (((int32)c->channel[ch].rpnmap[RPN_ADDR_0005] << 7) | c->channel[ch].rpnmap_lsb[RPN_ADDR_0005]) * 100 / 128;
		ctl->cmsg(CMSG_INFO, VERB_NOISY,
				"Modulation Depth Range (CH:%d VALUE:%d)", ch, c->channel[ch].rpnmap[RPN_ADDR_0005]);
		break;
	case RPN_ADDR_7F7F:		/* RPN reset */
		c->channel[ch].rpn_7f7f_flag = 1;
		break;
	case RPN_ADDR_FFFF:		/* RPN initialize */
		/* All reset to defaults */
		c->channel[ch].rpn_7f7f_flag = 0;
		memset(c->channel[ch].rpnmap, 0, sizeof(c->channel[ch].rpnmap));
		c->channel[ch].lastlrpn = c->channel[ch].lastmrpn = 0;
		c->channel[ch].nrpn = 0;
		c->channel[ch].rpnmap[RPN_ADDR_0000] = 2;
		c->channel[ch].rpnmap[RPN_ADDR_0001] = 0x40;
		c->channel[ch].rpnmap[RPN_ADDR_0002] = 0x40;
		c->channel[ch].rpnmap_lsb[RPN_ADDR_0005] = 0x40;
		c->channel[ch].rpnmap[RPN_ADDR_0005] = 0;	/* +- 50 cents */
		c->channel[ch].pitchfactor = 0;
		break;
	}
	drumflag = 0;
	if (drumflag && midi_drumpart_change(c, ch, 1)) {
		midi_program_change(c, ch, c->channel[ch].program);
		if (update_now)
			ctl_prog_event(c, ch);
	}
}

static void seek_forward(struct timiditycontext_t *c, int32 until_time)
{
    int32 i;
    int j, k, ch, orig_ch, port_ch, offset, layered;

    c->playmidi_seek_flag = 1;
    wrd_midi_event(c, WRD_START_SKIP, WRD_NOARG);
	while (MIDI_EVENT_TIME(c->current_event) < until_time) {
#ifndef SUPPRESS_CHANNEL_LAYER
		orig_ch = c->current_event->channel;
		layered = ! IS_SYSEX_EVENT_TYPE(c->current_event);
		for (j = 0; j < MAX_CHANNELS; j += 16) {
			port_ch = (orig_ch + j) % MAX_CHANNELS;
			offset = port_ch & ~0xf;
			for (k = offset; k < offset + 16; k++) {
				if (! layered && (j || k != offset))
					continue;
				if (layered) {
					if (! IS_SET_CHANNELMASK(
							c->channel[k].channel_layer, port_ch)
							|| c->channel[k].port_select != (orig_ch >> 4))
						continue;
					c->current_event->channel = k;
				}
#endif
	ch = c->current_event->channel;

	switch(c->current_event->type)
	{
	  case ME_PITCHWHEEL:
	    c->channel[ch].pitchbend = c->current_event->a + c->current_event->b * 128;
	    c->channel[ch].pitchfactor=0;
	    break;

	  case ME_MAINVOLUME:
	    c->channel[ch].volume = c->current_event->a;
	    break;

	  case ME_MASTER_VOLUME:
	    c->master_volume_ratio =
		(int32)c->current_event->a + 256 * (int32)c->current_event->b;
	    break;

	  case ME_PAN:
	    c->channel[ch].panning = c->current_event->a;
	    c->channel[ch].pan_random = 0;
	    break;

	  case ME_EXPRESSION:
	    c->channel[ch].expression=c->current_event->a;
	    break;

	  case ME_PROGRAM:
	    midi_program_change(c, ch, c->current_event->a);
	    break;

	  case ME_SUSTAIN:
		  c->channel[ch].sustain = c->current_event->a;
		  if (c->channel[ch].damper_mode == 0) {	/* half-damper is not allowed. */
			  if (c->channel[ch].sustain >= 64) {c->channel[ch].sustain = 127;}
			  else {c->channel[ch].sustain = 0;}
		  }
	    break;

	  case ME_SOSTENUTO:
		  c->channel[ch].sostenuto = (c->current_event->a >= 64);
	    break;

	  case ME_LEGATO_FOOTSWITCH:
        c->channel[ch].legato = (c->current_event->a >= 64);
	    break;

      case ME_HOLD2:
        break;

	  case ME_FOOT:
	    break;

	  case ME_BREATH:
	    break;

	  case ME_BALANCE:
	    break;

	  case ME_RESET_CONTROLLERS:
	    reset_controllers(c, ch);
	    break;

	  case ME_TONE_BANK_MSB:
	    c->channel[ch].bank_msb = c->current_event->a;
	    break;

	  case ME_TONE_BANK_LSB:
	    c->channel[ch].bank_lsb = c->current_event->a;
	    break;

	  case ME_MODULATION_WHEEL:
	    c->channel[ch].mod.val = c->current_event->a;
	    break;

	  case ME_PORTAMENTO_TIME_MSB:
	    c->channel[ch].portamento_time_msb = c->current_event->a;
	    break;

	  case ME_PORTAMENTO_TIME_LSB:
	    c->channel[ch].portamento_time_lsb = c->current_event->a;
	    break;

	  case ME_PORTAMENTO:
	    c->channel[ch].portamento = (c->current_event->a >= 64);
	    break;

	  case ME_MONO:
	    c->channel[ch].mono = 1;
	    break;

	  case ME_POLY:
	    c->channel[ch].mono = 0;
	    break;

	  case ME_SOFT_PEDAL:
		  if(c->opt_lpf_def) {
			  c->channel[ch].soft_pedal = c->current_event->a;
			  ctl->cmsg(CMSG_INFO,VERB_NOISY,"Soft Pedal (CH:%d VAL:%d)",ch,c->channel[ch].soft_pedal);
		  }
		  break;

	  case ME_HARMONIC_CONTENT:
		  if(c->opt_lpf_def) {
			  c->channel[ch].param_resonance = c->current_event->a - 64;
			  ctl->cmsg(CMSG_INFO,VERB_NOISY,"Harmonic Content (CH:%d VAL:%d)",ch,c->channel[ch].param_resonance);
		  }
		  break;

	  case ME_BRIGHTNESS:
		  if(c->opt_lpf_def) {
			  c->channel[ch].param_cutoff_freq = c->current_event->a - 64;
			  ctl->cmsg(CMSG_INFO,VERB_NOISY,"Brightness (CH:%d VAL:%d)",ch,c->channel[ch].param_cutoff_freq);
		  }
		  break;

	    /* RPNs */
	  case ME_NRPN_LSB:
	    c->channel[ch].lastlrpn = c->current_event->a;
	    c->channel[ch].nrpn = 1;
	    break;
	  case ME_NRPN_MSB:
	    c->channel[ch].lastmrpn = c->current_event->a;
	    c->channel[ch].nrpn = 1;
	    break;
	  case ME_RPN_LSB:
	    c->channel[ch].lastlrpn = c->current_event->a;
	    c->channel[ch].nrpn = 0;
	    break;
	  case ME_RPN_MSB:
	    c->channel[ch].lastmrpn = c->current_event->a;
	    c->channel[ch].nrpn = 0;
	    break;
	  case ME_RPN_INC:
	    if(c->channel[ch].rpn_7f7f_flag) /* disable */
		break;
	    if((i = last_rpn_addr(c, ch)) >= 0)
	    {
		if(c->channel[ch].rpnmap[i] < 127)
		    c->channel[ch].rpnmap[i]++;
		update_rpn_map(c, ch, i, 0);
	    }
	    break;
	case ME_RPN_DEC:
	    if(c->channel[ch].rpn_7f7f_flag) /* disable */
		break;
	    if((i = last_rpn_addr(c, ch)) >= 0)
	    {
		if(c->channel[ch].rpnmap[i] > 0)
		    c->channel[ch].rpnmap[i]--;
		update_rpn_map(c, ch, i, 0);
	    }
	    break;
	  case ME_DATA_ENTRY_MSB:
	    if(c->channel[ch].rpn_7f7f_flag) /* disable */
		break;
	    if((i = last_rpn_addr(c, ch)) >= 0)
	    {
		c->channel[ch].rpnmap[i] = c->current_event->a;
		update_rpn_map(c, ch, i, 0);
	    }
	    break;
	  case ME_DATA_ENTRY_LSB:
	    if(c->channel[ch].rpn_7f7f_flag) /* disable */
		break;
	    if((i = last_rpn_addr(c, ch)) >= 0)
	    {
		c->channel[ch].rpnmap_lsb[i] = c->current_event->a;
	    }
	    break;

	  case ME_REVERB_EFFECT:
		  if (c->opt_reverb_control) {
			if (ISDRUMCHANNEL(ch) && get_reverb_level(c, ch) != c->current_event->a) {c->channel[ch].drum_effect_flag = 0;}
			set_reverb_level(c, ch, c->current_event->a);
		  }
	    break;

	  case ME_CHORUS_EFFECT:
		if(c->opt_chorus_control == 1) {
			if (ISDRUMCHANNEL(ch) && c->channel[ch].chorus_level != c->current_event->a) {c->channel[ch].drum_effect_flag = 0;}
			c->channel[ch].chorus_level = c->current_event->a;
		} else {
			c->channel[ch].chorus_level = -c->opt_chorus_control;
		}

		if(c->current_event->a) {
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Chorus Send (CH:%d LEVEL:%d)",ch,c->current_event->a);
		}
		break;

	  case ME_TREMOLO_EFFECT:
		ctl->cmsg(CMSG_INFO,VERB_NOISY,"Tremolo Send (CH:%d LEVEL:%d)",ch,c->current_event->a);
		break;

	  case ME_CELESTE_EFFECT:
		if(c->opt_delay_control) {
			if (ISDRUMCHANNEL(ch) && c->channel[ch].delay_level != c->current_event->a) {c->channel[ch].drum_effect_flag = 0;}
			c->channel[ch].delay_level = c->current_event->a;
			if (c->play_system_mode == XG_SYSTEM_MODE) {
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"Delay Send (CH:%d LEVEL:%d)",ch,c->current_event->a);
			} else {
				ctl->cmsg(CMSG_INFO,VERB_NOISY,"Variation Send (CH:%d LEVEL:%d)",ch,c->current_event->a);
			}
		}
	    break;

	  case ME_ATTACK_TIME:
		if(!c->opt_tva_attack) { break; }
		set_envelope_time(c, ch, c->current_event->a, EG_ATTACK);
		break;

	  case ME_RELEASE_TIME:
		if(!c->opt_tva_release) { break; }
		set_envelope_time(c, ch, c->current_event->a, EG_RELEASE);
		break;

	  case ME_PHASER_EFFECT:
		ctl->cmsg(CMSG_INFO,VERB_NOISY,"Phaser Send (CH:%d LEVEL:%d)",ch,c->current_event->a);
		break;

	  case ME_RANDOM_PAN:
	    c->channel[ch].panning = int_rand(128);
	    c->channel[ch].pan_random = 1;
	    break;

	  case ME_SET_PATCH:
	    i = c->channel[ch].special_sample = c->current_event->a;
	    if(c->special_patch[i] != NULL)
		c->special_patch[i]->sample_offset = 0;
	    break;

	  case ME_TEMPO:
	    c->current_play_tempo = ch +
		c->current_event->b * 256 + c->current_event->a * 65536;
	    break;

	  case ME_RESET:
	    change_system_mode(c, c->current_event->a);
	    reset_midi(c, 0);
	    break;

	  case ME_PATCH_OFFS:
	    i = c->channel[ch].special_sample;
	    if(c->special_patch[i] != NULL)
		c->special_patch[i]->sample_offset =
		    (c->current_event->a | 256 * c->current_event->b);
	    break;

	  case ME_WRD:
	    wrd_midi_event(c, ch, c->current_event->a | 256 * c->current_event->b);
	    break;

	  case ME_SHERRY:
	    wrd_sherry_event(c, ch |
			     (c->current_event->a<<8) |
			     (c->current_event->b<<16));
	    break;

	  case ME_DRUMPART:
	    if(midi_drumpart_change(c, ch, c->current_event->a))
		midi_program_change(c, ch, c->channel[ch].program);
	    break;

	  case ME_KEYSHIFT:
	    c->channel[ch].key_shift = (int)c->current_event->a - 0x40;
	    break;

	case ME_KEYSIG:
		if (c->opt_init_keysig != 8)
			break;
		c->current_keysig = c->current_event->a + c->current_event->b * 16;
		break;

	case ME_MASTER_TUNING:
		set_master_tuning(c, (c->current_event->b << 8) | c->current_event->a);
		break;

	case ME_SCALE_TUNING:
		c->channel[ch].scale_tuning[c->current_event->a] = c->current_event->b;
		break;

	case ME_BULK_TUNING_DUMP:
		set_single_note_tuning(c, ch, c->current_event->a, c->current_event->b, 0);
		break;

	case ME_SINGLE_NOTE_TUNING:
		set_single_note_tuning(c, ch, c->current_event->a, c->current_event->b, 0);
		break;

	case ME_TEMPER_KEYSIG:
		c->current_temper_keysig = (c->current_event->a + 8) % 32 - 8;
		c->temper_adj = ((c->current_event->a + 8) & 0x20) ? 1 : 0;
		break;

	case ME_TEMPER_TYPE:
		c->channel[ch].temper_type = c->current_event->a;
		break;

	case ME_MASTER_TEMPER_TYPE:
		for (i = 0; i < MAX_CHANNELS; i++)
			c->channel[i].temper_type = c->current_event->a;
		break;

	case ME_USER_TEMPER_ENTRY:
		set_user_temper_entry(c, ch, c->current_event->a, c->current_event->b);
		break;

	  case ME_SYSEX_LSB:
	    process_sysex_event(c, ME_SYSEX_LSB,ch,c->current_event->a,c->current_event->b);
	    break;

	  case ME_SYSEX_MSB:
	    process_sysex_event(c, ME_SYSEX_MSB,ch,c->current_event->a,c->current_event->b);
	    break;

	  case ME_SYSEX_GS_LSB:
	    process_sysex_event(c, ME_SYSEX_GS_LSB,ch,c->current_event->a,c->current_event->b);
	    break;

	  case ME_SYSEX_GS_MSB:
	    process_sysex_event(c, ME_SYSEX_GS_MSB,ch,c->current_event->a,c->current_event->b);
	    break;

	  case ME_SYSEX_XG_LSB:
	    process_sysex_event(c, ME_SYSEX_XG_LSB,ch,c->current_event->a,c->current_event->b);
	    break;

	  case ME_SYSEX_XG_MSB:
	    process_sysex_event(c, ME_SYSEX_XG_MSB,ch,c->current_event->a,c->current_event->b);
	    break;

	  case ME_EOT:
	    c->current_sample = c->current_event->time;
	    c->playmidi_seek_flag = 0;
	    return;
	}
#ifndef SUPPRESS_CHANNEL_LAYER
			}
		}
		c->current_event->channel = orig_ch;
#endif
	c->current_event++;
    }
    wrd_midi_event(c, WRD_END_SKIP, WRD_NOARG);

    c->playmidi_seek_flag = 0;
    if(c->current_event != c->event_list)
	c->current_event--;
    c->current_sample = until_time;
}

static void skip_to(struct timiditycontext_t *c, int32 until_time)
{
  int ch;

  trace_flush(c);
  c->current_event = NULL;

  if (c->current_sample > until_time)
    c->current_sample=0;

  change_system_mode(c, DEFAULT_SYSTEM_MODE);
  reset_midi(c, 0);

  c->buffered_count = 0;
  c->buffer_pointer = c->common_buffer;
  c->current_event = c->event_list;
  c->current_play_tempo = 500000; /* 120 BPM */

  if (until_time)
    seek_forward(c, until_time);
  for(ch = 0; ch < MAX_CHANNELS; ch++)
      c->channel[ch].lasttime = c->current_sample;

  ctl_mode_event(c, CTLE_RESET, 0, 0, 0);
  trace_offset(c, until_time);

#ifdef SUPPORT_SOUNDSPEC
  soundspec_update_wave(NULL, 0);
#endif /* SUPPORT_SOUNDSPEC */
}

static int32 sync_restart(struct timiditycontext_t *c, int only_trace_ok)
{
    int32 cur;

    cur = current_trace_samples(c);
    if(cur == -1)
    {
	if(only_trace_ok)
	    return -1;
	cur = c->current_sample;
    }
    aq_flush(c, 1);
    skip_to(c, cur);
    return cur;
}

static int playmidi_change_rate(struct timiditycontext_t *c, int32 rate, int restart)
{
    int arg;

    if(rate == play_mode->rate)
	return 1; /* Not need to change */

    if(rate < MIN_OUTPUT_RATE || rate > MAX_OUTPUT_RATE)
    {
	ctl->cmsg(CMSG_ERROR, VERB_NORMAL,
		  "Out of sample rate: %d", rate);
	return -1;
    }

    if(restart)
    {
	if((c->midi_restart_time = current_trace_samples(c)) == -1)
	    c->midi_restart_time = c->current_sample;
    }
    else
	c->midi_restart_time = 0;

    arg = (int)rate;
    if(play_mode->acntl(PM_REQ_RATE, &arg) == -1)
    {
	ctl->cmsg(CMSG_ERROR, VERB_NORMAL,
		  "Can't change sample rate to %d", rate);
	return -1;
    }

    aq_flush(c, 1);
    aq_setup(c);
    aq_set_soft_queue(c, -1.0, -1.0);
    free_instruments(c, 1);
#ifdef SUPPORT_SOUNDSPEC
    soundspec_reinit();
#endif /* SUPPORT_SOUNDSPEC */
    return 0;
}

void playmidi_output_changed(struct timiditycontext_t *c, int play_state)
{
    if(target_play_mode == NULL)
	return;
    play_mode = target_play_mode;

    if(play_state == 0)
    {
	/* Playing */
	if((c->midi_restart_time = current_trace_samples(c)) == -1)
	    c->midi_restart_time = c->current_sample;
    }
    else /* Not playing */
	c->midi_restart_time = 0;

    if(play_state != 2)
    {
	aq_flush(c, 1);
	aq_setup(c);
	aq_set_soft_queue(c, -1.0, -1.0);
	clear_magic_instruments(c);
    }
    free_instruments(c, 1);
#ifdef SUPPORT_SOUNDSPEC
    soundspec_reinit();
#endif /* SUPPORT_SOUNDSPEC */
    target_play_mode = NULL;
}

int check_apply_control(struct timiditycontext_t *c)
{
    int rc;
    int32 val;

    if(c->file_from_stdin)
	return RC_NONE;
    rc = ctl->read(&val);
    switch(rc)
    {
      case RC_CHANGE_VOLUME:
	if (val>0 || c->amplification > -val)
	    c->amplification += val;
	else
	    c->amplification=0;
	if (c->amplification > MAX_AMPLIFICATION)
	    c->amplification=MAX_AMPLIFICATION;
	adjust_amplification(c);
	ctl_mode_event(c, CTLE_MASTER_VOLUME, 0, c->amplification, 0);
	break;
      case RC_SYNC_RESTART:
	aq_flush(c, 1);
	break;
      case RC_TOGGLE_PAUSE:
	c->play_pause_flag = !c->play_pause_flag;
	ctl_pause_event(c, c->play_pause_flag, 0);
	return RC_NONE;
      case RC_TOGGLE_SNDSPEC:
#ifdef SUPPORT_SOUNDSPEC
	if(view_soundspec_flag)
	    close_soundspec();
	else
	    open_soundspec();
	if(view_soundspec_flag || ctl_speana_flag)
	    soundspec_update_wave(NULL, -1);
	return RC_NONE;
      case RC_TOGGLE_CTL_SPEANA:
	ctl_speana_flag = !ctl_speana_flag;
	if(view_soundspec_flag || ctl_speana_flag)
	    soundspec_update_wave(NULL, -1);
#endif /* SUPPORT_SOUNDSPEC */
	return RC_NONE;
      case RC_CHANGE_RATE:
	if(playmidi_change_rate(c, val, 0))
	    return RC_NONE;
	return RC_RELOAD;
      case RC_OUTPUT_CHANGED:
	playmidi_output_changed(c, 1);
	return RC_RELOAD;
    }
    return rc;
}

static void voice_increment(struct timiditycontext_t *c, int n)
{
    int i;
    for(i = 0; i < n; i++)
    {
	if(c->voices == c->max_voices)
	    break;
	c->voice[c->voices].status = VOICE_FREE;
	c->voice[c->voices].temper_instant = 0;
	c->voice[c->voices].chorus_link = c->voices;
	c->voices++;
    }
    if(n > 0)
	ctl_mode_event(c, CTLE_MAXVOICES, 1, c->voices, 0);
}

static void voice_decrement(struct timiditycontext_t *c, int n)
{
    int i, j, lowest;
    int32 lv, v;

    /* decrease voice */
    for(i = 0; i < n && c->voices > 0; i++)
    {
	c->voices--;
	if(c->voice[c->voices].status == VOICE_FREE)
	    continue;	/* found */

	for(j = 0; j < c->voices; j++)
	    if(c->voice[j].status == VOICE_FREE)
		break;
	if(j != c->voices)
	{
	    c->voice[j] = c->voice[c->voices];
	    continue;	/* found */
	}

	/* Look for the decaying note with the lowest volume */
	lv = 0x7FFFFFFF;
	lowest = -1;
	for(j = 0; j <= c->voices; j++)
	{
	    if(c->voice[j].status & ~(VOICE_ON | VOICE_DIE))
	    {
		v = c->voice[j].left_mix;
		if((c->voice[j].panned==PANNED_MYSTERY) &&
		   (c->voice[j].right_mix > v))
		    v = c->voice[j].right_mix;
		if(v < lv)
		{
		    lv = v;
		    lowest = j;
		}
	    }
	}

	if(lowest != -1)
	{
	    c->cut_notes++;
	    free_voice(c, lowest);
	    ctl_note_event(c, lowest);
	    c->voice[lowest] = c->voice[c->voices];
	}
	else
	    c->lost_notes++;
    }
    if(c->upper_voices > c->voices)
	c->upper_voices = c->voices;
    if(n > 0)
	ctl_mode_event(c, CTLE_MAXVOICES, 1, c->voices, 0);
}

/* EAW -- do not throw away good notes, stop decrementing */
static void voice_decrement_conservative(struct timiditycontext_t *c, int n)
{
    int i, j, lowest, finalnv;
    int32 lv, v;

    /* decrease voice */
    finalnv = c->voices - n;
    for(i = 1; i <= n && c->voices > 0; i++)
    {
	if(c->voice[c->voices-1].status == VOICE_FREE) {
	    c->voices--;
	    continue;	/* found */
	}

	for(j = 0; j < finalnv; j++)
	    if(c->voice[j].status == VOICE_FREE)
		break;
	if(j != finalnv)
	{
	    c->voice[j] = c->voice[c->voices-1];
	    c->voices--;
	    continue;	/* found */
	}

	/* Look for the decaying note with the lowest volume */
	lv = 0x7FFFFFFF;
	lowest = -1;
	for(j = 0; j < c->voices; j++)
	{
	    if(c->voice[j].status & ~(VOICE_ON | VOICE_DIE) &&
	       !(c->voice[j].sample->note_to_use &&
	         ISDRUMCHANNEL(c->voice[j].channel)))
	    {
		v = c->voice[j].left_mix;
		if((c->voice[j].panned==PANNED_MYSTERY) &&
		   (c->voice[j].right_mix > v))
		    v = c->voice[j].right_mix;
		if(v < lv)
		{
		    lv = v;
		    lowest = j;
		}
	    }
	}

	if(lowest != -1)
	{
	    c->voices--;
	    c->cut_notes++;
	    free_voice(c, lowest);
	    ctl_note_event(c, lowest);
	    c->voice[lowest] = c->voice[c->voices];
	}
	else break;
    }
    if(c->upper_voices > c->voices)
	c->upper_voices = c->voices;
}

#define old_voices c->restore_voices_old_voices
void restore_voices(struct timiditycontext_t *c, int save_voices)
{
#ifdef REDUCE_VOICE_TIME_TUNING
    if(old_voices == -1 || save_voices)
	old_voices = c->voices;
    else if (c->voices < old_voices)
	voice_increment(c, old_voices - c->voices);
    else
	voice_decrement(c, c->voices - old_voices);
#endif /* REDUCE_VOICE_TIME_TUNING */
}
#undef old_voices


static int apply_controls(struct timiditycontext_t *c)
{
    int rc, i, jump_flag = 0;
    int32 val, cur;
    FLOAT_T r;
    ChannelBitMask tmp_chbitmask;

    /* ASCII renditions of CD player pictograms indicate approximate effect */
    do
    {
	switch(rc=ctl->read(&val))
	{
	  case RC_STOP:
	  case RC_QUIT:		/* [] */
	  case RC_LOAD_FILE:
	  case RC_NEXT:		/* >>| */
	  case RC_REALLY_PREVIOUS: /* |<< */
	  case RC_TUNE_END:	/* skip */
	    aq_flush(c, 1);
	    return rc;

	  case RC_CHANGE_VOLUME:
	    if (val>0 || c->amplification > -val)
		c->amplification += val;
	    else
		c->amplification=0;
	    if (c->amplification > MAX_AMPLIFICATION)
		c->amplification=MAX_AMPLIFICATION;
	    adjust_amplification(c);
	    for (i=0; i<c->upper_voices; i++)
		if (c->voice[i].status != VOICE_FREE)
		{
		    recompute_amp(c, i);
		    apply_envelope_to_amp(c, i);
		}
	    ctl_mode_event(c, CTLE_MASTER_VOLUME, 0, c->amplification, 0);
	    continue;

	  case RC_CHANGE_REV_EFFB:
	  case RC_CHANGE_REV_TIME:
	    reverb_rc_event(rc, val);
	    sync_restart(c, 0);
	    continue;

	  case RC_PREVIOUS:	/* |<< */
	    aq_flush(c, 1);
	    if (c->current_sample < 2*play_mode->rate)
		return RC_REALLY_PREVIOUS;
	    return RC_RESTART;

	  case RC_RESTART:	/* |<< */
	    if(c->play_pause_flag)
	    {
		c->midi_restart_time = 0;
		ctl_pause_event(c, 1, 0);
		continue;
	    }
	    aq_flush(c, 1);
	    skip_to(c, 0);
	    ctl_updatetime(c, 0);
	    jump_flag = 1;
		c->midi_restart_time = 0;
	    continue;

	  case RC_JUMP:
	    val = val * c->midi_time_ratio + 0.5;
	    if(c->play_pause_flag)
	    {
		c->midi_restart_time = val;
		ctl_pause_event(c, 1, val);
		continue;
	    }
	    aq_flush(c, 1);
	    if (val >= c->sample_count * c->midi_time_ratio + 0.5)
		return RC_TUNE_END;
	    skip_to(c, val);
	    ctl_updatetime(c, val);
	    return rc;

	  case RC_FORWARD:	/* >> */
	    if(c->play_pause_flag)
	    {
		c->midi_restart_time += val * c->midi_time_ratio + 0.5;
		if(c->midi_restart_time > c->sample_count * c->midi_time_ratio + 0.5)
		    c->midi_restart_time = c->sample_count * c->midi_time_ratio + 0.5;
		ctl_pause_event(c, 1, c->midi_restart_time);
		continue;
	    }
	    cur = current_trace_samples(c);
	    aq_flush(c, 1);
	    if(cur == -1)
		cur = c->current_sample;
	    if (val == 0x7fffffff)
		return RC_TUNE_END;
	    val = val * c->midi_time_ratio + 0.5;
	    if(val + cur >= c->sample_count * c->midi_time_ratio + 0.5)
		return RC_TUNE_END;
	    skip_to(c, val + cur);
	    ctl_updatetime(c, val + cur);
	    return RC_JUMP;

	  case RC_BACK:		/* << */
	    if(c->play_pause_flag)
	    {
		c->midi_restart_time -= val * c->midi_time_ratio + 0.5;
		if(c->midi_restart_time < 0)
		    c->midi_restart_time = 0;
		ctl_pause_event(c, 1, c->midi_restart_time);
		continue;
	    }
	    cur = current_trace_samples(c);
	    aq_flush(c, 1);
	    if(cur == -1)
		cur = c->current_sample;
	    val = val * c->midi_time_ratio + 0.5;
	    if(cur > val)
	    {
		skip_to(c, cur - val);
		ctl_updatetime(c, cur - val);
	    }
	    else
	    {
		skip_to(c, 0);
		ctl_updatetime(c, 0);
		c->midi_restart_time = 0;
	    }
	    return RC_JUMP;

	  case RC_TOGGLE_PAUSE:
	    if(c->play_pause_flag)
	    {
		c->play_pause_flag = 0;
		skip_to(c, c->midi_restart_time);
	    }
	    else
	    {
		c->midi_restart_time = current_trace_samples(c);
		if(c->midi_restart_time == -1)
		    c->midi_restart_time = c->current_sample;
		aq_flush(c, 1);
		c->play_pause_flag = 1;
	    }
	    ctl_pause_event(c, c->play_pause_flag, c->midi_restart_time);
	    jump_flag = 1;
	    continue;

	  case RC_KEYUP:
	  case RC_KEYDOWN:
	    c->note_key_offset += val;
	    c->current_freq_table += val;
	    c->current_freq_table -= floor(c->current_freq_table / 12.0) * 12;
	    c->current_temper_freq_table += val;
	    c->current_temper_freq_table -=
			floor(c->current_temper_freq_table / 12.0) * 12;
	    if(sync_restart(c, 1) != -1)
		jump_flag = 1;
	    ctl_mode_event(c, CTLE_KEY_OFFSET, 0, c->note_key_offset, 0);
	    continue;

	  case RC_SPEEDUP:
	    r = 1.0;
	    for(i = 0; i < val; i++)
		r *= SPEED_CHANGE_RATE;
	    sync_restart(c, 0);
	    c->midi_time_ratio /= r;
	    c->current_sample = (int32)(c->current_sample / r + 0.5);
	    trace_offset(c, c->current_sample);
	    jump_flag = 1;
	    ctl_mode_event(c, CTLE_TIME_RATIO, 0, 100 / c->midi_time_ratio + 0.5, 0);
	    continue;

	  case RC_SPEEDDOWN:
	    r = 1.0;
	    for(i = 0; i < val; i++)
		r *= SPEED_CHANGE_RATE;
	    sync_restart(c, 0);
	    c->midi_time_ratio *= r;
	    c->current_sample = (int32)(c->current_sample * r + 0.5);
	    trace_offset(c, c->current_sample);
	    jump_flag = 1;
	    ctl_mode_event(c, CTLE_TIME_RATIO, 0, 100 / c->midi_time_ratio + 0.5, 0);
	    continue;

	  case RC_VOICEINCR:
	    restore_voices(c, 0);
	    voice_increment(c, val);
	    if(sync_restart(c, 1) != -1)
		jump_flag = 1;
	    restore_voices(c, 1);
	    continue;

	  case RC_VOICEDECR:
	    restore_voices(c, 0);
	    if(sync_restart(c, 1) != -1)
	    {
		c->voices -= val;
		if(c->voices < 0)
		    c->voices = 0;
		jump_flag = 1;
	    }
	    else
		voice_decrement(c, val);
	    restore_voices(c, 1);
	    continue;

	  case RC_TOGGLE_DRUMCHAN:
	    c->midi_restart_time = current_trace_samples(c);
	    if(c->midi_restart_time == -1)
		c->midi_restart_time = c->current_sample;
	    SET_CHANNELMASK(c->drumchannel_mask, val);
	    SET_CHANNELMASK(c->current_file_info->drumchannel_mask, val);
	    if(IS_SET_CHANNELMASK(c->drumchannels, val))
	    {
		UNSET_CHANNELMASK(c->drumchannels, val);
		UNSET_CHANNELMASK(c->current_file_info->drumchannels, val);
	    }
	    else
	    {
		SET_CHANNELMASK(c->drumchannels, val);
		SET_CHANNELMASK(c->current_file_info->drumchannels, val);
	    }
	    aq_flush(c, 1);
	    return RC_RELOAD;

	  case RC_TOGGLE_SNDSPEC:
#ifdef SUPPORT_SOUNDSPEC
	    if(view_soundspec_flag)
		close_soundspec();
	    else
		open_soundspec();
	    if(view_soundspec_flag || ctl_speana_flag)
	    {
		sync_restart(c, 0);
		soundspec_update_wave(NULL, -1);
	    }
#endif /* SUPPORT_SOUNDSPEC */
	    continue;

	  case RC_TOGGLE_CTL_SPEANA:
#ifdef SUPPORT_SOUNDSPEC
	    ctl_speana_flag = !ctl_speana_flag;
	    if(view_soundspec_flag || ctl_speana_flag)
	    {
		sync_restart(c, 0);
		soundspec_update_wave(NULL, -1);
	    }
#endif /* SUPPORT_SOUNDSPEC */
	    continue;

	  case RC_SYNC_RESTART:
	    sync_restart(c, val);
	    jump_flag = 1;
	    continue;

	  case RC_RELOAD:
	    c->midi_restart_time = current_trace_samples(c);
	    if(c->midi_restart_time == -1)
		c->midi_restart_time = c->current_sample;
	    aq_flush(c, 1);
	    return RC_RELOAD;

	  case RC_CHANGE_RATE:
	    if(playmidi_change_rate(c, val, 1))
		return RC_NONE;
	    return RC_RELOAD;

	  case RC_OUTPUT_CHANGED:
	    playmidi_output_changed(c, 0);
	    return RC_RELOAD;

	case RC_TOGGLE_MUTE:
		TOGGLE_CHANNELMASK(c->channel_mute, val);
		sync_restart(c, 0);
		jump_flag = 1;
		ctl_mode_event(c, CTLE_MUTE, 0,
				val, (IS_SET_CHANNELMASK(c->channel_mute, val)) ? 1 : 0);
		continue;

	case RC_SOLO_PLAY:
		COPY_CHANNELMASK(tmp_chbitmask, c->channel_mute);
		FILL_CHANNELMASK(c->channel_mute);
		UNSET_CHANNELMASK(c->channel_mute, val);
		if (! COMPARE_CHANNELMASK(tmp_chbitmask, c->channel_mute)) {
			sync_restart(c, 0);
			jump_flag = 1;
			for (i = 0; i < MAX_CHANNELS; i++)
				ctl_mode_event(c, CTLE_MUTE, 0, i, 1);
			ctl_mode_event(c, CTLE_MUTE, 0, val, 0);
		}
		continue;

	case RC_MUTE_CLEAR:
		COPY_CHANNELMASK(tmp_chbitmask, c->channel_mute);
		CLEAR_CHANNELMASK(c->channel_mute);
		if (! COMPARE_CHANNELMASK(tmp_chbitmask, c->channel_mute)) {
			sync_restart(c, 0);
			jump_flag = 1;
			for (i = 0; i < MAX_CHANNELS; i++)
				ctl_mode_event(c, CTLE_MUTE, 0, i, 0);
		}
		continue;
	}
	if(c->intr)
	    return RC_QUIT;
	if(c->play_pause_flag)
	    usleep(300000);
    } while (rc != RC_NONE || c->play_pause_flag);
    return jump_flag ? RC_JUMP : RC_NONE;
}

static void mix_signal(int32 *dest, int32 *src, int32 count)
{
	int32 i;
	for (i = 0; i < count; i++) {
		dest[i] += src[i];
	}
}

#ifdef __BORLANDC__
static int is_insertion_effect_xg(struct timiditycontext_t *c, int ch)
#else
inline static int is_insertion_effect_xg(struct timiditycontext_t *c, int ch)
#endif
{
	int i;
	for (i = 0; i < XG_INSERTION_EFFECT_NUM; i++) {
		if (c->insertion_effect_xg[i].part == ch) {
			return 1;
		}
	}
	for (i = 0; i < XG_VARIATION_EFFECT_NUM; i++) {
		if (c->variation_effect_xg[i].connection == XG_CONN_INSERTION
			&& c->variation_effect_xg[i].part == ch) {
			return 1;
		}
	}
	return 0;
}

#ifdef USE_DSP_EFFECT
/* do_compute_data_midi() with DSP Effect */
static void do_compute_data_midi(struct timiditycontext_t *c, int32 count)
{
	int i, j, uv, stereo, n, ch, note;
	int32 *vpblist[MAX_CHANNELS];
	int channel_effect, channel_reverb, channel_chorus, channel_delay, channel_eq;
	int32 cnt = count * 2, rev_max_delay_out;
	struct DrumPartEffect *de;

	stereo = ! (play_mode->encoding & PE_MONO);
	n = count * ((stereo) ? 8 : 4); /* in bytes */

	memset(c->buffer_pointer, 0, n);
	memset(c->insertion_effect_buffer, 0, n);

	if (c->opt_reverb_control == 3) {
		rev_max_delay_out = 0x7fffffff;	/* disable */
	} else {
		rev_max_delay_out = REVERB_MAX_DELAY_OUT;
	}

	/* are effects valid? / don't supported in mono */
	channel_reverb = (stereo && (c->opt_reverb_control == 1
			|| c->opt_reverb_control == 3
			|| (c->opt_reverb_control < 0 && c->opt_reverb_control & 0x80)));
	channel_chorus = (stereo && c->opt_chorus_control && !c->opt_surround_chorus);
	channel_delay = (stereo && c->opt_delay_control > 0);

	/* is EQ valid? */
	channel_eq = c->opt_eq_control && (c->eq_status_gs.low_gain != 0x40 || c->eq_status_gs.high_gain != 0x40 ||
		c->play_system_mode == XG_SYSTEM_MODE);

	channel_effect = (stereo && (channel_reverb || channel_chorus
			|| channel_delay || channel_eq || c->opt_insertion_effect));

	uv = c->upper_voices;
	for(i = 0; i < uv; i++) {
		if(c->voice[i].status != VOICE_FREE) {
			c->channel[c->voice[i].channel].lasttime = c->current_sample + count;
		}
	}

	/* appropriate buffers for channels */
	if(channel_effect) {
		int buf_index = 0;

		if(c->reverb_buffer == NULL) {	/* allocating buffer for channel effect */
			c->reverb_buffer = (char *)safe_malloc(MAX_CHANNELS * AUDIO_BUFFER_SIZE * 8);
		}

		for(i = 0; i < MAX_CHANNELS; i++) {
			if(c->opt_insertion_effect && c->channel[i].insertion_effect) {
				vpblist[i] = c->insertion_effect_buffer;
			} else if(c->channel[i].eq_gs || (get_reverb_level(c, i) != DEFAULT_REVERB_SEND_LEVEL
					&& c->current_sample - c->channel[i].lasttime < rev_max_delay_out)
					|| c->channel[i].chorus_level > 0 || c->channel[i].delay_level > 0
					|| c->channel[i].eq_xg.valid
					|| c->channel[i].dry_level != 127
					|| (c->opt_drum_effect && ISDRUMCHANNEL(i))
					|| is_insertion_effect_xg(c, i)) {
				vpblist[i] = (int32*)(c->reverb_buffer + buf_index);
				buf_index += n;
			} else {
				vpblist[i] = c->buffer_pointer;
			}
			/* clear buffers of drum-part effect */
			if (c->opt_drum_effect && ISDRUMCHANNEL(i)) {
				for (j = 0; j < c->channel[i].drum_effect_num; j++) {
					if (c->channel[i].drum_effect[j].buf != NULL) {
						memset(c->channel[i].drum_effect[j].buf, 0, n);
					}
				}
			}
		}

		if(buf_index) {memset(c->reverb_buffer, 0, buf_index);}
	}

	for (i = 0; i < uv; i++) {
		if (c->voice[i].status != VOICE_FREE) {
			int32 *vpb = NULL;
			int8 flag;

			if (channel_effect) {
				flag = 0;
				ch = c->voice[i].channel;
				if (c->opt_drum_effect && ISDRUMCHANNEL(ch)) {
					make_drum_effect(c, ch);
					note = c->voice[i].note;
					for (j = 0; j < c->channel[ch].drum_effect_num; j++) {
						if (c->channel[ch].drum_effect[j].note == note) {
							vpb = c->channel[ch].drum_effect[j].buf;
							flag = 1;
						}
					}
					if (flag == 0) {vpb = vpblist[ch];}
				} else {
					vpb = vpblist[ch];
				}
			} else {
				vpb = c->buffer_pointer;
			}

			if(!IS_SET_CHANNELMASK(c->channel_mute, c->voice[i].channel)) {
				mix_voice(c, vpb, i, count);
			} else {
				free_voice(c, i);
				ctl_note_event(c, i);
			}

			if(c->voice[i].timeout == 1 && c->voice[i].timeout < c->current_sample) {
				free_voice(c, i);
				ctl_note_event(c, i);
			}
		}
	}

	while(uv > 0 && c->voice[uv - 1].status == VOICE_FREE)	{uv--;}
	c->upper_voices = uv;

	if(c->play_system_mode == XG_SYSTEM_MODE && channel_effect) {	/* XG */
		if (c->opt_insertion_effect) {	/* insertion effect */
			for (i = 0; i < XG_INSERTION_EFFECT_NUM; i++) {
				if (c->insertion_effect_xg[i].part <= MAX_CHANNELS) {
					do_insertion_effect_xg(c, vpblist[c->insertion_effect_xg[i].part], cnt, &c->insertion_effect_xg[i]);
				}
			}
			for (i = 0; i < XG_VARIATION_EFFECT_NUM; i++) {
				if (c->variation_effect_xg[i].part <= MAX_CHANNELS) {
					do_insertion_effect_xg(c, vpblist[c->variation_effect_xg[i].part], cnt, &c->variation_effect_xg[i]);
				}
			}
		}
		for(i = 0; i < MAX_CHANNELS; i++) {	/* system effects */
			int32 *p;
			p = vpblist[i];
			if(p != c->buffer_pointer) {
				if (c->opt_drum_effect && ISDRUMCHANNEL(i)) {
					for (j = 0; j < c->channel[i].drum_effect_num; j++) {
						de = &(c->channel[i].drum_effect[j]);
						if (de->reverb_send > 0) {
							set_ch_reverb(c, de->buf, cnt, de->reverb_send);
						}
						if (de->chorus_send > 0) {
							set_ch_chorus(c, de->buf, cnt, de->chorus_send);
						}
						if (de->delay_send > 0) {
							set_ch_delay(c, de->buf, cnt, de->delay_send);
						}
						mix_signal(p, de->buf, cnt);
					}
				} else {
					if(channel_eq && c->channel[i].eq_xg.valid) {
						do_ch_eq_xg(p, cnt, &(c->channel[i].eq_xg));
					}
					if(channel_chorus && c->channel[i].chorus_level > 0) {
						set_ch_chorus(c, p, cnt, c->channel[i].chorus_level);
					}
					if(channel_delay && c->channel[i].delay_level > 0) {
						set_ch_delay(c, p, cnt, c->channel[i].delay_level);
					}
					if(channel_reverb && c->channel[i].reverb_level > 0
						&& c->current_sample - c->channel[i].lasttime < rev_max_delay_out) {
						set_ch_reverb(c, p, cnt, c->channel[i].reverb_level);
					}
				}
				if(c->channel[i].dry_level == 127) {
					set_dry_signal(c, p, cnt);
				} else {
					set_dry_signal_xg(c, p, cnt, c->channel[i].dry_level);
				}
			}
		}

		if(channel_reverb) {
			set_ch_reverb(c, c->buffer_pointer, cnt, DEFAULT_REVERB_SEND_LEVEL);
		}
		set_dry_signal(c, c->buffer_pointer, cnt);

		/* mixing signal and applying system effects */
		mix_dry_signal(c, c->buffer_pointer, cnt);
		if(channel_delay) {do_variation_effect1_xg(c, c->buffer_pointer, cnt);}
		if(channel_chorus) {do_ch_chorus_xg(c, c->buffer_pointer, cnt);}
#warning should this have been do_ch_reverb_xg ?
		if(channel_reverb) {do_ch_reverb(c, c->buffer_pointer, cnt);}
		if(c->multi_eq_xg.valid) {do_multi_eq_xg(c, c->buffer_pointer, cnt);}
	} else if(channel_effect) {	/* GM & GS */
		if(c->opt_insertion_effect) {	/* insertion effect */
			/* applying insertion effect */
			do_insertion_effect_gs(c, c->insertion_effect_buffer, cnt);
			/* sending insertion effect voice to channel effect */
			set_ch_chorus(c, c->insertion_effect_buffer, cnt, c->insertion_effect_gs.send_chorus);
			set_ch_delay(c, c->insertion_effect_buffer, cnt, c->insertion_effect_gs.send_delay);
			set_ch_reverb(c, c->insertion_effect_buffer, cnt,	c->insertion_effect_gs.send_reverb);
			if(c->insertion_effect_gs.send_eq_switch && channel_eq) {
				set_ch_eq_gs(c, c->insertion_effect_buffer, cnt);
			} else {
				set_dry_signal(c, c->insertion_effect_buffer, cnt);
			}
		}

		for(i = 0; i < MAX_CHANNELS; i++) {	/* system effects */
			int32 *p;
			p = vpblist[i];
			if(p != c->buffer_pointer && p != c->insertion_effect_buffer) {
				if (c->opt_drum_effect && ISDRUMCHANNEL(i)) {
					for (j = 0; j < c->channel[i].drum_effect_num; j++) {
						de = &(c->channel[i].drum_effect[j]);
						if (de->reverb_send > 0) {
							set_ch_reverb(c, de->buf, cnt, de->reverb_send);
						}
						if (de->chorus_send > 0) {
							set_ch_chorus(c, de->buf, cnt, de->chorus_send);
						}
						if (de->delay_send > 0) {
							set_ch_delay(c, de->buf, cnt, de->delay_send);
						}
						mix_signal(p, de->buf, cnt);
					}
				} else {
					if(channel_chorus && c->channel[i].chorus_level > 0) {
						set_ch_chorus(c, p, cnt, c->channel[i].chorus_level);
					}
					if(channel_delay && c->channel[i].delay_level > 0) {
						set_ch_delay(c, p, cnt, c->channel[i].delay_level);
					}
					if(channel_reverb && c->channel[i].reverb_level > 0
						&& c->current_sample - c->channel[i].lasttime < rev_max_delay_out) {
						set_ch_reverb(c, p, cnt, c->channel[i].reverb_level);
					}
				}
				if(channel_eq && c->channel[i].eq_gs) {
					set_ch_eq_gs(c, p, cnt);
				} else {
					set_dry_signal(c, p, cnt);
				}
			}
		}

		if(channel_reverb) {
			set_ch_reverb(c, c->buffer_pointer, cnt, DEFAULT_REVERB_SEND_LEVEL);
		}
		set_dry_signal(c, c->buffer_pointer, cnt);

		/* mixing signal and applying system effects */
		mix_dry_signal(c, c->buffer_pointer, cnt);
		if(channel_eq) {do_ch_eq_gs(c, c->buffer_pointer, cnt);}
		if(channel_chorus) {do_ch_chorus(c, c->buffer_pointer, cnt);}
		if(channel_delay) {do_ch_delay(c, c->buffer_pointer, cnt);}
		if(channel_reverb) {do_ch_reverb(c, c->buffer_pointer, cnt);}
	}

	c->current_sample += count;
}

#else
/* do_compute_data_midi() without DSP Effect */
static void do_compute_data_midi(struct timiditycontext_t *c, int32 count)
{
	int i, j, uv, stereo, n, ch, note;
	int32 *vpblist[MAX_CHANNELS];
	int vc[MAX_CHANNELS];
	int channel_reverb;
	int channel_effect;
	int32 cnt = count * 2;

	stereo = ! (play_mode->encoding & PE_MONO);
	n = count * ((stereo) ? 8 : 4); /* in bytes */
	/* don't supported in mono */
	channel_reverb = (stereo && (c->opt_reverb_control == 1
			|| c->opt_reverb_control == 3
			|| c->opt_reverb_control < 0 && c->opt_reverb_control & 0x80));
	memset(c->buffer_pointer, 0, n);

	channel_effect = (stereo && (c->opt_reverb_control || c->opt_chorus_control
			|| c->opt_delay_control || c->opt_eq_control || c->opt_insertion_effect));
	uv = c->upper_voices;
	for (i = 0; i < uv; i++)
		if (c->voice[i].status != VOICE_FREE)
			c->channel[c->voice[i].channel].lasttime = c->current_sample + count;

	if (channel_reverb) {
		int chbufidx;

		if (! c->make_rvid_flag) {
			make_rvid();
			c->make_rvid_flag = 1;
		}
		chbufidx = 0;
		for (i = 0; i < MAX_CHANNELS; i++) {
			vc[i] = 0;
			if (c->channel[i].reverb_id != -1
					&& c->current_sample - c->channel[i].lasttime
					< REVERB_MAX_DELAY_OUT) {
				if (c->reverb_buffer == NULL)
					c->reverb_buffer = (char *) safe_malloc(MAX_CHANNELS
							* AUDIO_BUFFER_SIZE * 8);
				if (c->channel[i].reverb_id != i)
					vpblist[i] = vpblist[c->channel[i].reverb_id];
				else {
					vpblist[i] = (int32 *) (c->reverb_buffer + chbufidx);
					chbufidx += n;
				}
			} else
				vpblist[i] = c->buffer_pointer;
		}
		if (chbufidx)
			memset(c->reverb_buffer, 0, chbufidx);
	}
	for (i = 0; i < uv; i++)
		if (c->voice[i].status != VOICE_FREE) {
			int32 *vpb;

			if (channel_reverb) {
				int ch = c->voice[i].channel;

				vpb = vpblist[ch];
				vc[ch] = 1;
			} else
				vpb = c->buffer_pointer;
			if (! IS_SET_CHANNELMASK(c->channel_mute, c->voice[i].channel))
				mix_voice(vpb, i, count);
			else {
				free_voice(c, i);
				ctl_note_event(c, i);
			}
			if (c->voice[i].timeout == 1 && c->voice[i].timeout < c->current_sample) {
				free_voice(c, i);
				ctl_note_event(c, i);
			}
		}

	while (uv > 0 && c->voice[uv - 1].status == VOICE_FREE)
		uv--;
	c->upper_voices = uv;

	if (channel_reverb) {
		int k;

		k = count * 2; /* calclated buffer length in int32 */
		for (i = 0; i < MAX_CHANNELS; i++) {
			int32 *p;

			p = vpblist[i];
			if (p != c->buffer_pointer && c->channel[i].reverb_id == i)
				set_ch_reverb(c, p, k, c->channel[i].reverb_level);
		}
		set_ch_reverb(c, c->buffer_pointer, k, DEFAULT_REVERB_SEND_LEVEL);
		do_ch_reverb(c, c->buffer_pointer, k);
	}
	c->current_sample += count;
}
#endif

static void do_compute_data_wav(struct timiditycontext_t *c, int32 count)
{
	int i, stereo, samples, req_size, act_samples, v;
	int16 wav_buffer[AUDIO_BUFFER_SIZE*2]; // used to be global static buffer

	stereo = !(play_mode->encoding & PE_MONO);
	samples = (stereo ? (count * 2) : count);
	req_size = samples * 2; /* assume 16bit */

	act_samples = tf_read(c, wav_buffer, 1, req_size, c->current_file_info->pcm_tf) / 2;
	for(i = 0; i < act_samples; i++) {
		v = (uint16)LE_SHORT(wav_buffer[i]);
		c->buffer_pointer[i] = (int32)((v << 16) | (v ^ 0x8000)) / 4; /* 4 : level down */
	}
	for(; i < samples; i++)
		c->buffer_pointer[i] = 0;

	c->current_sample += count;
}

static void do_compute_data_aiff(struct timiditycontext_t *c, int32 count)
{
	int i, stereo, samples, req_size, act_samples, v;
	int16 wav_buffer[AUDIO_BUFFER_SIZE*2]; // used to be global static buffer

	stereo = !(play_mode->encoding & PE_MONO);
	samples = (stereo ? (count * 2) : count);
	req_size = samples * 2; /* assume 16bit */

	act_samples = tf_read(c, wav_buffer, 1, req_size, c->current_file_info->pcm_tf) / 2;
	for(i = 0; i < act_samples; i++) {
		v = (uint16)BE_SHORT(wav_buffer[i]);
		c->buffer_pointer[i] = (int32)((v << 16) | (v ^ 0x8000)) / 4; /* 4 : level down */
	}
	for(; i < samples; i++)
		c->buffer_pointer[i] = 0;

	c->current_sample += count;
}


static void do_compute_data(struct timiditycontext_t *c, int32 count)
{
    switch(c->current_file_info->pcm_mode)
    {
      case PCM_MODE_NON:
	do_compute_data_midi(c, count);
	break;
      case PCM_MODE_WAV:
	do_compute_data_wav(c, count);
        break;
      case PCM_MODE_AIFF:
	do_compute_data_aiff(c, count);
        break;
      case PCM_MODE_AU:
        break;
      case PCM_MODE_MP3:
        break;
    }
}

static int check_midi_play_end(MidiEvent *e, int len)
{
    int i, type;

    for(i = 0; i < len; i++)
    {
	type = e[i].type;
	if(type == ME_NOTEON || type == ME_LAST || type == ME_WRD || type == ME_SHERRY)
	    return 0;
	if(type == ME_EOT)
	    return i + 1;
    }
    return 0;
}

static int compute_data(struct timiditycontext_t *c, int32 count);
static int midi_play_end(struct timiditycontext_t *c)
{
    int i, rc = RC_TUNE_END;

    c->check_eot_flag = 0;

    if(c->opt_realtime_playing && c->current_sample == 0)
    {
	reset_voices(c);
	return RC_TUNE_END;
    }

    if(c->upper_voices > 0)
    {
	int fadeout_cnt;

	rc = compute_data(c, play_mode->rate);
	if(RC_IS_SKIP_FILE(rc))
	    goto midi_end;

	for(i = 0; i < c->upper_voices; i++)
	    if(c->voice[i].status & (VOICE_ON | VOICE_SUSTAINED))
		finish_note(c, i);
	if(c->opt_realtime_playing)
	    fadeout_cnt = 3;
	else
	    fadeout_cnt = 6;
	for(i = 0; i < fadeout_cnt && c->upper_voices > 0; i++)
	{
	    rc = compute_data(c, play_mode->rate / 2);
	    if(RC_IS_SKIP_FILE(rc))
		goto midi_end;
	}

	/* kill voices */
	kill_all_voices(c);
	rc = compute_data(c, MAX_DIE_TIME);
	if(RC_IS_SKIP_FILE(rc))
	    goto midi_end;
	c->upper_voices = 0;
    }

    /* clear reverb echo sound */
    init_reverb(c);
    for(i = 0; i < MAX_CHANNELS; i++)
    {
	c->channel[i].reverb_level = -1;
	c->channel[i].reverb_id = -1;
	c->make_rvid_flag = 1;
    }

    /* output null sound */
    if(c->opt_realtime_playing)
	rc = compute_data(c, (int32)(play_mode->rate * PLAY_INTERLEAVE_SEC/2));
    else
	rc = compute_data(c, (int32)(play_mode->rate * PLAY_INTERLEAVE_SEC));
    if(RC_IS_SKIP_FILE(rc))
	goto midi_end;

    compute_data(c, 0); /* flush buffer to device */

    if(ctl->trace_playing)
    {
	rc = aq_flush(c, 0); /* Wait until play out */
	if(RC_IS_SKIP_FILE(rc))
	    goto midi_end;
    }
    else
    {
	trace_flush(c);
	rc = aq_soft_flush(c);
	if(RC_IS_SKIP_FILE(rc))
	    goto midi_end;
    }

  midi_end:
    if(RC_IS_SKIP_FILE(rc))
	aq_flush(c, 1);

    ctl->cmsg(CMSG_INFO, VERB_VERBOSE, "Playing time: ~%d seconds",
	      c->current_sample/play_mode->rate+2);
    ctl->cmsg(CMSG_INFO, VERB_VERBOSE, "Notes cut: %d",
	      c->cut_notes);
    ctl->cmsg(CMSG_INFO, VERB_VERBOSE, "Notes lost totally: %d",
	      c->lost_notes);
    if(RC_IS_SKIP_FILE(rc))
	return rc;
    return RC_TUNE_END;
}

/* count=0 means flush remaining buffered data to output device, then
   flush the device itself */
#define last_filled c->compute_data_last_filled
static int compute_data(struct timiditycontext_t *c, int32 count)
{
  int rc;

  if (!count)
    {
      if (c->buffered_count)
      {
	  ctl->cmsg(CMSG_INFO, VERB_DEBUG_SILLY,
		    "output data (%d)", c->buffered_count);

#ifdef SUPPORT_SOUNDSPEC
	  soundspec_update_wave(c->common_buffer, c->buffered_count);
#endif /* SUPPORT_SOUNDSPEC */

	  if(aq_add(c, c->common_buffer, c->buffered_count) == -1)
	      return RC_ERROR;
      }
      c->buffer_pointer = c->common_buffer;
      c->buffered_count = 0;
      return RC_NONE;
    }

  while ((count+c->buffered_count) >= audio_buffer_size)
    {
      int i;

      if((rc = apply_controls(c)) != RC_NONE)
	  return rc;

      do_compute_data(c, audio_buffer_size-c->buffered_count);
      count -= audio_buffer_size-c->buffered_count;
      ctl->cmsg(CMSG_INFO, VERB_DEBUG_SILLY,
		"output data (%d)", audio_buffer_size);

#ifdef SUPPORT_SOUNDSPEC
      soundspec_update_wave(c->common_buffer, audio_buffer_size);
#endif /* SUPPORT_SOUNDSPEC */

      /* fall back to linear interpolation when queue < 100% */
      if (! c->opt_realtime_playing && (play_mode->flag & PF_CAN_TRACE)) {
	  if (!c->aq_fill_buffer_flag &&
	      100 * ((double)(aq_filled(c) + aq_soft_filled(c)) /
		     aq_get_dev_queuesize(c)) < 99)
	      c->reduce_quality_flag = 1;
	  else
	      c->reduce_quality_flag = c->no_4point_interpolation;
      }

#ifdef REDUCE_VOICE_TIME_TUNING
      /* Auto voice reduce implementation by Masanao Izumo */
      if(c->reduce_voice_threshold &&
	 (play_mode->flag & PF_CAN_TRACE) &&
	 !c->aq_fill_buffer_flag &&
	 aq_get_dev_queuesize(c) > 0)
      {
	  /* Reduce voices if there is not enough audio device buffer */

          int nv, filled, filled_limit, rate, rate_limit;

	  filled = aq_filled(c);

	  rate_limit = 75;
	  if(c->reduce_voice_threshold >= 0)
	  {
	      filled_limit = play_mode->rate * c->reduce_voice_threshold / 1000
		  + 1; /* +1 disable zero */
	  }
	  else /* Use default threshold */
	  {
	      int32 maxfill;
	      maxfill = aq_get_dev_queuesize(c);
	      filled_limit = REDUCE_VOICE_TIME_TUNING;
	      if(filled_limit > maxfill / 5) /* too small audio buffer */
	      {
		  rate_limit -= 100 * audio_buffer_size / maxfill / 5;
		  filled_limit = 1;
	      }
	  }

	  /* Calculate rate as it is displayed in ncurs_c.c */
	  /* The old method of calculating rate resulted in very low values
	     when using the new high order interplation methods on "slow"
	     CPUs when the queue was being drained WAY too quickly.  This
	     caused premature voice reduction under Linux, even if the queue
	     was over 2000%, leading to major voice lossage. */
	  rate = (int)(((double)(aq_filled(c) + aq_soft_filled(c)) /
	aq_get_dev_queuesize(c)) * 100 + 0.5);

          for(i = nv = 0; i < c->upper_voices; i++)
	      if(c->voice[i].status != VOICE_FREE)
	          nv++;

	  if(! c->opt_realtime_playing)
	  {
	      /* calculate ok_nv, the "optimum" max polyphony */
	      if (c->auto_reduce_polyphony && rate < 85) {
		/* average in current nv */
	        if ((rate == c->old_rate && nv > c->min_bad_nv) ||
	            (rate >= c->old_rate && rate < 20)) {
		c->ok_nv_total += nv;
		c->ok_nv_counts++;
	        }
	        /* increase polyphony when it is too low */
	        else if (nv == c->voices &&
	                 (rate > c->old_rate && filled > last_filled)) {
			c->ok_nv_total += nv + 1;
			c->ok_nv_counts++;
	        }
	        /* reduce polyphony when loosing buffer */
	        else if (rate < 75 &&
		 (rate < c->old_rate && filled < last_filled)) {
		c->ok_nv_total += c->min_bad_nv;
			c->ok_nv_counts++;
	        }
	        else goto NO_RESCALE_NV;

		/* rescale ok_nv stuff every 1 seconds */
		if (c->current_sample >= c->ok_nv_sample && c->ok_nv_counts > 1) {
			c->ok_nv_total >>= 1;
			c->ok_nv_counts >>= 1;
			c->ok_nv_sample = c->current_sample + (play_mode->rate);
		}

		NO_RESCALE_NV:;
	      }
	  }

	  /* EAW -- if buffer is < 75%, start reducing some voices to
	     try to let it recover.  This really helps a lot, preserves
	     decent sound, and decreases the frequency of lost ON notes */
	  if ((! c->opt_realtime_playing && rate < rate_limit)
	      || filled < filled_limit)
	  {
	      if(filled <= last_filled)
	      {
	          int kill_nv, temp_nv;

		  /* set bounds on "good" and "bad" nv */
		  if (! c->opt_realtime_playing && rate > 20 &&
		      nv < c->min_bad_nv) {
			c->min_bad_nv = nv;
	                if (c->max_good_nv < c->min_bad_nv)
		c->max_good_nv = c->min_bad_nv;
	          }

		  /* EAW -- count number of !ON voices */
		  /* treat chorus notes as !ON */
		  for(i = kill_nv = 0; i < c->upper_voices; i++) {
		      if(c->voice[i].status & VOICE_FREE ||
		         c->voice[i].cache != NULL)
				continue;

		      if((c->voice[i].status & ~(VOICE_ON|VOICE_SUSTAINED) &&
			  !(c->voice[i].status & ~(VOICE_DIE) &&
			    c->voice[i].sample->note_to_use)))
				kill_nv++;
		  }

		  /* EAW -- buffer is dangerously low, drasticly reduce
		     voices to a hopefully "safe" amount */
		  if (filled < filled_limit &&
		      (c->opt_realtime_playing || rate < 10)) {
		      FLOAT_T n;

		      /* calculate the drastic voice reduction */
		      if(nv > kill_nv) /* Avoid division by zero */
		      {
			  n = (FLOAT_T) nv / (nv - kill_nv);
			  temp_nv = (int)(nv - nv / (n + 1));

			  /* reduce by the larger of the estimates */
			  if (kill_nv < temp_nv && temp_nv < nv)
			      kill_nv = temp_nv;
		      }
		      else kill_nv = nv - 1; /* do not kill all the voices */
		  }
		  else {
		      /* the buffer is still high enough that we can throw
		         fewer voices away; keep the ON voices, use the
		         minimum "bad" nv as a floor on voice reductions */
		      temp_nv = nv - c->min_bad_nv;
		      if (kill_nv > temp_nv)
		          kill_nv = temp_nv;
		  }

		  for(i = 0; i < kill_nv; i++)
		      reduce_voice(c);

		  /* lower max # of allowed voices to let the buffer recover */
		  if (c->auto_reduce_polyphony) {
			temp_nv = nv - kill_nv;
			c->ok_nv = c->ok_nv_total / c->ok_nv_counts;

			/* decrease it to current nv left */
			if (c->voices > temp_nv && temp_nv > c->ok_nv)
			    voice_decrement_conservative(c, c->voices - temp_nv);
			/* decrease it to ok_nv */
			else if (c->voices > c->ok_nv && temp_nv <= c->ok_nv)
			    voice_decrement_conservative(c, c->voices - c->ok_nv);
			/* increase the polyphony */
			else if (c->voices < c->ok_nv)
			    voice_increment(c, c->ok_nv - c->voices);
		  }

		  while(c->upper_voices > 0 &&
			c->voice[c->upper_voices - 1].status == VOICE_FREE)
		      c->upper_voices--;
	      }
	      last_filled = filled;
	  }
	  else {
	      if (! c->opt_realtime_playing && rate >= rate_limit &&
	          filled > last_filled) {

		    /* set bounds on "good" and "bad" nv */
		    if (rate > 85 && nv > c->max_good_nv) {
			c->max_good_nv = nv;
			if (c->min_bad_nv > c->max_good_nv)
			    c->min_bad_nv = c->max_good_nv;
		    }

		    if (c->auto_reduce_polyphony) {
			/* reset ok_nv stuff when out of danger */
			c->ok_nv_total = c->max_good_nv * c->ok_nv_counts;
			if (c->ok_nv_counts > 1) {
			    c->ok_nv_total >>= 1;
			    c->ok_nv_counts >>= 1;
			}

			/* restore max # of allowed voices to normal */
			restore_voices(c, 0);
		    }
	      }

	      last_filled = filled_limit;
          }
          c->old_rate = rate;
      }
#endif

      if(aq_add(c, c->common_buffer, audio_buffer_size) == -1)
	  return RC_ERROR;

      c->buffer_pointer = c->common_buffer;
      c->buffered_count = 0;
      if(c->current_event->type != ME_EOT)
	  ctl_timestamp(c);

      /* check break signals */
      VOLATILE_TOUCH(c->intr);
      if(c->intr)
	  return RC_QUIT;

      if(c->upper_voices == 0 && c->check_eot_flag &&
	 (i = check_midi_play_end(c->current_event, EOT_PRESEARCH_LEN)) > 0)
      {
	  if(i > 1)
	      ctl->cmsg(CMSG_INFO, VERB_VERBOSE,
			"Last %d MIDI events are ignored", i - 1);
	  return midi_play_end(c);
      }
    }
  if (count>0)
    {
      do_compute_data(c, count);
      c->buffered_count += count;
      c->buffer_pointer += (play_mode->encoding & PE_MONO) ? count : count*2;
    }
  return RC_NONE;
}
#undef last_filled

static void update_modulation_wheel(struct timiditycontext_t *c, int ch)
{
    int i, uv = c->upper_voices;
	c->channel[ch].pitchfactor = 0;
    for(i = 0; i < uv; i++)
	if(c->voice[i].status != VOICE_FREE && c->voice[i].channel == ch)
	{
	    /* Set/Reset mod-wheel */
		c->voice[i].vibrato_control_counter = c->voice[i].vibrato_phase = 0;
	    recompute_amp(c, i);
		apply_envelope_to_amp(c, i);
	    recompute_freq(c, i);
		recompute_voice_filter(c, i);
	}
}

static void drop_portamento(struct timiditycontext_t *c, int ch)
{
    int i, uv = c->upper_voices;

    c->channel[ch].porta_control_ratio = 0;
    for(i = 0; i < uv; i++)
	if(c->voice[i].status != VOICE_FREE &&
	   c->voice[i].channel == ch &&
	   c->voice[i].porta_control_ratio)
	{
	    c->voice[i].porta_control_ratio = 0;
	    recompute_freq(c, i);
	}
    c->channel[ch].last_note_fine = -1;
}

static void update_portamento_controls(struct timiditycontext_t *c, int ch)
{
    if(!c->channel[ch].portamento ||
       (c->channel[ch].portamento_time_msb | c->channel[ch].portamento_time_lsb)
       == 0)
	drop_portamento(c, ch);
    else
    {
	double mt, dc;
	int d;

	mt = midi_time_table[c->channel[ch].portamento_time_msb & 0x7F] *
	    midi_time_table2[c->channel[ch].portamento_time_lsb & 0x7F] *
		PORTAMENTO_TIME_TUNING;
	dc = play_mode->rate * mt;
	d = (int)(1.0 / (mt * PORTAMENTO_CONTROL_RATIO));
	d++;
	c->channel[ch].porta_control_ratio = (int)(d * dc + 0.5);
	c->channel[ch].porta_dpb = d;
    }
}

static void update_portamento_time(struct timiditycontext_t *c, int ch)
{
    int i, uv = c->upper_voices;
    int dpb;
    int32 ratio;

    update_portamento_controls(c, ch);
    dpb = c->channel[ch].porta_dpb;
    ratio = c->channel[ch].porta_control_ratio;

    for(i = 0; i < uv; i++)
    {
	if(c->voice[i].status != VOICE_FREE &&
	   c->voice[i].channel == ch &&
	   c->voice[i].porta_control_ratio)
	{
	    c->voice[i].porta_control_ratio = ratio;
	    c->voice[i].porta_dpb = dpb;
	    recompute_freq(c, i);
	}
    }
}

static void update_legato_controls(struct timiditycontext_t *c, int ch)
{
	double mt, dc;
	int d;

	mt = 0.06250 * PORTAMENTO_TIME_TUNING * 0.3;
	dc = play_mode->rate * mt;
	d = (int)(1.0 / (mt * PORTAMENTO_CONTROL_RATIO));
	d++;
	c->channel[ch].porta_control_ratio = (int)(d * dc + 0.5);
	c->channel[ch].porta_dpb = d;
}

int play_event(struct timiditycontext_t *c, MidiEvent *ev)
{
    int32 i, j, cet;
    int k, l, ch, orig_ch, port_ch, offset, layered;

    if(play_mode->flag & PF_MIDI_EVENT)
	return play_mode->acntl(PM_REQ_MIDI, ev);
    if(!(play_mode->flag & PF_PCM_STREAM))
	return RC_NONE;

    c->current_event = ev;
    cet = MIDI_EVENT_TIME(ev);

    if(ctl->verbosity >= VERB_DEBUG_SILLY)
	ctl->cmsg(CMSG_INFO, VERB_DEBUG_SILLY,
		  "Midi Event %d: %s %d %d %d", cet,
		  event_name(ev->type), ev->channel, ev->a, ev->b);
    if(cet > c->current_sample)
    {
	int rc;

#if ! defined(IA_WINSYN) && ! defined(IA_PORTMIDISYN) && ! defined(IA_W32G_SYN)
	if (c->midi_streaming != 0)
		if ((cet - c->current_sample) * 1000 / play_mode->rate
				> c->stream_max_compute) {
			kill_all_voices(c);
			/* reset_voices(c); */
			/* ctl->cmsg(CMSG_INFO, VERB_DEBUG_SILLY,
					"play_event: discard %d samples", cet - c->current_sample); */
			c->current_sample = cet;
		}
#endif

	rc = compute_data(c, cet - c->current_sample);
	ctl_mode_event(c, CTLE_REFRESH, 0, 0, 0);
    if(rc == RC_JUMP)
	{
		ctl_timestamp(c);
		return RC_NONE;
	}
	if(rc != RC_NONE)
	    return rc;
	}

#ifndef SUPPRESS_CHANNEL_LAYER
	orig_ch = ev->channel;
	layered = ! IS_SYSEX_EVENT_TYPE(ev);
	for (k = 0; k < MAX_CHANNELS; k += 16) {
		port_ch = (orig_ch + k) % MAX_CHANNELS;
		offset = port_ch & ~0xf;
		for (l = offset; l < offset + 16; l++) {
			if (! layered && (k || l != offset))
				continue;
			if (layered) {
				if (! IS_SET_CHANNELMASK(c->channel[l].channel_layer, port_ch)
						|| c->channel[l].port_select != (orig_ch >> 4))
					continue;
				ev->channel = l;
			}
#endif
	ch = ev->channel;

    switch(ev->type)
    {
	/* MIDI Events */
      case ME_NOTEOFF:
	note_off(c, ev);
	break;

      case ME_NOTEON:
	note_on(c, ev);
	break;

      case ME_KEYPRESSURE:
	adjust_pressure(c, ev);
	break;

      case ME_PROGRAM:
	midi_program_change(c, ch, ev->a);
	ctl_prog_event(c, ch);
	break;

      case ME_CHANNEL_PRESSURE:
	adjust_channel_pressure(c, ev);
	break;

      case ME_PITCHWHEEL:
	c->channel[ch].pitchbend = ev->a + ev->b * 128;
	c->channel[ch].pitchfactor = 0;
	/* Adjust pitch for notes already playing */
	adjust_pitch(c, ch);
	ctl_mode_event(c, CTLE_PITCH_BEND, 1, ch, c->channel[ch].pitchbend);
	break;

	/* Controls */
      case ME_TONE_BANK_MSB:
	c->channel[ch].bank_msb = ev->a;
	break;

      case ME_TONE_BANK_LSB:
	c->channel[ch].bank_lsb = ev->a;
	break;

      case ME_MODULATION_WHEEL:
	c->channel[ch].mod.val = ev->a;
	update_modulation_wheel(c, ch);
	ctl_mode_event(c, CTLE_MOD_WHEEL, 1, ch, c->channel[ch].mod.val);
	break;

      case ME_MAINVOLUME:
	c->channel[ch].volume = ev->a;
	adjust_volume(c, ch);
	ctl_mode_event(c, CTLE_VOLUME, 1, ch, ev->a);
	break;

      case ME_PAN:
	c->channel[ch].panning = ev->a;
	c->channel[ch].pan_random = 0;
	if(c->adjust_panning_immediately && !c->channel[ch].pan_random)
	    adjust_panning(c, ch);
	ctl_mode_event(c, CTLE_PANNING, 1, ch, ev->a);
	break;

      case ME_EXPRESSION:
	c->channel[ch].expression = ev->a;
	adjust_volume(c, ch);
	ctl_mode_event(c, CTLE_EXPRESSION, 1, ch, ev->a);
	break;

      case ME_SUSTAIN:
    if (c->channel[ch].sustain == 0 && ev->a >= 64) {
		update_redamper_controls(c, ch);
	}
	c->channel[ch].sustain = ev->a;
	if (c->channel[ch].damper_mode == 0) {	/* half-damper is not allowed. */
		if (c->channel[ch].sustain >= 64) {c->channel[ch].sustain = 127;}
		else {c->channel[ch].sustain = 0;}
	}
	if(c->channel[ch].sustain == 0 && c->channel[ch].sostenuto == 0)
	    drop_sustain(c, ch);
	ctl_mode_event(c, CTLE_SUSTAIN, 1, ch, c->channel[ch].sustain);
	break;

      case ME_SOSTENUTO:
	c->channel[ch].sostenuto = (ev->a >= 64);
	if(c->channel[ch].sustain == 0 && c->channel[ch].sostenuto == 0)
	    drop_sustain(c, ch);
	else {update_sostenuto_controls(c, ch);}
	ctl->cmsg(CMSG_INFO, VERB_NOISY, "Sostenuto %d", c->channel[ch].sostenuto);
	break;

      case ME_LEGATO_FOOTSWITCH:
    c->channel[ch].legato = (ev->a >= 64);
	ctl->cmsg(CMSG_INFO,VERB_NOISY,"Legato Footswitch (CH:%d VAL:%d)", ch, c->channel[ch].legato);
	break;

      case ME_HOLD2:
	ctl->cmsg(CMSG_INFO,VERB_NOISY,"Hold2 - this function is not supported.");
	break;

      case ME_BREATH:
	ctl->cmsg(CMSG_INFO,VERB_NOISY,"Breath - this function is not supported.");
	break;

      case ME_FOOT:
	ctl->cmsg(CMSG_INFO,VERB_NOISY,"Foot - this function is not supported.");
	break;

      case ME_BALANCE:
	ctl->cmsg(CMSG_INFO,VERB_NOISY,"Balance - this function is not supported.");
	break;

      case ME_PORTAMENTO_TIME_MSB:
	c->channel[ch].portamento_time_msb = ev->a;
	update_portamento_time(c, ch);
	break;

      case ME_PORTAMENTO_TIME_LSB:
	c->channel[ch].portamento_time_lsb = ev->a;
	update_portamento_time(c, ch);
	break;

      case ME_PORTAMENTO:
	c->channel[ch].portamento = (ev->a >= 64);
	if(!c->channel[ch].portamento)
	    drop_portamento(c, ch);
	break;

	  case ME_SOFT_PEDAL:
		  if(c->opt_lpf_def) {
			  c->channel[ch].soft_pedal = ev->a;
			  ctl->cmsg(CMSG_INFO,VERB_NOISY,"Soft Pedal (CH:%d VAL:%d)",ch,c->channel[ch].soft_pedal);
		  }
		  break;

	  case ME_HARMONIC_CONTENT:
		  if(c->opt_lpf_def) {
			  c->channel[ch].param_resonance = ev->a - 64;
			  ctl->cmsg(CMSG_INFO,VERB_NOISY,"Harmonic Content (CH:%d VAL:%d)",ch,c->channel[ch].param_resonance);
		  }
		  break;

	  case ME_BRIGHTNESS:
		  if(c->opt_lpf_def) {
			  c->channel[ch].param_cutoff_freq = ev->a - 64;
			  ctl->cmsg(CMSG_INFO,VERB_NOISY,"Brightness (CH:%d VAL:%d)",ch,c->channel[ch].param_cutoff_freq);
		  }
		  break;

      case ME_DATA_ENTRY_MSB:
	if(c->channel[ch].rpn_7f7f_flag) /* disable */
	    break;
	if((i = last_rpn_addr(c, ch)) >= 0)
	{
	    c->channel[ch].rpnmap[i] = ev->a;
	    update_rpn_map(c, ch, i, 1);
	}
	break;

      case ME_DATA_ENTRY_LSB:
	if(c->channel[ch].rpn_7f7f_flag) /* disable */
	    break;
	if((i = last_rpn_addr(c, ch)) >= 0)
	{
	    c->channel[ch].rpnmap_lsb[i] = ev->a;
	}
	break;

	case ME_REVERB_EFFECT:
		if (c->opt_reverb_control) {
			if (ISDRUMCHANNEL(ch) && get_reverb_level(c, ch) != ev->a) {c->channel[ch].drum_effect_flag = 0;}
			set_reverb_level(c, ch, ev->a);
			ctl_mode_event(c, CTLE_REVERB_EFFECT, 1, ch, get_reverb_level(c, ch));
		}
		break;

      case ME_CHORUS_EFFECT:
	if(c->opt_chorus_control)
	{
		if(c->opt_chorus_control == 1) {
			if (ISDRUMCHANNEL(ch) && c->channel[ch].chorus_level != ev->a) {c->channel[ch].drum_effect_flag = 0;}
			c->channel[ch].chorus_level = ev->a;
		} else {
			c->channel[ch].chorus_level = -c->opt_chorus_control;
		}
	    ctl_mode_event(c, CTLE_CHORUS_EFFECT, 1, ch, get_chorus_level(c, ch));
		if(ev->a) {
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Chorus Send (CH:%d LEVEL:%d)",ch,ev->a);
		}
	}
	break;

      case ME_TREMOLO_EFFECT:
	ctl->cmsg(CMSG_INFO,VERB_NOISY,"Tremolo Send (CH:%d LEVEL:%d)",ch,ev->a);
	break;

      case ME_CELESTE_EFFECT:
	if(c->opt_delay_control) {
		if (ISDRUMCHANNEL(ch) && c->channel[ch].delay_level != ev->a) {c->channel[ch].drum_effect_flag = 0;}
		c->channel[ch].delay_level = ev->a;
		if (c->play_system_mode == XG_SYSTEM_MODE) {
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Variation Send (CH:%d LEVEL:%d)",ch,ev->a);
		} else {
			ctl->cmsg(CMSG_INFO,VERB_NOISY,"Delay Send (CH:%d LEVEL:%d)",ch,ev->a);
		}
	}
	break;

	  case ME_ATTACK_TIME:
	if(!c->opt_tva_attack) { break; }
	set_envelope_time(c, ch, ev->a, EG_ATTACK);
	break;

	  case ME_RELEASE_TIME:
	if(!c->opt_tva_release) { break; }
	set_envelope_time(c, ch, ev->a, EG_RELEASE);
	break;

      case ME_PHASER_EFFECT:
	ctl->cmsg(CMSG_INFO,VERB_NOISY,"Phaser Send (CH:%d LEVEL:%d)",ch,ev->a);
	break;

      case ME_RPN_INC:
	if(c->channel[ch].rpn_7f7f_flag) /* disable */
	    break;
	if((i = last_rpn_addr(c, ch)) >= 0)
	{
	    if(c->channel[ch].rpnmap[i] < 127)
		c->channel[ch].rpnmap[i]++;
	    update_rpn_map(c, ch, i, 1);
	}
	break;

      case ME_RPN_DEC:
	if(c->channel[ch].rpn_7f7f_flag) /* disable */
	    break;
	if((i = last_rpn_addr(c, ch)) >= 0)
	{
	    if(c->channel[ch].rpnmap[i] > 0)
		c->channel[ch].rpnmap[i]--;
	    update_rpn_map(c, ch, i, 1);
	}
	break;

      case ME_NRPN_LSB:
	c->channel[ch].lastlrpn = ev->a;
	c->channel[ch].nrpn = 1;
	break;

      case ME_NRPN_MSB:
	c->channel[ch].lastmrpn = ev->a;
	c->channel[ch].nrpn = 1;
	break;

      case ME_RPN_LSB:
	c->channel[ch].lastlrpn = ev->a;
	c->channel[ch].nrpn = 0;
	break;

      case ME_RPN_MSB:
	c->channel[ch].lastmrpn = ev->a;
	c->channel[ch].nrpn = 0;
	break;

      case ME_ALL_SOUNDS_OFF:
	all_sounds_off(c, ch);
	break;

      case ME_RESET_CONTROLLERS:
	reset_controllers(c, ch);
	redraw_controllers(c, ch);
	break;

      case ME_ALL_NOTES_OFF:
	all_notes_off(c, ch);
	break;

      case ME_MONO:
	c->channel[ch].mono = 1;
	all_notes_off(c, ch);
	break;

      case ME_POLY:
	c->channel[ch].mono = 0;
	all_notes_off(c, ch);
	break;

	/* TiMidity Extensionals */
      case ME_RANDOM_PAN:
	c->channel[ch].panning = int_rand(128);
	c->channel[ch].pan_random = 1;
	if(c->adjust_panning_immediately && !c->channel[ch].pan_random)
	    adjust_panning(c, ch);
	break;

      case ME_SET_PATCH:
	i = c->channel[ch].special_sample = c->current_event->a;
	if(c->special_patch[i] != NULL)
	    c->special_patch[i]->sample_offset = 0;
	ctl_prog_event(c, ch);
	break;

      case ME_TEMPO:
	c->current_play_tempo = ch + ev->b * 256 + ev->a * 65536;
	ctl_mode_event(c, CTLE_TEMPO, 1, c->current_play_tempo, 0);
	break;

      case ME_CHORUS_TEXT:
      case ME_LYRIC:
      case ME_MARKER:
      case ME_INSERT_TEXT:
      case ME_TEXT:
      case ME_KARAOKE_LYRIC:
	i = ev->a | ((int)ev->b << 8);
	ctl_mode_event(c, CTLE_LYRIC, 1, i, ev->time);
	break;

      case ME_GSLCD:
	i = ev->a | ((int)ev->b << 8);
	ctl_mode_event(c, CTLE_GSLCD, 1, i, 0);
	break;

      case ME_MASTER_VOLUME:
	c->master_volume_ratio = (int32)ev->a + 256 * (int32)ev->b;
	adjust_master_volume(c);
	break;

      case ME_RESET:
	change_system_mode(c, ev->a);
	reset_midi(c, 1);
	break;

      case ME_PATCH_OFFS:
	i = c->channel[ch].special_sample;
	if(c->special_patch[i] != NULL)
	    c->special_patch[i]->sample_offset =
		(c->current_event->a | 256 * c->current_event->b);
	break;

      case ME_WRD:
	push_midi_trace2(c, wrd_midi_event,
			 ch, c->current_event->a | (c->current_event->b << 8));
	break;

      case ME_SHERRY:
	push_midi_trace1(c, wrd_sherry_event,
			 ch | (c->current_event->a<<8) | (c->current_event->b<<16));
	break;

      case ME_DRUMPART:
	if(midi_drumpart_change(c, ch, c->current_event->a))
	{
	    /* Update bank information */
	    midi_program_change(c, ch, c->channel[ch].program);
	    ctl_mode_event(c, CTLE_DRUMPART, 1, ch, ISDRUMCHANNEL(ch));
	    ctl_prog_event(c, ch);
	}
	break;

      case ME_KEYSHIFT:
	i = (int)c->current_event->a - 0x40;
	if(i != c->channel[ch].key_shift)
	{
	    all_sounds_off(c, ch);
	    c->channel[ch].key_shift = (int8)i;
	}
	break;

	case ME_KEYSIG:
		if (c->opt_init_keysig != 8)
			break;
		c->current_keysig = c->current_event->a + c->current_event->b * 16;
		ctl_mode_event(c, CTLE_KEYSIG, 1, c->current_keysig, 0);
		if (c->opt_force_keysig != 8) {
			i = c->current_keysig - ((c->current_keysig < 8) ? 0 : 16), j = 0;
			while (i != c->opt_force_keysig && i != c->opt_force_keysig + 12)
				i += (i > 0) ? -5 : 7, j++;
			while (abs(j - c->note_key_offset) > 7)
				j += (j > c->note_key_offset) ? -12 : 12;
			if (abs(j - c->key_adjust) >= 12)
				j += (j > c->key_adjust) ? -12 : 12;
			c->note_key_offset = j;
			kill_all_voices(c);
			ctl_mode_event(c, CTLE_KEY_OFFSET, 1, c->note_key_offset, 0);
		}
		i = c->current_keysig + ((c->current_keysig < 8) ? 7 : -9), j = 0;
		while (i != 7)
			i += (i < 7) ? 5 : -7, j++;
		j += c->note_key_offset, j -= floor(j / 12.0) * 12;
		c->current_freq_table = j;
		break;

	case ME_MASTER_TUNING:
		set_master_tuning(c, (ev->b << 8) | ev->a);
		adjust_all_pitch(c);
		break;

	case ME_SCALE_TUNING:
		resamp_cache_refer_alloff(c, ch, c->current_event->time);
		c->channel[ch].scale_tuning[c->current_event->a] = c->current_event->b;
		adjust_pitch(c, ch);
		break;

	case ME_BULK_TUNING_DUMP:
		set_single_note_tuning(c, ch, c->current_event->a, c->current_event->b, 0);
		break;

	case ME_SINGLE_NOTE_TUNING:
		set_single_note_tuning(c, ch, c->current_event->a, c->current_event->b, 1);
		break;

	case ME_TEMPER_KEYSIG:
		c->current_temper_keysig = (c->current_event->a + 8) % 32 - 8;
		c->temper_adj = ((c->current_event->a + 8) & 0x20) ? 1 : 0;
		ctl_mode_event(c, CTLE_TEMPER_KEYSIG, 1, c->current_event->a, 0);
		i = c->current_temper_keysig + ((c->current_temper_keysig < 8) ? 7 : -9);
		j = 0;
		while (i != 7)
			i += (i < 7) ? 5 : -7, j++;
		j += c->note_key_offset, j -= floor(j / 12.0) * 12;
		c->current_temper_freq_table = j;
		if (c->current_event->b)
			for (i = 0; i < c->upper_voices; i++)
				if (c->voice[i].status != VOICE_FREE) {
					c->voice[i].temper_instant = 1;
					recompute_freq(c, i);
				}
		break;

	case ME_TEMPER_TYPE:
		c->channel[ch].temper_type = c->current_event->a;
		ctl_mode_event(c, CTLE_TEMPER_TYPE, 1, ch, c->channel[ch].temper_type);
		if (c->temper_type_mute) {
			if (c->temper_type_mute & (1 << (c->current_event->a
					- ((c->current_event->a >= 0x40) ? 0x3c : 0)))) {
				SET_CHANNELMASK(c->channel_mute, ch);
				ctl_mode_event(c, CTLE_MUTE, 1, ch, 1);
			} else {
				UNSET_CHANNELMASK(c->channel_mute, ch);
				ctl_mode_event(c, CTLE_MUTE, 1, ch, 0);
			}
		}
		if (c->current_event->b)
			for (i = 0; i < c->upper_voices; i++)
				if (c->voice[i].status != VOICE_FREE) {
					c->voice[i].temper_instant = 1;
					recompute_freq(c, i);
				}
		break;

	case ME_MASTER_TEMPER_TYPE:
		for (i = 0; i < MAX_CHANNELS; i++) {
			c->channel[i].temper_type = c->current_event->a;
			ctl_mode_event(c, CTLE_TEMPER_TYPE, 1, i, c->channel[i].temper_type);
		}
		if (c->temper_type_mute) {
			if (c->temper_type_mute & (1 << (c->current_event->a
					- ((c->current_event->a >= 0x40) ? 0x3c : 0)))) {
				FILL_CHANNELMASK(c->channel_mute);
				for (i = 0; i < MAX_CHANNELS; i++)
					ctl_mode_event(c, CTLE_MUTE, 1, i, 1);
			} else {
				CLEAR_CHANNELMASK(c->channel_mute);
				for (i = 0; i < MAX_CHANNELS; i++)
					ctl_mode_event(c, CTLE_MUTE, 1, i, 0);
			}
		}
		if (c->current_event->b)
			for (i = 0; i < c->upper_voices; i++)
				if (c->voice[i].status != VOICE_FREE) {
					c->voice[i].temper_instant = 1;
					recompute_freq(c, i);
				}
		break;

	case ME_USER_TEMPER_ENTRY:
		set_user_temper_entry(c, ch, c->current_event->a, c->current_event->b);
		break;

	case ME_SYSEX_LSB:
		process_sysex_event(c, ME_SYSEX_LSB,ch,c->current_event->a,c->current_event->b);
	    break;

	case ME_SYSEX_MSB:
		process_sysex_event(c, ME_SYSEX_MSB,ch,c->current_event->a,c->current_event->b);
	    break;

	case ME_SYSEX_GS_LSB:
		process_sysex_event(c, ME_SYSEX_GS_LSB,ch,c->current_event->a,c->current_event->b);
	    break;

	case ME_SYSEX_GS_MSB:
		process_sysex_event(c, ME_SYSEX_GS_MSB,ch,c->current_event->a,c->current_event->b);
	    break;

	case ME_SYSEX_XG_LSB:
		process_sysex_event(c, ME_SYSEX_XG_LSB,ch,c->current_event->a,c->current_event->b);
	    break;

	case ME_SYSEX_XG_MSB:
		process_sysex_event(c, ME_SYSEX_XG_MSB,ch,c->current_event->a,c->current_event->b);
	    break;

	case ME_NOTE_STEP:
		i = ev->a + ((ev->b & 0x0f) << 8);
		j = ev->b >> 4;
		ctl_mode_event(c, CTLE_METRONOME, 1, i, j);
		if (c->readmidi_wrd_mode)
			wrdt->update_events();
		break;

	case ME_CUEPOINT:
		set_cuepoint(c, ch, c->current_event->a, c->current_event->b);
		break;

      case ME_EOT:
	return midi_play_end(c);
    }
#ifndef SUPPRESS_CHANNEL_LAYER
		}
	}
	ev->channel = orig_ch;
#endif

    return RC_NONE;
}

static void set_master_tuning(struct timiditycontext_t *c, int tune)
{
	if (tune & 0x4000)	/* 1/8192 semitones + 0x2000 | 0x4000 */
		tune = (tune & 0x3FFF) - 0x2000;
	else if (tune & 0x8000)	/* 1 semitones | 0x8000 */
		tune = ((tune & 0x7F) - 0x40) << 13;
	else	/* millisemitones + 0x400 */
		tune = (((tune - 0x400) << 13) + 500) / 1000;
	c->master_tuning = tune;
}

#define tp c->set_single_note_tuning_tp
#define kn c->set_single_note_tuning_kn
#define st c->set_single_note_tuning_st
static void set_single_note_tuning(struct timiditycontext_t *c, int part, int a, int b, int rt)
{
	double f, fst;	/* fraction of semitone */
	int i;

	switch (part) {
	case 0:
		tp = a;
		break;
	case 1:
		kn = a, st = b;
		break;
	case 2:
		if (st == 0x7f && a == 0x7f && b == 0x7f)	/* no change */
			break;
		f = 440 * pow(2.0, (st - 69) / 12.0);
		fst = pow(2.0, (a << 7 | b) / 196608.0);
		c->freq_table_tuning[tp][kn] = f * fst * 1000 + 0.5;
		if (rt)
			for (i = 0; i < c->upper_voices; i++)
				if (c->voice[i].status != VOICE_FREE) {
					c->voice[i].temper_instant = 1;
					recompute_freq(c, i);
				}
		break;
	}
}
#undef tp
#undef kn
#undef st

#define tp c->set_user_temper_entry_tp
#define ll c->set_user_temper_entry_ll
#define fh c->set_user_temper_entry_fh
#define fl c->set_user_temper_entry_fl
#define bh c->set_user_temper_entry_bh
#define bl c->set_user_temper_entry_bl
#define aa c->set_user_temper_entry_aa
#define bb c->set_user_temper_entry_bb
#define cc c->set_user_temper_entry_cc
#define dd c->set_user_temper_entry_dd
#define ee c->set_user_temper_entry_ee
#define ff c->set_user_temper_entry_ff
#define ifmax c->set_user_temper_entry_ifmax
#define ibmax c->set_user_temper_entry_ibmax
#define count c->set_user_temper_entry_count
#define rf c->set_user_temper_entry_rf
#define rb c->set_user_temper_entry_rb
static void set_user_temper_entry(struct timiditycontext_t *c, int part, int a, int b)
{
	int i, j, k, l, n, m;
	double ratio[12], f, sc;

	switch (part) {
	case 0:
		for (i = 0; i < 11; i++)
			rf[i] = rb[i] = 1;
		ifmax = ibmax = 0;
		count = 0;
		tp = a, ll = b;
		break;
	case 1:
		fh = a, fl = b;
		break;
	case 2:
		bh = a, bl = b;
		break;
	case 3:
		aa = a, bb = b;
		break;
	case 4:
		cc = a, dd = b;
		break;
	case 5:
		ee = a, ff = b;
		for (i = 0; i < 11; i++) {
			if (((fh & 0xf) << 7 | fl) & 1 << i) {
				rf[i] *= (double) aa / bb
						* pow((double) cc / dd, (double) ee / ff);
				if (ifmax < i + 1)
					ifmax = i + 1;
			}
			if (((bh & 0xf) << 7 | bl) & 1 << i) {
				rb[i] *= (double) aa / bb
						* pow((double) cc / dd, (double) ee / ff);
				if (ibmax < i + 1)
					ibmax = i + 1;
			}
		}
		if (++count < ll)
			break;
		ratio[0] = 1;
		for (i = n = m = 0; i < ifmax; i++, m = n) {
			n += (n > 4) ? -5 : 7;
			ratio[n] = ratio[m] * rf[i];
			if (ratio[n] > 2)
				ratio[n] /= 2;
		}
		for (i = n = m = 0; i < ibmax; i++, m = n) {
			n += (n > 6) ? -7 : 5;
			ratio[n] = ratio[m] / rb[i];
			if (ratio[n] < 1)
				ratio[n] *= 2;
		}
		sc = 27 / ratio[9] / 16;	/* syntonic comma */
		for (i = 0; i < 12; i++)
			for (j = -1; j < 11; j++) {
				f = 440 * pow(2.0, (i - 9) / 12.0 + j - 5);
				for (k = 0; k < 12; k++) {
					l = i + j * 12 + k;
					if (l < 0 || l >= 128)
						continue;
					if (! (fh & 0x40)) {	/* major */
						c->freq_table_user[tp][i][l] =
								f * ratio[k] * 1000 + 0.5;
						c->freq_table_user[tp][i + 36][l] =
								f * ratio[k] * sc * 1000 + 0.5;
					}
					if (! (bh & 0x40)) {	/* minor */
						c->freq_table_user[tp][i + 12][l] =
								f * ratio[k] * sc * 1000 + 0.5;
						c->freq_table_user[tp][i + 24][l] =
								f * ratio[k] * 1000 + 0.5;
					}
				}
			}
		break;
	}
}
#undef tp
#undef ll
#undef fh
#undef fl
#undef bh
#undef bl
#undef aa
#undef bb
#undef cc
#undef dd
#undef ee
#undef ff
#undef ifmax
#undef ibmax
#undef count
#undef rf
#undef rb

#define a0 c->set_cuepoint_a0
#define b0 c->set_cuepoint_b0
static void set_cuepoint(struct timiditycontext_t *c, int part, int a, int b)
{
	if (part == 0) {
		a0 = a, b0 = b;
		return;
	}
	ctl_mode_event(c, CTLE_CUEPOINT, 1, a0 << 24 | b0 << 16 | a << 8 | b, 0);
}
#undef a0
#undef b0

#define play_count c->play_midi_play_count
static int play_midi(struct timiditycontext_t *c, MidiEvent *eventlist, int32 samples)
{
    int rc;

    if (play_mode->id_character == 'M') {
	int cnt, err;

	err = convert_mod_to_midi_file(c, eventlist);

	play_count = 0;
	cnt = free_global_mblock(c);	/* free unused memory */
	if(cnt > 0)
	    ctl->cmsg(CMSG_INFO, VERB_VERBOSE,
		      "%d memory blocks are free", cnt);
	if (err) return RC_ERROR;
	return RC_TUNE_END;
    }

    c->sample_count = samples;
    c->event_list = eventlist;
    c->lost_notes = c->cut_notes = 0;
    c->check_eot_flag = 1;

    wrd_midi_event(c, -1, -1); /* For initialize */

    reset_midi(c, 0);
    if(!c->opt_realtime_playing &&
       c->allocate_cache_size > 0 &&
       !IS_CURRENT_MOD_FILE &&
       (play_mode->flag&PF_PCM_STREAM))
    {
	play_midi_prescan(c, eventlist);
	reset_midi(c, 0);
    }

    rc = aq_flush(c, 0);
    if(RC_IS_SKIP_FILE(rc))
	return rc;

    skip_to(c, c->midi_restart_time);

    if(c->midi_restart_time > 0) { /* Need to update interface display */
      int i;
      for(i = 0; i < MAX_CHANNELS; i++)
	redraw_controllers(c, i);
    }
    rc = RC_NONE;
    for(;;)
    {
	c->midi_restart_time = 1;
	rc = play_event(c, c->current_event);
	if(rc != RC_NONE)
	    break;
	if (c->midi_restart_time)    /* don't skip the first event if == 0 */
	    c->current_event++;
    }

    if(play_count++ > 3)
    {
	int cnt;
	play_count = 0;
	cnt = free_global_mblock(c);	/* free unused memory */
	if(cnt > 0)
	    ctl->cmsg(CMSG_INFO, VERB_VERBOSE,
		      "%d memory blocks are free", cnt);
    }
    return rc;
}
#undef play_count

static void read_header_wav(struct timiditycontext_t *c, struct timidity_file* tf)
{
    char buff[44];
    tf_read(c, buff, 1, 44, tf);
}

static int read_header_aiff(struct timiditycontext_t *c, struct timidity_file* tf)
{
    char buff[5]="    ";
    int i;

    for( i=0; i<100; i++ ){
	buff[0]=buff[1]; buff[1]=buff[2]; buff[2]=buff[3];
	tf_read(c, &buff[3], 1, 1, tf);
	if( strcmp(buff,"SSND")==0 ){
            /*SSND chunk found */
	    tf_read(c, &buff[0], 1, 4, tf);
	    tf_read(c, &buff[0], 1, 4, tf);
	    ctl->cmsg(CMSG_INFO, VERB_NOISY,
		      "aiff header read OK.");
	    return 0;
	}
    }
    /*SSND chunk not found */
    return -1;
}

static int load_pcm_file_wav(struct timiditycontext_t *c)
{
    char *filename;

    if(strcmp(c->pcm_alternate_file, "auto") == 0)
    {
	filename = safe_malloc(strlen(c->current_file_info->filename)+5);
	strcpy(filename, c->current_file_info->filename);
	strcat(filename, ".wav");
    }
    else if(strlen(c->pcm_alternate_file) >= 5 &&
	    strncasecmp(c->pcm_alternate_file + strlen(c->pcm_alternate_file) - 4,
			".wav", 4) == 0)
	filename = safe_strdup(c->pcm_alternate_file);
    else
	return -1;

    ctl->cmsg(CMSG_INFO, VERB_NOISY,
		      "wav filename: %s", filename);
    c->current_file_info->pcm_tf = open_file(c, filename, 0, OF_SILENT);
    if( c->current_file_info->pcm_tf ){
	ctl->cmsg(CMSG_INFO, VERB_NOISY,
		      "open successed.");
	read_header_wav(c, c->current_file_info->pcm_tf);
	c->current_file_info->pcm_filename = filename;
	c->current_file_info->pcm_mode = PCM_MODE_WAV;
	return 0;
    }else{
	ctl->cmsg(CMSG_INFO, VERB_NOISY,
		      "open failed.");
	free(filename);
	c->current_file_info->pcm_filename = NULL;
	return -1;
    }
}

static int load_pcm_file_aiff(struct timiditycontext_t *c)
{
    char *filename;

    if(strcmp(c->pcm_alternate_file, "auto") == 0)
    {
	filename = safe_malloc(strlen(c->current_file_info->filename)+6);
	strcpy(filename, c->current_file_info->filename);
	strcat( filename, ".aiff");
    }
    else if(strlen(c->pcm_alternate_file) >= 6 &&
	    strncasecmp(c->pcm_alternate_file + strlen(c->pcm_alternate_file) - 5,
			".aiff", 5) == 0)
	filename = safe_strdup(c->pcm_alternate_file);
    else
	return -1;

    ctl->cmsg(CMSG_INFO, VERB_NOISY,
		      "aiff filename: %s", filename);
    c->current_file_info->pcm_tf = open_file(c, filename, 0, OF_SILENT);
    if( c->current_file_info->pcm_tf ){
	ctl->cmsg(CMSG_INFO, VERB_NOISY,
		      "open successed.");
	read_header_aiff(c, c->current_file_info->pcm_tf);
	c->current_file_info->pcm_filename = filename;
	c->current_file_info->pcm_mode = PCM_MODE_AIFF;
	return 0;
    }else{
	ctl->cmsg(CMSG_INFO, VERB_NOISY,
		      "open failed.");
	free(filename);
	c->current_file_info->pcm_filename = NULL;
	return -1;
    }
}

static void load_pcm_file(struct timiditycontext_t *c)
{
    if( load_pcm_file_wav(c)==0 ) return; /*load OK*/
    if( load_pcm_file_aiff(c)==0 ) return; /*load OK*/
}

static int play_midi_load_file(struct timiditycontext_t *c, char *fn,
			       MidiEvent **event,
			       int32 *nsamples)
{
    int rc;
    struct timidity_file *tf;
    int32 nevents;

    *event = NULL;

    if(!strcmp(fn, "-"))
	c->file_from_stdin = 1;
    else
	c->file_from_stdin = 0;

    ctl_mode_event(c, CTLE_NOW_LOADING, 0, (long)fn, 0);
    ctl->cmsg(CMSG_INFO, VERB_VERBOSE, "MIDI file: %s", fn);
    if((tf = open_midi_file(c, fn, 1, OF_VERBOSE)) == NULL)
    {
	ctl_mode_event(c, CTLE_LOADING_DONE, 0, -1, 0);
	return RC_ERROR;
    }

    *event = NULL;
    rc = check_apply_control(c);
    if(RC_IS_SKIP_FILE(rc))
    {
	close_file(c, tf);
	ctl_mode_event(c, CTLE_LOADING_DONE, 0, 1, 0);
	return rc;
    }

    *event = read_midi_file(c, tf, &nevents, nsamples, fn);
    close_file(c, tf);

    if(*event == NULL)
    {
	ctl_mode_event(c, CTLE_LOADING_DONE, 0, -1, 0);
	return RC_ERROR;
    }

    ctl->cmsg(CMSG_INFO, VERB_NOISY,
	      "%d supported events, %d samples, time %d:%02d",
	      nevents, *nsamples,
	      *nsamples / play_mode->rate / 60,
	      (*nsamples / play_mode->rate) % 60);

    c->current_file_info->pcm_mode = PCM_MODE_NON; /*initialize*/
    if(c->pcm_alternate_file != NULL &&
       strcmp(c->pcm_alternate_file, "none") != 0 &&
       (play_mode->flag&PF_PCM_STREAM))
	load_pcm_file(c);

    if(!IS_CURRENT_MOD_FILE &&
       (play_mode->flag&PF_PCM_STREAM))
    {
	/* FIXME: Instruments is not need for pcm_alternate_file. */

	/* Load instruments
	 * If opt_realtime_playing, the instruments will be loaded later.
	 */
	if(!c->opt_realtime_playing)
	{
	    rc = RC_NONE;
	    load_missing_instruments(c, &rc);
	    if(RC_IS_SKIP_FILE(rc))
	    {
		/* Instrument loading is terminated */
		ctl_mode_event(c, CTLE_LOADING_DONE, 0, 1, 0);
		clear_magic_instruments(c);
		return rc;
	    }
	}
    }
    else
	clear_magic_instruments(c);	/* Clear load markers */

    ctl_mode_event(c, CTLE_LOADING_DONE, 0, 0, 0);

    return RC_NONE;
}

#define last_rc c->play_midi_file_last_rc
int play_midi_file(struct timiditycontext_t *c, char *fn)
{
    int i, j, rc;
    MidiEvent *event;
    int32 nsamples;

    /* Set current file information */
    c->current_file_info = get_midi_file_info(c, fn, 1);

    rc = check_apply_control(c);
    if(RC_IS_SKIP_FILE(rc) && rc != RC_RELOAD)
	return rc;

    /* Reset key & speed each files */
    c->current_keysig = (c->opt_init_keysig == 8) ? 0 : c->opt_init_keysig;
    c->note_key_offset = c->key_adjust;
    c->midi_time_ratio = c->tempo_adjust;
	for (i = 0; i < MAX_CHANNELS; i++) {
		for (j = 0; j < 12; j++)
			c->channel[i].scale_tuning[j] = 0;
		c->channel[i].prev_scale_tuning = 0;
		c->channel[i].temper_type = 0;
	}
    CLEAR_CHANNELMASK(c->channel_mute);
	if (c->temper_type_mute & 1)
		FILL_CHANNELMASK(c->channel_mute);

    /* Reset restart offset */
    c->midi_restart_time = 0;

#ifdef REDUCE_VOICE_TIME_TUNING
    /* Reset voice reduction stuff */
    c->min_bad_nv = 256;
    c->max_good_nv = 1;
    c->ok_nv_total = 32;
    c->ok_nv_counts = 1;
    c->ok_nv = 32;
    c->ok_nv_sample = 0;
    c->old_rate = -1;
    c->reduce_quality_flag = c->no_4point_interpolation;
    restore_voices(c, 0);
#endif

	ctl_mode_event(c, CTLE_METRONOME, 0, 0, 0);
	ctl_mode_event(c, CTLE_KEYSIG, 0, c->current_keysig, 0);
	ctl_mode_event(c, CTLE_TEMPER_KEYSIG, 0, 0, 0);
	ctl_mode_event(c, CTLE_KEY_OFFSET, 0, c->note_key_offset, 0);
	i = c->current_keysig + ((c->current_keysig < 8) ? 7 : -9), j = 0;
	while (i != 7)
		i += (i < 7) ? 5 : -7, j++;
	j += c->note_key_offset, j -= floor(j / 12.0) * 12;
	c->current_freq_table = j;
	ctl_mode_event(c, CTLE_TEMPO, 0, c->current_play_tempo, 0);
	ctl_mode_event(c, CTLE_TIME_RATIO, 0, 100 / c->midi_time_ratio + 0.5, 0);
	for (i = 0; i < MAX_CHANNELS; i++) {
		ctl_mode_event(c, CTLE_TEMPER_TYPE, 0, i, c->channel[i].temper_type);
		ctl_mode_event(c, CTLE_MUTE, 0, i, c->temper_type_mute & 1);
	}
  play_reload: /* Come here to reload MIDI file */
    rc = play_midi_load_file(c, fn, &event, &nsamples);
    if(RC_IS_SKIP_FILE(rc))
	goto play_end; /* skip playing */

    init_mblock(&c->playmidi_pool);
    ctl_mode_event(c, CTLE_PLAY_START, 0, nsamples, 0);
    play_mode->acntl(PM_REQ_PLAY_START, NULL);
    rc = play_midi(c, event, nsamples);
    play_mode->acntl(PM_REQ_PLAY_END, NULL);
    ctl_mode_event(c, CTLE_PLAY_END, 0, 0, 0);
    reuse_mblock(c, &c->playmidi_pool);

    for(i = 0; i < MAX_CHANNELS; i++)
	memset(c->channel[i].drums, 0, sizeof(c->channel[i].drums));

  play_end:
    if(c->current_file_info->pcm_tf){
	close_file(c, c->current_file_info->pcm_tf);
	c->current_file_info->pcm_tf = NULL;
	free( c->current_file_info->pcm_filename );
	c->current_file_info->pcm_filename = NULL;
    }

    if(wrdt->opened)
	wrdt->end();

    if(c->free_instruments_afterwards)
    {
	int cnt;
	free_instruments(c, 0);
	cnt = free_global_mblock(c); /* free unused memory */
	if(cnt > 0)
	    ctl->cmsg(CMSG_INFO, VERB_VERBOSE, "%d memory blocks are free",
		      cnt);
    }

    free_special_patch(c, -1);

    if(event != NULL)
	free(event);
    if(rc == RC_RELOAD)
	goto play_reload;

    if(rc == RC_ERROR)
    {
	if(c->current_file_info->file_type == IS_OTHER_FILE)
	    c->current_file_info->file_type = IS_ERROR_FILE;
	if(last_rc == RC_REALLY_PREVIOUS)
	    return RC_REALLY_PREVIOUS;
    }
    last_rc = rc;
    return rc;
}
#undef last_rc

int dumb_pass_playing_list(struct timiditycontext_t *c, int number_of_files, char *list_of_files[])
{
    #ifndef CFG_FOR_SF
    int i = 0;

    for(;;)
    {
	switch(play_midi_file(c, list_of_files[i]))
	{
	  case RC_REALLY_PREVIOUS:
	    if(i > 0)
		i--;
	    break;

	  default: /* An error or something */
	  case RC_NEXT:
	    if(i < number_of_files-1)
	    {
		i++;
		break;
	    }
	    aq_flush(c, 0);

	    if(!(ctl->flags & CTLF_LIST_LOOP))
		return 0;
	    i = 0;
	    break;

	    case RC_QUIT:
		return 0;
	}
    }
    #endif
}

void default_ctl_lyric(struct timiditycontext_t *c, int lyricid)
{
    char *lyric;

    lyric = event2string(c, lyricid);
    if(lyric != NULL)
	ctl->cmsg(CMSG_TEXT, VERB_VERBOSE, "%s", lyric + 1);
}

void ctl_mode_event(struct timiditycontext_t *c, int type, int trace, ptr_size_t arg1, ptr_size_t arg2)
{
    CtlEvent ce;
    ce.type = type;
    ce.v1 = arg1;
    ce.v2 = arg2;
    if(trace && ctl->trace_playing)
	push_midi_trace_ce(c, ctl->event, &ce);
    else
	ctl->event(&ce);
}

void ctl_note_event(struct timiditycontext_t *c, int noteID)
{
    CtlEvent ce;
    ce.type = CTLE_NOTE;
    ce.v1 = c->voice[noteID].status;
    ce.v2 = c->voice[noteID].channel;
    ce.v3 = c->voice[noteID].note;
    ce.v4 = c->voice[noteID].velocity;
    if(ctl->trace_playing)
	push_midi_trace_ce(c, ctl->event, &ce);
    else
	ctl->event(&ce);
}

#define last_secs c->ctl_timestamp_last_secs
#define last_voices c->ctl_timestamp_last_voices
static void ctl_timestamp(struct timiditycontext_t *c)
{
    long i, secs, voices;
    CtlEvent ce;

    secs = (long)(c->current_sample / (c->midi_time_ratio * play_mode->rate));
    for(i = voices = 0; i < c->upper_voices; i++)
	if(c->voice[i].status != VOICE_FREE)
	    voices++;
    if(secs == last_secs && voices == last_voices)
	return;
    ce.type = CTLE_CURRENT_TIME;
    ce.v1 = last_secs = secs;
    ce.v2 = last_voices = voices;
    if(ctl->trace_playing)
	push_midi_trace_ce(c, ctl->event, &ce);
    else
	ctl->event(&ce);
}
#undef last_secs
#undef last_voices

static void ctl_updatetime(struct timiditycontext_t *c, int32 samples)
{
    long secs;
    secs = (long)(samples / (c->midi_time_ratio * play_mode->rate));
    ctl_mode_event(c, CTLE_CURRENT_TIME, 0, secs, 0);
    ctl_mode_event(c, CTLE_REFRESH, 0, 0, 0);
}

static void ctl_prog_event(struct timiditycontext_t *c, int ch)
{
    CtlEvent ce;
    int bank, prog;

    if(IS_CURRENT_MOD_FILE)
    {
	bank = 0;
	prog = c->channel[ch].special_sample;
    }
    else
    {
	bank = c->channel[ch].bank;
	prog = c->channel[ch].program;
    }

    ce.type = CTLE_PROGRAM;
    ce.v1 = ch;
    ce.v2 = prog;
    ce.v3 = (ptr_size_t)channel_instrum_name(c, ch);
    ce.v4 = (bank |
	     (c->channel[ch].bank_lsb << 8) |
	     (c->channel[ch].bank_msb << 16));
    if(ctl->trace_playing)
	push_midi_trace_ce(c, ctl->event, &ce);
    else
	ctl->event(&ce);
}

static void ctl_pause_event(struct timiditycontext_t *c, int pause, int32 s)
{
    long secs;
    secs = (long)(s / (c->midi_time_ratio * play_mode->rate));
    ctl_mode_event(c, CTLE_PAUSE, 0, pause, secs);
}

char *channel_instrum_name(struct timiditycontext_t *c, int ch)
{
    char *comm;
    int bank, prog;

    if(ISDRUMCHANNEL(ch)) {
	bank = c->channel[ch].bank;
	if (c->drumset[bank] == NULL) return "";
	prog = 0;
	comm = c->drumset[bank]->tone[prog].comment;
	if (comm == NULL) return "";
	return comm;
    }

    if(c->channel[ch].program == SPECIAL_PROGRAM)
	return "Special Program";

    if(IS_CURRENT_MOD_FILE)
    {
	int pr;
	pr = c->channel[ch].special_sample;
	if(pr > 0 &&
	   c->special_patch[pr] != NULL &&
	   c->special_patch[pr]->name != NULL)
	    return c->special_patch[pr]->name;
	return "MOD";
    }

    bank = c->channel[ch].bank;
    prog = c->channel[ch].program;
    instrument_map(c, c->channel[ch].mapID, &bank, &prog);

	if (c->tonebank[bank] == NULL) {alloc_instrument_bank(c, 0, bank);}
	if (c->tonebank[bank]->tone[prog].name) {
	    comm = c->tonebank[bank]->tone[prog].comment;
		if (comm == NULL) {comm = c->tonebank[bank]->tone[prog].name;}
	} else {
	    comm = c->tonebank[0]->tone[prog].comment;
		if (comm == NULL) {comm = c->tonebank[0]->tone[prog].name;}
	}

    return comm;
}


/*
 * For MIDI stream player.
 */
#define notfirst c->playmidi_stream_init_notfirst
void playmidi_stream_init(struct timiditycontext_t *c)
{
    int i;

    c->note_key_offset = c->key_adjust;
    c->midi_time_ratio = c->tempo_adjust;
    CLEAR_CHANNELMASK(c->channel_mute);
	if (c->temper_type_mute & 1)
		FILL_CHANNELMASK(c->channel_mute);
    c->midi_restart_time = 0;
    if(!notfirst)
    {
	notfirst = 1;
        init_mblock(&c->playmidi_pool);
	c->current_file_info = get_midi_file_info(c, "TiMidity", 1);
    c->midi_streaming=1;
    }
    else
        reuse_mblock(c, &c->playmidi_pool);

    /* Fill in current_file_info */
    c->current_file_info->readflag = 1;
    c->current_file_info->seq_name = safe_strdup("TiMidity server");
    c->current_file_info->karaoke_title = c->current_file_info->first_text = NULL;
    c->current_file_info->mid = 0x7f;
    c->current_file_info->hdrsiz = 0;
    c->current_file_info->format = 0;
    c->current_file_info->tracks = 0;
    c->current_file_info->divisions = 192; /* ?? */
    c->current_file_info->time_sig_n = 4; /* 4/ */
    c->current_file_info->time_sig_d = 4; /* /4 */
    c->current_file_info->time_sig_c = 24; /* clock */
    c->current_file_info->time_sig_b = 8;  /* q.n. */
    c->current_file_info->samples = 0;
    c->current_file_info->max_channel = MAX_CHANNELS;
    c->current_file_info->compressed = 0;
    c->current_file_info->midi_data = NULL;
    c->current_file_info->midi_data_size = 0;
    c->current_file_info->file_type = IS_OTHER_FILE;

    c->current_play_tempo = 500000;
    c->check_eot_flag = 0;

    /* Setup default drums */
	COPY_CHANNELMASK(c->current_file_info->drumchannels, c->default_drumchannels);
	COPY_CHANNELMASK(c->current_file_info->drumchannel_mask, c->default_drumchannel_mask);
    for(i = 0; i < MAX_CHANNELS; i++)
	memset(c->channel[i].drums, 0, sizeof(c->channel[i].drums));
    change_system_mode(c, DEFAULT_SYSTEM_MODE);
    reset_midi(c, 0);

    playmidi_tmr_reset(c);
}
#undef notfirst

void playmidi_tmr_reset(struct timiditycontext_t *c)
{
    int i;

    aq_flush(c, 0);
    if(ctl->id_character != 'N')
        c->current_sample = 0;
    c->buffered_count = 0;
    c->buffer_pointer = c->common_buffer;
    for(i = 0; i < MAX_CHANNELS; i++)
	c->channel[i].lasttime = 0;
}

void playmidi_stream_free(struct timiditycontext_t *c)
{
	reuse_mblock(c, &c->playmidi_pool);
}

/*! initialize Part EQ (XG) */
static void init_part_eq_xg(struct part_eq_xg *p)
{
	p->bass = 0x40;
	p->treble = 0x40;
	p->bass_freq = 0x0C;
	p->treble_freq = 0x36;
	p->valid = 0;
}

/*! recompute Part EQ (XG) */
static void recompute_part_eq_xg(struct part_eq_xg *p)
{
	int8 vbass, vtreble;

	if(p->bass_freq >= 4 && p->bass_freq <= 40 && p->bass != 0x40) {
		vbass = 1;
		p->basss.q = 0.7;
		p->basss.freq = eq_freq_table_xg[p->bass_freq];
		if(p->bass == 0) {p->basss.gain = -12.0;}
		else {p->basss.gain = 0.19 * (double)(p->bass - 0x40);}
		calc_filter_shelving_low(&(p->basss));
	} else {vbass = 0;}
	if(p->treble_freq >= 28 && p->treble_freq <= 58 && p->treble != 0x40) {
		vtreble = 1;
		p->trebles.q = 0.7;
		p->trebles.freq = eq_freq_table_xg[p->treble_freq];
		if(p->treble == 0) {p->trebles.gain = -12.0;}
		else {p->trebles.gain = 0.19 * (double)(p->treble - 0x40);}
		calc_filter_shelving_high(&(p->trebles));
	} else {vtreble = 0;}
	p->valid = vbass || vtreble;
}

static void init_midi_controller(midi_controller *p)
{
	p->val = 0;
	p->pitch = 0;
	p->cutoff = 0;
	p->amp = 0.0;
	p->lfo1_rate = p->lfo2_rate = p->lfo1_tva_depth = p->lfo2_tva_depth = 0;
	p->lfo1_pitch_depth = p->lfo2_pitch_depth = p->lfo1_tvf_depth = p->lfo2_tvf_depth = 0;
	p->variation_control_depth = p->insertion_control_depth = 0;
}

static float get_midi_controller_amp(midi_controller *p)
{
	return (1.0 + (float)p->val * (1.0f / 127.0f) * p->amp);
}

static float get_midi_controller_filter_cutoff(midi_controller *p)
{
	return ((float)p->val * (1.0f / 127.0f) * (float)p->cutoff);
}

static float get_midi_controller_filter_depth(midi_controller *p)
{
	return ((float)p->val * (1.0f / 127.0f) * (float)p->lfo1_tvf_depth);
}

static int32 get_midi_controller_pitch(midi_controller *p)
{
	return ((int32)(p->val * p->pitch) << 6);
}

static int16 get_midi_controller_pitch_depth(midi_controller *p)
{
	return (int16)((float)p->val * (float)p->lfo1_pitch_depth * (1.0f / 127.0f * 256.0 / 400.0));
}

static int16 get_midi_controller_amp_depth(midi_controller *p)
{
	return (int16)((float)p->val * (float)p->lfo1_tva_depth * (1.0f / 127.0f * 256.0));
}

static void init_rx(struct timiditycontext_t *c, int ch)
{
	c->channel[ch].rx = 0xFFFFFFFF;	/* all on */
}

static void set_rx(struct timiditycontext_t *c, int ch, int32 rx, int flag)
{
	if(ch > MAX_CHANNELS) {return;}
	if(flag) {c->channel[ch].rx |= rx;}
	else {c->channel[ch].rx &= ~rx;}
}

#if 0
static int32 get_rx(struct timiditycontext_t *c, int ch, int32 rx)
{
	return (c->channel[ch].rx & rx);
}
#endif

static void init_rx_drum(struct DrumParts *p)
{
	p->rx = 0xFFFFFFFF;	/* all on */
}

static void set_rx_drum(struct DrumParts *p, int32 rx, int flag)
{
	if(flag) {p->rx |= rx;}
	else {p->rx &= ~rx;}
}

static int32 get_rx_drum(struct DrumParts *p, int32 rx)
{
	return (p->rx & rx);
}

void free_reverb_buffer(struct timiditycontext_t *c)
{
	free(c->reverb_buffer);
	c->reverb_buffer = NULL;
}
