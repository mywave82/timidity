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

/*
 * Historical issues: This file once was a huge header file, but now is
 * devided into some smaller ones.  Please do not add things to this
 * header, but consider put them on other files.
 */

#ifndef TIMIDITY_H_INCLUDED
#define TIMIDITY_H_INCLUDED 1

/*
   Table of contents:
   (1) Flags and definitions to customize timidity
   (3) inportant definitions not to customize
   (2) #includes -- include other headers
 */

/*****************************************************************************\
 section 1: some customize issues
\*****************************************************************************/


/* You could specify a complete path, e.g. "/etc/timidity.cfg", and
   then specify the library directory in the configuration file. */
/* #define CONFIG_FILE "/etc/timidity.cfg" */
#ifndef CONFIG_FILE
#  ifdef DEFAULT_PATH
#    define CONFIG_FILE DEFAULT_PATH "/timidity.cfg"
#  else
#    define CONFIG_FILE PKGDATADIR "/timidity.cfg"
#  endif /* DEFAULT_PATH */
#endif /* CONFIG_FILE */


/* Filename extension, followed by command to run decompressor so that
   output is written to stdout. Terminate the list with a 0.

   Any file with a name ending in one of these strings will be run
   through the corresponding decompressor. If you don't like this
   behavior, you can undefine DECOMPRESSOR_LIST to disable automatic
   decompression entirely. */

#define DECOMPRESSOR_LIST { \
			      ".gz",  "gunzip -c %s", \
			      ".xz",  "xzcat %s", \
			      ".lzma", "lzcat %s", \
			      ".bz2", "bunzip2 -c %s", \
			      ".Z",   "zcat %s", \
			      ".zip", "unzip -p %s", \
			      ".lha", "lha -pq %s", \
			      ".lzh", "lha -pq %s", \
			      ".shn", "shorten -x %s -", \
			     0 }


/* Define GUS/patch converter. */
#define PATCH_CONVERTERS { \
			     ".wav", "wav2pat %s", \
			     0 }

/* When a patch file can't be opened, one of these extensions is
   appended to the filename and the open is tried again.

   This is ignored for Windows, which uses only ".pat" (see the bottom
   of this file if you need to change this.) */
#define PATCH_EXT_LIST { \
			   ".pat", \
			   ".shn", ".pat.shn", \
			   ".gz", ".pat.gz", \
			   ".bz2", ".pat.bz2", \
			   0 }


/* Acoustic Grand Piano seems to be the usual default instrument. */
#define DEFAULT_PROGRAM 0


/* Specify drum channels (terminated with -1).
   10 is the standard percussion channel.
   Some files (notably C:\WINDOWS\CANYON.MID) think that 16 is one too.
   On the other hand, some files know that 16 is not a drum channel and
   try to play music on it. This is now a runtime option, so this isn't
   a critical choice anymore. */
#define DEFAULT_DRUMCHANNELS {10, -1}
/* #define DEFAULT_DRUMCHANNELS {10, 16, -1} */

/* type of floating point number */
typedef double FLOAT_T;
/* typedef float FLOAT_T; */


/* A somewhat arbitrary frequency range. The low end of this will
   sound terrible as no lowpass filtering is performed on most
   instruments before resampling. */
#define MIN_OUTPUT_RATE	4000
#define MAX_OUTPUT_RATE	400000


/* Master volume in percent. */
#define DEFAULT_AMPLIFICATION	70


/* Default sampling rate, default polyphony, and maximum polyphony.
   All but the last can be overridden from the command line. */
#ifndef DEFAULT_RATE
#define DEFAULT_RATE	44100
#endif /* DEFAULT_RATE */

#define DEFAULT_VOICES	256


/* The size of the internal buffer is 2^AUDIO_BUFFER_BITS samples.
   This determines maximum number of samples ever computed in a row.

   For Linux and FreeBSD users:

   This also specifies the size of the buffer fragment.  A smaller
   fragment gives a faster response in interactive mode -- 10 or 11 is
   probably a good number. Unfortunately some sound cards emit a click
   when switching DMA buffers. If this happens to you, try increasing
   this number to reduce the frequency of the clicks.

   For other systems:

   You should probably use a larger number for improved performance.

*/
#define AUDIO_BUFFER_BITS 12	/* Maxmum audio buffer size (2^bits) */


/* 1000 here will give a control ratio of 22:1 with 22 kHz output.
   Higher CONTROLS_PER_SECOND values allow more accurate rendering
   of envelopes and tremolo. The cost is CPU time. */
#define CONTROLS_PER_SECOND 1000


/* Default resamplation algorighm.  Define as resample_XXX, where XXX is
   the algorithm name.  The following algorighms are available:
   cspline, gauss, newton, linear, none. */
#ifndef DEFAULT_RESAMPLATION
#define DEFAULT_RESAMPLATION resample_gauss

#endif

/* Don't allow users to choose the resamplation algorithm. */
/* #define FIXED_RESAMPLATION */


/* Defining USE_DSP_EFFECT to refine chorus, delay, EQ and insertion effect.
   This is definitely worth the slight increase in CPU usage. */
#define USE_DSP_EFFECT


