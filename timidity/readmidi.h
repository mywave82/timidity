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

    readmidi.h
*/

#ifndef ___READMIDI_H_
#define ___READMIDI_H_

#include "reverb.h"

struct timiditycontext_t;

/* MIDI file types */
#define IS_ERROR_FILE	-1	/* Error file */
#define IS_OTHER_FILE	0	/* Not a MIDI file */
#define IS_SMF_FILE	101	/* Standard MIDI File */
#define IS_MCP_FILE	201	/* MCP */
#define IS_RCP_FILE	202	/* Recomposer */
#define IS_R36_FILE	203	/* Recomposer */
#define IS_G18_FILE	204	/* Recomposer */
#define IS_G36_FILE	205	/* Recomposer */
#define IS_SNG_FILE	301
#define IS_MM2_FILE	401
#define IS_MML_FILE	501
#define IS_FM_FILE	601
#define IS_FPD_FILE	602
#define IS_MOD_FILE	701	/* Pro/Fast/Star/Noise Tracker */
#define IS_669_FILE	702	/* Composer 669, UNIS669 */
#define IS_MTM_FILE	703	/* MultiModuleEdit */
#define IS_STM_FILE	704	/* ScreamTracker 2 */
#define IS_S3M_FILE	705	/* ScreamTracker 3 */
#define IS_ULT_FILE	706	/* UltraTracker */
#define IS_XM_FILE	707	/* FastTracker 2 */
#define IS_FAR_FILE	708	/* Farandole Composer */
#define IS_WOW_FILE	709	/* Grave Composer */
#define IS_OKT_FILE	710	/* Oktalyzer */
#define IS_DMF_FILE	711	/* X-Tracker */
#define IS_MED_FILE	712	/* MED/OctaMED */
#define IS_IT_FILE	713	/* Impulse Tracker */
#define IS_PTM_FILE	714	/* Poly Tracker */
#define IS_MFI_FILE	800	/* Melody Format for i-mode */

#define REDUCE_CHANNELS		16

enum play_system_modes
{
    DEFAULT_SYSTEM_MODE,
    GM_SYSTEM_MODE,
    GM2_SYSTEM_MODE,
    GS_SYSTEM_MODE,
    XG_SYSTEM_MODE
};

enum {
    PCM_MODE_NON = 0,
    PCM_MODE_WAV,
    PCM_MODE_AIFF,
    PCM_MODE_AU,
    PCM_MODE_MP3
};

#define IS_CURRENT_MOD_FILE \
	(c->current_file_info && \
	c->current_file_info->file_type >= 700 && \
	c->current_file_info->file_type < 800)

typedef struct {
  MidiEvent event;
  void *next;
  void *prev;
} MidiEventList;

struct midi_file_info
{
    int readflag;
    char *filename;
    char *seq_name;
    char *karaoke_title;
    char *first_text;
    uint8 mid;	/* Manufacture ID (0x41/Roland, 0x43/Yamaha, etc...) */
    int16 hdrsiz;
    int16 format;
    int16 tracks;
    int32 divisions;
    int time_sig_n, time_sig_d, time_sig_c, time_sig_b;	/* Time signature */
    int drumchannels_isset;
    ChannelBitMask drumchannels;
    ChannelBitMask drumchannel_mask;
    int32 samples;
    int max_channel;
    struct midi_file_info *next;
    int compressed; /* True if midi_data is compressed */
    char *midi_data;
    int32 midi_data_size;
    int file_type;

    int pcm_mode;
    char *pcm_filename;
    struct timidity_file *pcm_tf;
};

typedef struct {
	int meas;
	int beat;
} Measure;

typedef struct _TimeSegment {
	int type;	/* seconds: 0, measure: 1 */
	union {
		FLOAT_T s;
		Measure m;
	} begin;
	union {
		FLOAT_T s;
		Measure m;
	} end;
	struct _TimeSegment *prev;
	struct _TimeSegment *next;
} TimeSegment;

extern int32 readmidi_set_track(struct timiditycontext_t *c, int trackno, int rewindp);
extern void readmidi_add_event(struct timiditycontext_t *c, MidiEvent *newev);
extern void readmidi_add_ctl_event(struct timiditycontext_t *c, int32 at, int ch, int control, int val);
extern int parse_sysex_event(struct timiditycontext_t *c, uint8 *data, int32 datalen, MidiEvent *ev_ret);
extern int parse_sysex_event_multi(struct timiditycontext_t *c, uint8 *data, int32 datalen, MidiEvent *ev_ret);
extern int convert_midi_control_change(int chn, int type, int val,
				       MidiEvent *ev_ret);
