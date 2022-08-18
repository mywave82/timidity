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

    resample.h
*/

#ifndef ___RESAMPLE_H_
#define ___RESAMPLE_H_

#ifdef HAVE_CONFIH_H
#include "config.h"
#endif

typedef int32 resample_t;

enum {
	RESAMPLE_CSPLINE,
	RESAMPLE_LAGRANGE,
	RESAMPLE_GAUSS,
	RESAMPLE_NEWTON,
	RESAMPLE_LINEAR,
	RESAMPLE_NONE
};

extern int get_current_resampler(struct timiditycontext_t *c);
extern int set_current_resampler(struct timiditycontext_t *c, int type);
extern void initialize_resampler_coeffs(struct timiditycontext_t *c);
extern int set_resampler_parm(struct timiditycontext_t *c, int val);
extern void free_gauss_table(struct timiditycontext_t *c);

typedef struct resample_rec {
	splen_t loop_start;
	splen_t loop_end;
	splen_t data_length;
} resample_rec_t;

extern resample_t do_resamplation(struct timiditycontext_t *c, sample_t *src, splen_t ofs, resample_rec_t *rec);

extern resample_t *resample_voice(struct timiditycontext_t *c, int v, int32 *countptr);
extern void pre_resample(struct timiditycontext_t *c, Sample *sp);

#define DEFAULT_GAUSS_ORDER	25

typedef resample_t (*resampler_t)(struct timiditycontext_t *c, sample_t*, splen_t, resample_rec_t *);

#ifndef FIXED_RESAMPLATION
resample_t DEFAULT_RESAMPLATION (struct timiditycontext_t *c, sample_t *src, splen_t ofs, resample_rec_t *rec);
#endif


#endif /* ___RESAMPLE_H_ */