/* This is an experimental kludge that needs to be done right, but if
   you've got an 8-bit sound card, or cheap multimedia speakers hooked
   to your 16-bit output device, you should definitely give it a try.

   Defining LOOKUP_HACK causes table lookups to be used in mixing
   instead of multiplication. We convert the sample data to 8 bits at
   load time and volumes to logarithmic 7-bit values before looking up
   the product, which degrades sound quality noticeably.

   Defining LOOKUP_HACK should save ~20% of CPU on an Intel machine.
   LOOKUP_INTERPOLATION might give another ~5% */
/* #define LOOKUP_HACK */
/* #define LOOKUP_INTERPOLATION */

/* Greatly reduces popping due to large volume/pan changes.
 * This is definitely worth the slight increase in CPU usage. */
#define SMOOTH_MIXING

/* Make envelopes twice as fast. Saves ~20% CPU time (notes decay
   faster) and sounds more like a GUS. There is now a command line
   option to toggle this as well. */
/* #define FAST_DECAY */


/* How many bits to use for the fractional part of sample positions.
   This affects tonal accuracy. The entire position counter must fit
   in 32 bits, so with FRACTION_BITS equal to 12, the maximum size of
   a sample is 1048576 samples (2 megabytes in memory). The GUS gets
   by with just 9 bits and a little help from its friends...
   "The GUS does not SUCK!!!" -- a happy user :) */
#define FRACTION_BITS 12


/* For some reason the sample volume is always set to maximum in all
   patch files. Define this for a crude adjustment that may help
   equalize instrument volumes. */
#define ADJUST_SAMPLE_VOLUMES


/* If you have root access, you can define DANGEROUS_RENICE and chmod
   timidity setuid root to have it automatically raise its priority
   when run -- this may make it possible to play MIDI files in the
   background while running other CPU-intensive jobs. Of course no
   amount of renicing will help if the CPU time simply isn't there.

   The root privileges are used and dropped at the beginning of main()
   in timidity.c -- please check the code to your satisfaction before
   using this option. (And please check sections 11 and 12 in the
   GNU General Public License (under GNU Emacs, hit ^H^W) ;) */
/* #define DANGEROUS_RENICE -15 */


/* The number of samples to use for ramping out a dying note. Affects
   click removal. */
#define MAX_DIE_TIME 20


/* On some machines (especially PCs without math coprocessors),
   looking up sine values in a table will be significantly faster than
   computing them on the fly. Uncomment this to use lookups. */
#define LOOKUP_SINE


/* Shawn McHorse's resampling optimizations. These may not in fact be
   faster on your particular machine and compiler. You'll have to run
   a benchmark to find out. */
#define PRECALC_LOOPS


/* If calling ldexp() is faster than a floating point multiplication
   on your machine/compiler/libm, uncomment this. It doesn't make much
   difference either way, but hey -- it was on the TODO list, so it
   got done. */
/* #define USE_LDEXP */


/* Define the pre-resampling cache size.
 * This value is default. You can change the cache saze with
 * command line option.
 */
#define DEFAULT_CACHE_DATA_SIZE (2*1024*1024)


#ifdef SUPPORT_SOCKET
/* Please define your mail domain address. */
#ifndef MAIL_DOMAIN
#define MAIL_DOMAIN "@localhost"
#endif /* MAIL_DOMAIN */

/* Please define your mail name if you are at Windows.
 * Otherwise (maybe unix), undefine this macro
 */
/* #define MAIL_NAME "somebody" */
#endif /* SUPPORT_SOCKET */


/* Where do you want to put temporary file into ?
 * If you are in UNIX, you can undefine this macro. If TMPDIR macro is
 * undefined, the value is used in environment variable `TMPDIR'.
 * If both macro and environment variable is not set, the directory is
 * set to /tmp.
 */
/* #define TMPDIR "/var/tmp" */


/* To use GS drumpart setting. */
#define GS_DRUMPART

/**** Japanese section ****/
/* To use Japanese kanji code. */
#define JAPANESE

/* Select output text code:
 * "AUTO"	- Auto conversion by `LANG' environment variable (UNIX only)
 * "ASCII"	- Convert unreadable characters to '.'(0x2e)
 * "NOCNV"	- No conversion
 * "EUC"	- EUC
 * "JIS"	- JIS
 * "SJIS"	- shift JIS
 */

#ifndef JAPANESE
/* Not japanese (Select "ASCII" or "NOCNV") */
#define OUTPUT_TEXT_CODE "ASCII"
#else
/* Japanese */
#ifndef __W32__
/* UNIX (Select "AUTO" or "ASCII" or "NOCNV" or "EUC" or "JIS" or "SJIS") */
#define OUTPUT_TEXT_CODE "AUTO"
#else
/* Windows (Select "ASCII" or "NOCNV" or "SJIS") */
#define OUTPUT_TEXT_CODE "SJIS"
#endif
#endif


/* Undefine if you don't use modulation wheel MIDI controls.
 * There is a command line option to enable/disable this mode.
 */
#define MODULATION_WHEEL_ALLOW


/* Undefine if you don't use portamento MIDI controls.
 * There is a command line option to enable/disable this mode.
 */
#define PORTAMENTO_ALLOW


/* Undefine if you don't use NRPN vibrato MIDI controls
 * There is a command line option to enable/disable this mode.
 */
#define NRPN_VIBRATO_ALLOW


/* Define if you want to use reverb / freeverb controls in defaults.
 * This mode needs high CPU power.
 * There is a command line option to enable/disable this mode.
 */