extern int unconvert_midi_control_change(MidiEvent *ev);
extern char *readmidi_make_string_event(struct timiditycontext_t *c, int type, char *string, MidiEvent *ev,
					int cnv);
extern void free_time_segments(struct timiditycontext_t *c);
extern MidiEvent *read_midi_file(struct timiditycontext_t *c, struct timidity_file *mtf,
				 int32 *count, int32 *sp, char *file_name);
extern struct midi_file_info *get_midi_file_info(struct timiditycontext_t *c, const char *filename,int newp);
extern struct midi_file_info *new_midi_file_info(struct timiditycontext_t *c, const char *filename);
extern void free_all_midi_file_info(struct timiditycontext_t *c);
extern int check_midi_file(struct timiditycontext_t *c, const char *filename);
extern char *get_midi_title(struct timiditycontext_t *c, const char *filename);
extern struct timidity_file *open_midi_file(struct timiditycontext_t *c, const char *name,
					    int decompress, int noise_mode);
extern int midi_file_save_as(struct timiditycontext_t *c, char *in_name, char *out_name);
extern char *event2string(struct timiditycontext_t *c, int id);
extern void change_system_mode(struct timiditycontext_t *c, int mode);
extern int get_default_mapID(struct timiditycontext_t *c, int ch);
extern int dump_current_timesig(struct timiditycontext_t *c, MidiEvent *codes, int maxlen);

extern void recompute_delay_status_gs(struct timiditycontext_t *c);
extern void set_delay_macro_gs(struct timiditycontext_t *c, int);
extern void recompute_chorus_status_gs(struct timiditycontext_t *c);
extern void set_chorus_macro_gs(struct timiditycontext_t *c, int);
extern void recompute_reverb_status_gs(struct timiditycontext_t *c);
extern void set_reverb_macro_gs(struct timiditycontext_t *c, int);
extern void set_reverb_macro_gm2(struct timiditycontext_t *c, int);
extern void recompute_eq_status_gs(struct timiditycontext_t *c);
extern void realloc_insertion_effect_gs(struct timiditycontext_t *c);
extern void recompute_insertion_effect_gs(struct timiditycontext_t *c);
extern void recompute_multi_eq_xg(struct timiditycontext_t *c);
extern void set_multi_eq_type_xg(struct timiditycontext_t *c, int);
extern void realloc_effect_xg(struct timiditycontext_t *c, struct effect_xg_t *st);
extern void recompute_effect_xg(struct timiditycontext_t *c, struct effect_xg_t *st);

extern Instrument *recompute_userdrum(struct timiditycontext_t *c, int bank, int prog);
extern void free_userdrum(struct timiditycontext_t *c);

extern void recompute_userinst(struct timiditycontext_t *c, int bank, int prog);
extern void free_userinst(struct timiditycontext_t *c);

extern void init_channel_layer(struct timiditycontext_t *c, int);
extern void add_channel_layer(struct timiditycontext_t *c, int, int);
extern void remove_channel_layer(struct timiditycontext_t *c, int);

extern void free_readmidi(struct timiditycontext_t *c);


extern int read_mfi_file(struct timiditycontext_t *c, struct timidity_file *tf);
extern char *get_mfi_file_title(struct timiditycontext_t *c, struct timidity_file *tf);


#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef struct _UserDrumset {
	int8 bank;
	int8 prog;
	int8 play_note;
	int8 level;
	int8 assign_group;
	int8 pan;
	int8 reverb_send_level;
	int8 chorus_send_level;
	int8 rx_note_off;
	int8 rx_note_on;
	int8 delay_send_level;
	int8 source_map;
	int8 source_prog;
	int8 source_note;
	struct _UserDrumset *next;
} UserDrumset;

typedef struct _UserInstrument {
	int8 bank;
	int8 prog;
	int8 source_map;
	int8 source_bank;
	int8 source_prog;
	int8 vibrato_rate;
	int8 vibrato_depth;
	int8 cutoff_freq;
	int8 resonance;
	int8 env_attack;
	int8 env_decay;
	int8 env_release;
	int8 vibrato_delay;
	struct _UserInstrument *next;
} UserInstrument;

#endif /* ___READMIDI_H_ */
