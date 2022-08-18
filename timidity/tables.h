/*
    TiMidity++ -- MIDI to WAVE converter and player
    Copyright (C) 1999-2004 Masanao Izumo <iz@onicos.co.jp>
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

    tables.h
*/

#ifndef ___TABLES_H_
#define ___TABLES_H_

#ifdef LOOKUP_SINE
extern FLOAT_T lookup_sine(int x);
#else
#include <math.h>
#define lookup_sine(x) (sin((2*M_PI/1024.0) * (x)))
#endif
extern FLOAT_T lookup_triangular(struct timiditycontext_t *c, int x);
extern FLOAT_T lookup_log(int x);

#define SINE_CYCLE_LENGTH 1024
extern const FLOAT_T midi_time_table[], midi_time_table2[];
#ifdef LOOKUP_HACK
extern const int16 _u2l[]; /* 13-bit PCM to 8-bit u-law */
extern const uint8 _l2u_[]; /* used in LOOKUP_HACK */
#endif
extern const uint8 reverb_macro_presets[];
extern const uint8 chorus_macro_presets[];
extern const uint8 delay_macro_presets[];
extern const float delay_time_center_table[];
extern const float pre_delay_time_table[];
extern const float chorus_delay_time_table[];
extern const float rate1_table[];
extern const float sc_eg_attack_table[];
extern const float sc_eg_decay_table[];
extern const float sc_eg_release_table[];
extern const FLOAT_T sc_vel_table[];
extern const FLOAT_T sc_vol_table[];
extern const FLOAT_T sc_pan_table[];
extern const FLOAT_T sc_drum_level_table[];
extern const float cb_to_amp_table[];
extern const float reverb_time_table[];
extern const float pan_delay_table[];
extern const float chamberlin_filter_db_to_q_table[];
extern const uint8 multi_eq_block_table_xg[];
extern const float eq_freq_table_xg[];
extern const float lfo_freq_table_xg[];
extern const float mod_delay_offset_table_xg[];
extern const float reverb_time_table_xg[];
extern const float delay_time_table_xg[];
extern const int16 cutoff_freq_table_gs[];
extern const int16 lpf_table_gs[];
extern const int16 eq_freq_table_gs[];
extern const float lofi_sampling_freq_table_xg[];

extern void init_freq_table(struct timiditycontext_t *c);
extern void init_freq_table_tuning(struct timiditycontext_t *c);
extern void init_freq_table_pytha(struct timiditycontext_t *c);
extern void init_freq_table_meantone(struct timiditycontext_t *c);
extern void init_freq_table_pureint(struct timiditycontext_t *c);
extern void init_freq_table_user(struct timiditycontext_t *c);
extern void init_bend_fine(struct timiditycontext_t *c);
extern void init_bend_coarse(struct timiditycontext_t *c);
extern void init_tables(struct timiditycontext_t *c);
extern void init_gm2_pan_table(struct timiditycontext_t *c);
extern void init_attack_vol_table(struct timiditycontext_t *c);
extern void init_sb_vol_table(struct timiditycontext_t *c);
extern void init_modenv_vol_table(struct timiditycontext_t *c);
extern void init_def_vol_table(struct timiditycontext_t *c);
extern void init_gs_vol_table(struct timiditycontext_t *c);
extern void init_perceived_vol_table(struct timiditycontext_t *c);
extern void init_gm2_vol_table(struct timiditycontext_t *c);
extern void init_user_vol_table(struct timiditycontext_t *c, FLOAT_T power);

#endif /* ___TABLES_H_ */