/* #define REVERB_CONTROL_ALLOW */
#define FREEVERB_CONTROL_ALLOW


/* Define if you want to use chorus controls in defaults.
 * This mode needs high CPU power.
 * There is a command line option to enable/disable this mode.
 */
#define CHORUS_CONTROL_ALLOW


/* Define if you want to use surround chorus in defaults.
 * This mode needs high CPU power.
 * There is a command line option to enable/disable this mode.
 */
/* #define SURROUND_CHORUS_ALLOW */


/* Define if you want to use channel pressure.
 * Channel pressure effect is different in sequencers.
 * There is a command line option to enable/disable this mode.
 */
/* #define GM_CHANNEL_PRESSURE_ALLOW */


/* Define if you want to use voice chamberlin / moog LPF.
 * This mode needs high CPU power.
 * There is a command line option to enable/disable this mode.
 */
#define VOICE_CHAMBERLIN_LPF_ALLOW
/* #define VOICE_MOOG_LPF_ALLOW */


/* Define if you want to use modulation envelope.
 * This mode needs high CPU power.
 * There is a command line option to enable/disable this mode.
 */
/* #define MODULATION_ENVELOPE_ALLOW */


/* Define if you want to trace text meta event at playing.
 * There is a command line option to enable/disable this mode.
 */
/* #define ALWAYS_TRACE_TEXT_META_EVENT */


/* Define if you want to allow overlapped voice.
 * There is a command line option to enable/disable this mode.
 */
#define OVERLAP_VOICE_ALLOW


/* Define if you want to allow temperament control.
 * There is a command line option to enable/disable this mode.
 */
#define TEMPER_CONTROL_ALLOW



/*****************************************************************************\
 section 2: some important definitions
\*****************************************************************************/
/*
  Anything below this shouldn't need to be changed unless you're porting
   to a new machine with other than 32-bit, big-endian words.
 */

/* change FRACTION_BITS above, not these */
#define INTEGER_BITS (32 - FRACTION_BITS)
#define INTEGER_MASK (0xFFFFFFFF << FRACTION_BITS)
#define FRACTION_MASK (~ INTEGER_MASK)

/* This is enforced by some computations that must fit in an int */
#define MAX_CONTROL_RATIO 255

/* Audio buffer size has to be a power of two to allow DMA buffer
   fragments under the VoxWare (Linux & FreeBSD) audio driver */
#define AUDIO_BUFFER_SIZE (1<<AUDIO_BUFFER_BITS)

/* These affect general volume */
#define GUARD_BITS 3
#define AMP_BITS (15-GUARD_BITS)

#define MAX_AMPLIFICATION 800
#define MAX_CHANNELS 32
/*#define MAX_CHANNELS 256*/
#define MAXMIDIPORT 16

/* Vibrato and tremolo Choices of the Day */
#define SWEEP_TUNING 38
#define VIBRATO_AMPLITUDE_TUNING 1.0L
#define VIBRATO_RATE_TUNING 38
#define TREMOLO_AMPLITUDE_TUNING 1.0L
#define TREMOLO_RATE_TUNING 38

#define SWEEP_SHIFT 16
#define RATE_SHIFT 5

#define VIBRATO_SAMPLE_INCREMENTS 32

#define MODULATION_WHEEL_RATE (1.0/6.0)
/* #define MODULATION_WHEEL_RATE (c->midi_time_ratio/8.0) */
/* #define MODULATION_WHEEL_RATE (current_play_tempo/500000.0/32.0) */

#define VIBRATO_DEPTH_TUNING (1.0/4.0)

/* you cannot but use safe_malloc(). */
#define HAVE_SAFE_MALLOC 1
/* malloc's limit */
#define MAX_SAFE_MALLOC_SIZE (1<<23) /* 8M */

#define DEFAULT_SOUNDFONT_ORDER 0


/*****************************************************************************\
 section 3: include other headers
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>

#ifdef HAVE_ERRNO_H
#include <errno.h>
#else
extern int errno;
#endif /* HAVE_ERRNO_H */

#ifdef HAVE_MACHINE_ENDIAN_H
#include <machine/endian.h> /* for __byte_swap_*() */
#endif /* HAVE_MACHINE_ENDIAN_H */

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif /* HAVE_SYS_TYPES_H */

#include "sysdep.h"
#include "support.h"
#include "optcode.h"

#include "arc.h" // ArchiveHandler
#include "aq.h" // AudioBucket
#include "common.h" // PathList
#include "interface.h" // IA_* defines
#include "instrum.h" // AlternateAssign;
#include "playmidi.h" // MidiEvent
#include "mblock.h" // MBlockList
#include "miditrace.h" // MidiTrace
#include "mt19937ar.h"
#include "nkflib.h"
#include "rcp.h"
#include "readmidi.h"
#include "recache.h"
#include "resample.h"
#include "reverb.h"
#include "sffile.h"
#include "sfitem.h"
#include "sndfont.h"
#include "strtab.h" // StringTable
#include "wrd.h" // sry_datapacket
struct timiditycontext_t /* all data should be initialized to 0 unless told otherwise */
{
	void *contextowner;

	/* arc.h */
	ArchiveHandler arc_handler;
	void (* arc_error_handler)(struct timiditycontext_t *c, char *error_message);

	/* aq.h */
	int aq_fill_buffer_flag; /* non-zero if aq_add() is in filling mode */

	/* common.h */
	char *program_name;
	char current_filename[1024];
	MBlockList tmpbuffer;
	char *output_text_code;
#ifdef DEFAULT_PATH
	PathList defaultpathlist; // Init to {DEFAULT_PATH,0} if DEFAULT_PATH is defined
#endif
	/* The paths in this list will be tried whenever we're reading a file */
	PathList *pathlist; /* This is a linked list. */ // Init to &defaultpathlist if DEFAULT_PATH is defined
	int open_file_noise_mode; // init to OF_NORMAL

	/* instrum.h */
	ToneBank *tonebank[128 + MAP_BANK_COUNT]; // init the first index to {&standard_tonebank};
	ToneBank *drumset[128 + MAP_BANK_COUNT]; // init the first index to {&standard_drumset};
	Instrument *default_instrument; /* This is a special instrument, used for all melodic programs */
	SpecialPatch *special_patch[NSPECIAL_PATCH];
	int default_program[MAX_CHANNELS]; /* This is only used for tracks that don't specify a program */
	int antialiasing_allowed;
	int fast_decay; // init to 1 if FAST_DECAY is defined, otherwise 0
	int free_instruments_afterwards;
	int cutoff_allowed;
	int opt_sf_close_each_file; // init to SF_CLOSE_EACH_FILE
	char *default_instrument_name;
	int progbase;
	int32 modify_release; /*Pseudo Reverb*/

	/* miditrace.h */
	MidiTrace midi_trace;

	/* mix.h */
	int min_sustain_time; /* time (ms) for full vol note to sustain */ // init to 5000

	/* output.h */
	int audio_buffer_bits; // default to DEFAULT_AUDIO_BUFFER_BITS

	/* playmidi.h */
	Channel channel[MAX_CHANNELS];
	Voice *voice;
		/* --module */
	int opt_default_module; // init to MODULE_TIMIDITY_DEFAULT
	int opt_preserve_silence;
	int32 control_ratio;
	int32 amplification; // init to DEFAULT_AMPLIFICATION
	ChannelBitMask default_drumchannel_mask;
	ChannelBitMask drumchannel_mask;
	ChannelBitMask default_drumchannels;
	ChannelBitMask drumchannels;
	int adjust_panning_immediately; // init to 1
	int max_voices; // init to DEFAULT_VOICES
	int voices; // init to DEFAULT_VOICES
	int upper_voices;
	int note_key_offset; /* For key up/down */
	FLOAT_T midi_time_ratio; /* For speed up/down */ // init to 1.0;
	int opt_modulation_wheel; // default to 1 if MODULATION_WHEEL_ALLOW is defined, otherwise 0
	int opt_portamento; // default to 1 if PORTAMENTO_ALLOW is defined, otherwise 0
	int opt_nrpn_vibrato; // default to 1 if NRPN_VIBRATO_ALLOW is defined, otherwise 0
	int opt_reverb_control; // default to 1 if REVERB_CONTROL_ALLOW is defined, 3 if FREEVERB_CONTROL_ALLOW is defined, otherwise 0
	int opt_chorus_control; // default to 1 if CHORUS_CONTROL_ALLOW is defined, otherwise 0
	int opt_surround_chorus; // default to 1 if SURROUND_CHORUS_ALLOW is defined, otherwise 0
	int opt_channel_pressure; // default to 1 if GM_CHANNEL_PRESSURE_ALLOW is defined, otherwise 0
	int opt_lpf_def; // default to 1 if VOICE_CHAMBERLIN_LPF_ALLOW is defined, 2 if VOICE_MOOG_LPF_ALLOW is defined, otherwise 0
	int opt_overlap_voice_allow; // default to 1 if OVERLAP_VOICE_ALLOW is defined, otherwise 0
	int opt_temper_control; // default to 1 if TEMPER_CONTROL_ALLOW is defined, otherwise 0
	int opt_tva_attack; /* attack envelope control (TVA = Time Variant Amplifier, TVF = Time Variant Filter) */
	int opt_tva_decay; /* decay envelope control */
	int opt_tva_release; /* release envelope control */
	int opt_delay_control; /* CC#94 delay(celeste) effect control */
	int opt_eq_control; /* channel equalizer control */
	int opt_insertion_effect; /* insertion effect control */
	int opt_drum_effect; /* drumpart effect control */
	int opt_modulation_envelope;
	int opt_realtime_playing; /* if set, the instruments will be loaded later */
	int8 opt_init_keysig; // init to 8
	int8 opt_force_keysig; // init to 8
	int32 current_play_tempo; // playmidi_stream_init() inits this to 500000
	int reduce_voice_threshold; /* msec, -1=auto, 0=disable, positive=user given */ // init to -1?, but 0 on mac and remote
	int special_tonebank; /* forces tonebank if >= 0*/ // init to -1
	int default_tonebank; /* default tonebank if not given by MIDI */
	int auto_reduce_polyphony; // init to 1?, but 0 on mac and remote
	int reduce_quality_flag; /* internal use */
	int no_4point_interpolation; // option if resampler is set to CSPLINE or LAGRANGE
	int temper_type_mute; /* For temperament type mute 0..3: preset, 4..7: user-defined */
	int8 current_keysig;
	int8 current_temper_keysig;
	int key_adjust;
	FLOAT_T tempo_adjust; // init to 1.0
	int opt_pure_intonation; /* disallow temper (frequency alterations) */
	int current_freq_table;
	int32 opt_drum_power; /* coef. of drum amplitude */ // init to 100
	int opt_amp_compensation;
	int opt_user_volume_curve; /* use together with init_user_vol_table(), 0,x: auto, 1,1.0: linear, 1,1.661, GS: 1,~2 */
	int opt_pan_delay; /* phase difference between left ear and right ear. */
	char* pcm_alternate_file; /* NULL or "none": Nothing (default)
	                           * "auto": Auto select
	                           * filename: Use it
	                           */
#ifdef IA_ALSASEQ
		/* interface/alsaseq_c.c */
	int opt_realtime_priority; // init to 0
	int opt_sequencer_ports; // init to 4
#endif
		/* effect.c */
	int noise_sharp_type; // init to 4
	int effect_lr_mode; /* 0: left delay, 1: right delay, 2: rotate, -1: not use */ // init to -1
	int effect_lr_delay_msec; // init to 25

	/* readmidi.h */
	ChannelBitMask quietchannels;
	struct midi_file_info *current_file_info;
	TimeSegment *time_segments;
	int opt_trace_text_meta_event; // default to 1 if ALWAYS_TRACE_TEXT_META_EVENT is defined, otherwise 0
	int opt_default_mid;
	int opt_system_mid;
	int ignore_midi_error; // init to 1
	int readmidi_error_flag;
	int readmidi_wrd_mode;
	int play_system_mode; // init to DEFAULT_SYSTEM_MODE

	/* reverb.h */
	struct eq_status_gs_t eq_status_gs;
	FLOAT_T reverb_predelay_factor; // init to 1.0
	FLOAT_T freeverb_scaleroom; // init to 0.28
	FLOAT_T freeverb_offsetroom; // init to 0.7
	struct insertion_effect_gs_t insertion_effect_gs;
	struct effect_xg_t insertion_effect_xg[XG_INSERTION_EFFECT_NUM];
	struct effect_xg_t variation_effect_xg[XG_VARIATION_EFFECT_NUM];
	struct effect_xg_t reverb_status_xg;
	struct effect_xg_t chorus_status_xg;
	struct reverb_status_gs_t reverb_status_gs;
	struct chorus_status_gs_t chorus_status_gs;
	struct delay_status_gs_t delay_status_gs;
	struct multi_eq_xg_t multi_eq_xg;
	pink_noise global_pink_noise_light;

	/* recache.h */
	int32 allocate_cache_size; // init to DEFAULT_CACHE_DATA_SIZE

	/* sfitem.h */
	LayerItem layer_items[SF_EOF]; /* layer type definitions */ // init with layer_items_default

	/* tables.h */
	int32 freq_table[128];
	int32 freq_table_zapped[128];
	int32 freq_table_tuning[128][128];
	int32 freq_table_pytha[24][128];
	int32 freq_table_meantone[48][128];
	int32 freq_table_pureint[48][128];
	int32 freq_table_user[4][48][128];
	      FLOAT_T def_vol_table[1024]; /* v=2.^((x/127-1) * 6) */
	const FLOAT_T    *vol_table; // init to def_vol_table
	      FLOAT_T  gs_vol_table[1024]; /* v=2.^((x/127-1) * 8) */
	const FLOAT_T *xg_vol_table; // init to gs_vol_table
	const FLOAT_T *pan_table; // init to sc_pan_table
	FLOAT_T bend_fine[256];
	FLOAT_T bend_coarse[128];
	#ifdef LOOKUP_HACK
	extern uint8 *_l2u; /* 13-bit PCM to 8-bit u-law */ // init to _l2u_ + 4096
	extern int32 *mixup;
	# ifdef LOOKUP_INTERPOLATION
	extern int8 *iplookup;
	# endif
	#endif
	FLOAT_T attack_vol_table[1024];
	FLOAT_T perceived_vol_table[128];
	FLOAT_T gm2_vol_table[128];
	FLOAT_T user_vol_table[128];
	FLOAT_T gm2_pan_table[129];
	FLOAT_T sb_vol_table[1024];
	FLOAT_T modenv_vol_table[1024];

	/* url.h */
	int url_errno;
#ifdef SUPPORT_SOCKET
	char *user_mailaddr;
	char *url_user_agent;
	char *url_http_proxy_host;
	unsigned short url_http_proxy_port;
	char *url_ftp_proxy_host;
	unsigned short url_ftp_proxy_port;
#endif
	int uudecode_unquote_html;

	/* wrd.h */
	sry_datapacket *datapacket;
	StringTable wrd_read_opts;

	/* arc.c private */
	MBlockList arc_buffer;
	char *compress_buff;
	long  compress_buff_len;
	ArchiveFileList *arc_filelist;
	MBlockList *expand_archive_names_pool;
	StringTable expand_archive_names_stab;
	int expand_archive_names_error_flag;
	int expand_archive_names_depth;

	/* arc_lzh.c private */
	char *lzh_get_ptr;

	/* aq.c private */
	int32 device_qsize;
	int Bps; /* Bytes per sample frame */
	int bucket_size;
	int nbuckets;
	double bucket_time;
	int32 aq_start_count;
	int32 aq_add_count;
	int32 aq_play_counter;
	int32 aq_play_offset_counter;
	double aq_play_start_time;
	AudioBucket *aq_base_buckets;
	AudioBucket *aq_allocated_bucket_list;
	AudioBucket *aq_head;
	AudioBucket *aq_tail;
	double aq__last_soft_buff_time;
	double aq__last_fill_start_time;

	/* common.c private */
	int expand_file_lists_depth;
	int expand_file_lists_error_outflag;
	StringTable expand_file_lists_st;
#ifdef JAPANESE
	char *code_convert_japan_mode;
	char *code_convert_japan_wrd_mode;
#endif
	uint32 tmdy_mkstemp_value;

	/* deflate.c private */
	void *deflate_free_queue;

	/* effect.c private */
	int32 ns_z0[4];
	int32 ns_z1[4];
	int32 ns9_histposl, ns9_histposr;
	int32 ns9_ehl[18];
	int32 ns9_ehr[18];
	uint32 ns9_r1l, ns9_r2l, ns9_r1r, ns9_r2r;
	int32 ns9_c[9];
	int32 effect_left_right_delay_prev[AUDIO_BUFFER_SIZE * 2];
	int effect_left_right_delay_turn_counter;
	int effect_left_right_delay_tc;
	int effect_left_right_delay_status;
	double effect_left_right_delay_rate0;
	double effect_left_right_delay_rate1;
	double effect_left_right_delay_dr;

	/* freq.c private */
	float *floatdata, *magdata, *prunemagdata;
	int *ip;
	float *w;
	uint32 oldfftsize;
	float pitchmags[129];
	double pitchbins[129];
	double new_pitchbins[129];
	int *fft1_bin_to_pitch;

	/* instrum.c private */
	/* Some functions get aggravated if not even the standard banks are
	   available. */
	ToneBank standard_tonebank, standard_drumset;
	struct InstrumentCache *instrument_cache[INSTRUMENT_HASH_SIZE];
	struct bank_map_elem map_bank[MAP_BANK_COUNT], map_drumset[MAP_BANK_COUNT];
	int map_bank_counter;
	struct inst_map_elem *inst_map_table[NUM_INST_MAP][128];
	const char *set_default_instrument_last_name;

	/* mblock.c private */
	MBlockNode *free_mblock_list;

	/* miditrace.c private */
	int trace_loop_lasttime; // init to -1

	/* mt19937ar.c private */
	unsigned long mt[MT19937AR_N]; /* the array for the state vector  */
	int mti; /* mti==MT19937AR_N+1 means mt[MT19937AR_N] is not initialized */
	unsigned long mag01[2]; // init [1] to MATRIX_A

	/* nkflib.c private */
	#ifdef JAPANESE
		/* options */
	char kanji_intro;
	char ascii_intro;
		/* flags */
	int unbuf_f;
	int estab_f;
	int nop_f;
	int rot_f;      /* rot14/43 mode */
	int input_f;    /* non fixed input code  */
	int alpha_f;    /* convert JIx0208 alphbet to ASCII */
	int mimebuf_f;  /* MIME buffered input */
	int broken_f;   /* convert ESC-less broken JIS */
	int iso8859_f;  /* ISO8859 through */
	int binmode_f;  /* binary mode */
	int mime_f;     /* convert MIME B base64 or Q */
	int x0201_f;    /* Assume JISX0201 kana ? */
		/* fold parameter */
	int line;       /* chars in line */
	int prev;
	int fold_f;
	int fold_len;

	void *nkflib_sstdout;
	void *nkflib_sstdin; /* Never used ? */

	#ifndef BUFSIZ
	# define NKFLIB_BUFSIZ 1024
	#else
	# define NKFLIB_BUFSIZ BUFSIZ
	#endif /* NKFLIB_BUFSIZ */
	char sfile_buffer[NKFLIB_BUFSIZ];
		/* buffers */
	unsigned char   hold_buf[HOLD_SIZE*2];
	int             hold_count;

	unsigned char           mime_buf[MIME_BUF_SIZE];
	unsigned int            mime_top;
	unsigned int            mime_last;  /* decoded */
	#ifdef STRICT_MIME
	unsigned int            mime_input; /* undecoded */
	#endif

	/* Global states */
	int output_mode; /* output kanji mode */
        int input_mode;  /* input kanji mode */
        int shift_mode;  /* TRUE shift out, or X0201  */
	int mime_mode;   /* MIME mode B base64, Q hex */

	int c1_return;
	int (*iconv)(struct timiditycontext_t *c, int c2,int c1); /* s_iconv or oconv */
	int (*oconv)(struct timiditycontext_t *c, int c2,int c1); /* [ejs]_oconv */
	int file_out;
	int add_cr;
	int del_cr;

	int iso8859_f_save;
	#endif

// init to MT19937AR_N+1

	/* playmidi.c private */
	int check_eot_flag; /* End-Of-Track */
	int playmidi_seek_flag;
	int play_pause_flag;
	ChannelBitMask channel_mute; /* for channel mute */
	int temper_adj;
/* Undefine if you don't want to use auto voice reduce implementation */
#define REDUCE_VOICE_TIME_TUNING	(play_mode->rate/5) /* 0.2 sec */
#ifdef REDUCE_VOICE_TIME_TUNING
	int max_good_nv; // initialized to 1 by play_midi_file()
	int min_bad_nv; // inititalized to 256 by play_midi_file()
	int32 ok_nv_total; // initialized to 32 by play_midi_file()
	int32 ok_nv_counts; // initialized to 1 by play_midi_file()
	int32 ok_nv_sample; // initialized to 0 by play_midi_file()
	int ok_nv; // initialized to 32 by play_midi_file()
	int old_rate; // initialized to -1 by play_midi_file()
#endif
	int midi_streaming;
	int volatile stream_max_compute; /* compute time limit (in msec) when streaing */ // init to 500 500
	int prescanning_flag;
	int32 midi_restart_time;
	MBlockList playmidi_pool;
	int file_from_stdin;
	int current_temper_freq_table;
	int master_tuning;
	int make_rvid_flag; /* For reverb optimization */
	uint8 vidq_head[128 * MAX_CHANNELS]; /* Ring voice id for each notes.  This ID enables duplicated note. */
	uint8 vidq_tail[128 * MAX_CHANNELS]; /* Ring voice id for each notes.  This ID enables duplicated note. */
	FLOAT_T master_volume;
	int32 master_volume_ratio; // init to 0xFFFF
	int32 lost_notes, cut_notes; /* debug-statistics */
	int32 common_buffer[AUDIO_BUFFER_SIZE*2], /* stereo samples */
	     *buffer_pointer;
	int32 buffered_count;
	char *reverb_buffer; /* MAX_CHANNELS*AUDIO_BUFFER_SIZE*8 */
#ifdef USE_DSP_EFFECT
	int32 insertion_effect_buffer[AUDIO_BUFFER_SIZE * 2]; /* can probably be moved to stack */
#endif /* USE_DSP_EFFECT */
	MidiEvent *event_list;
	MidiEvent *current_event;
	int32 sample_count; /* Length of event_list */
	int32 current_sample; /* Number of calclated samples */
	/* for auto amplitude compensation */
	int mainvolume_max; /* maximum value of mainvolume */
	double compensation_ratio; /* compensation ratio */ // init to 1.0
#ifdef REDUCE_VOICE_TIME_TUNING
	int restore_voices_old_voices; // init to -1
#endif
	int ctl_timestamp_last_secs; // init to -1
	int ctl_timestamp_last_voices; // init to -1
	int playmidi_stream_init_notfirst;
	int set_single_note_tuning_tp;  /* tuning program number */
	int set_single_note_tuning_kn;  /* MIDI key number */
	int set_single_note_tuning_st;  /* the nearest equal-tempered semitone */
	int set_user_temper_entry_tp; /* temperament program number */
	int set_user_temper_entry_ll; /* number of formula */
	int set_user_temper_entry_fh,
	    set_user_temper_entry_fl; /* applying pitch bit mask (forward) */
	int set_user_temper_entry_bh,
	    set_user_temper_entry_bl; /* applying pitch bit mask (backward) */
	int set_user_temper_entry_aa,
	    set_user_temper_entry_bb; /* fraction (aa/bb) */
	int set_user_temper_entry_cc,
	    set_user_temper_entry_dd; /* power (cc/dd)^(ee/ff) */
	int set_user_temper_entry_ee,
	    set_user_temper_entry_ff;
	int set_user_temper_entry_ifmax,
	    set_user_temper_entry_ibmax,
	    set_user_temper_entry_count;
	double set_user_temper_entry_rf[11],
	       set_user_temper_entry_rb[11];
	int set_cuepoint_a0;
	int set_cuepoint_b0;
	int play_midi_play_count;
	int compute_data_last_filled;
	int play_midi_file_last_rc; // init to RC_NONE

	/* rcp.c private */
	uint8 user_exclusive_data[8][USER_EXCLUSIVE_LENGTH];
	int32 init_tempo;
	int32 init_keysig;
	int play_bias;
	char rcp_cmd_name_name[16];

	/* readmidi.c private */
	uint8 rhythm_part[2]; /* for GS, initialized by readmidi_read_init() */
	uint8 drum_setup_xg[16]; /* for XG, initialized by readmidi_read_init() */
	MidiEventList *evlist; // Init to NULL
	MidiEventList *current_midi_point; // Init to NULL
	int32 event_count; // init to 0
	MBlockList mempool;
	StringTable string_event_strtab; // init to { 0 }
	int current_read_track;
	int karaoke_format, karaoke_title_flag;
	struct midi_file_info *midi_file_info; // init to NULL
	char **string_event_table; // init to NULL;
	int    string_event_table_size; // init to 0
#if 0
	int    default_channel_program[256];
#endif
	MidiEvent timesig[256];
	/* MIDI ports will be merged in several channels in the future. */
	int midi_port_number;
	/* These would both fit into 32 bits, but they are often added in
	   large multiples, so it's simpler to have two roomy ints */
	int32 sample_increment, sample_correction; /*samples per MIDI delta-t*/
	uint8 xg_reverb_type_msb; // parse_sysex_event_multi() - init to 0x01
	uint8 xg_reverb_type_lsb; // parse_sysex_event_multi() - init to 0x00;
	uint8 xg_chorus_type_msb; // parse_sysex_event_multi() - init to 0x41
	uint8 xg_chorus_type_lsb; // parse_sysex_event_multi() - init to 0x00;
	int readmidi_read_init_first; // init to 1
	UserDrumset *userdrum_first; // init to (UserDrumset *)NULL;
	UserDrumset *userdrum_last; // init to (UserDrumset *)NULL;
	UserInstrument *userinst_first; // init to (UserInstrument *)NULL;
	UserInstrument *userinst_last; // init to (UserInstrument *)NULL;

	/* recache.c private */
	sample_t *cache_data;
	splen_t cache_data_len;
	struct cache_hash *cache_hash_table[HASH_TABLE_SIZE];
	MBlockList hash_entry_pool;
	struct channel_note_table_t channel_note_table[MAX_CHANNELS];

	/* resample.c private */
	int sample_bounds_min, sample_bounds_max; /* min/max bounds for sample data */
	float *gauss_table[(1<<FRACTION_BITS)]; /* don't need doubles */
	int gauss_n; // init to DEFAULT_GAUSS_ORDER;
	int newt_n; // init to 11
	int32 newt_old_trunc_x; // init to -1
	int newt_grow; // init to -1
	int newt_max; // init to 13
	double newt_divd[60][60];
	sample_t *newt_old_src;
#ifndef FIXED_RESAMPLATION
	resampler_t cur_resample; // init to DEFAULT_RESAMPLATION;
#endif
	resample_t resample_buffer[AUDIO_BUFFER_SIZE];
	int resample_buffer_offset;

	/* reverb.c private */
	double REV_INP_LEV; // init to 1.0
	int32 direct_buffer[AUDIO_BUFFER_SIZE * 2];
	int32 reverb_effect_buffer[AUDIO_BUFFER_SIZE * 2];
	int32 delay_effect_buffer[AUDIO_BUFFER_SIZE * 2];
	int32 chorus_effect_buffer[AUDIO_BUFFER_SIZE * 2];
	int32 eq_buffer[AUDIO_BUFFER_SIZE * 2];

	/* sffile.c private */
	SFBags prbags, inbags;

	/* sndfont.c private */
	SFInsts *sfrecs;
	SFInsts *current_sfrec;
	int last_sample_type;
	int last_sample_instrument;
	int last_sample_keyrange;
	SampleList *last_sample_list;

	/* tables.c private */
	double init_freq_table_meantone_major_ratio[12];
	double init_freq_table_meantone_minor_ratio[12];
	FLOAT_T triangular_table[257];

	/* timidity.c private */
	int try_config_again;
	char *opt_output_name;
	int32 opt_output_rate;
	int opt_control_ratio; /* Save -C option */
	char *wrdt_open_opts;
	char *opt_aq_max_buff;
	char *opt_aq_fill_buff;
	int opt_aq_fill_buff_free_not_needed;
	StringTable opt_config_string;
	int read_config_file_rcf_count;
	int got_a_configuration;
	int def_prog; // init to -1
	VOLATILE int intr;
	char def_instr_name[256];
	int timidity_start_initialize_initialized;
	int opt_buffer_fragments; // init to -1

	/* url.c private */
	struct URL_module *url_mod_list;
#if defined(TILD_SCHEME_ENABLE)
	char url_expand_home_dir_path[BUFSIZ];
	char url_unexpand_home_dir_path[BUFSIZ];
#endif

	/* url_dir.c private */
	void *url_dir_cache;

	/* wrd_read.c private */
	int wrd_bugstatus;
	int wrd_wmode_prev_step;
	int wrd_version;
	/*
	 * Define Bug emulation level.
	 * 0: No emulatoin.
	 * 1: Standard emulation (emulate if the bugs is well known).
	 * 2: More emulation (including unknown bugs).
	 * 3-9: Danger level!! (special debug level)
	 */
	int mimpi_bug_emulation_level; // init to MIMPI_BUG_EMULATION_LEVEL
	MBlockList sry_pool; /* data buffer */
#ifdef ENABLE_SHERRY
	int datapacket_len, datapacket_cnt;
#endif
	uint8 wrd_tokval[MAXTOKLEN + 1]; /* Token value */
	int wrd_lineno; /* linenumber */
	int8 wrd_tok; /* Token type */
	int32 wrd_last_event_time;
	int import_wrd_file_initflag;
	char *import_wrd_file_default_wrd_file1, /* Default */
	     *import_wrd_file_default_wrd_file2; /* Always */
	int wrd_nexttok_waitflag;
	uint8 wrd_nexttok_linebuf[MAXTOKLEN + 16]; /* Token value */
	int wrd_nexttok_tokp;
#ifdef ENABLE_SHERRY
	int sherry_started;	/* 0 - before start command 0x01*/
	                        /* 1 - after start command 0x01*/
	int sry_timebase_mode = 0; /* 0 is default */
#endif

	/* wrdt.c private */
	StringTable wrdt_path_list;
	StringTable wrdt_default_path_list;
	int wrd_midi_event_argc;
	int wrd_midi_event_args[WRD_MAXPARAM];
};

static inline int get_module(struct timiditycontext_t *c) {return c->opt_default_module;}

static inline int is_gs_module(struct timiditycontext_t *c)
{
	int module = get_module(c);
    return (module >= MODULE_SC55 && module <= MODULE_MU100);
}

static inline int is_xg_module(struct timiditycontext_t *c)
{
	int module = get_module(c);
    return (module >= MODULE_MU50 && module <= MODULE_MU100);
}

#endif /* TIMIDITY_H_INCLUDED */
