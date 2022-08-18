#ifndef TIMIDITY_SNDFONT_H_INCLUDED
#define TIMIDITY_SNDFONT_H_INCLUDED 1

#ifdef CFG_FOR_SF
#define SF_CLOSE_EACH_FILE 0
#define SF_SUPPRESS_ENVELOPE
#define SF_SUPPRESS_TREMOLO
#define SF_SUPPRESS_VIBRATO
#else
#define SF_CLOSE_EACH_FILE 1
/*#define SF_SUPPRESS_ENVELOPE*/
/*#define SF_SUPPRESS_TREMOLO*/
/*#define SF_SUPPRESS_VIBRATO*/
#endif /* CFG_FOR_SF */

typedef struct _SFPatchRec {
	int preset, bank, keynote; /* -1 = matches all */
} SFPatchRec;

typedef struct _SampleList {
	Sample v;
	struct _SampleList *next;
	int32 start;
	int32 len;
	int32 cutoff_freq;
	int16 resonance;
	int16 root, tune;
	char low, high;		/* key note range */
	int8 reverb_send, chorus_send;

	/* Depend on play_mode->rate */
	int32 vibrato_freq;
	int32 attack;
	int32 hold;
	int32 sustain;
	int32 decay;
	int32 release;

	int32 modattack;
	int32 modhold;
	int32 modsustain;
	int32 moddecay;
	int32 modrelease;

	int bank, keynote;	/* for drum instruments */
} SampleList;

typedef struct _InstList {
	SFPatchRec pat;
	int pr_idx;
	int samples;
	int order;
	SampleList *slist;
	struct _InstList *next;
} InstList;

typedef struct _SFExclude {
	SFPatchRec pat;
	struct _SFExclude *next;
} SFExclude;

typedef struct _SFOrder {
	SFPatchRec pat;
	int order;
	struct _SFOrder *next;
} SFOrder;

#define INSTHASHSIZE 127
#define INSTHASH(bank, preset, keynote) \
	((int)(((unsigned)bank ^ (unsigned)preset ^ (unsigned)keynote) % INSTHASHSIZE))

typedef struct _SFInsts {
	struct timidity_file *tf;
	char *fname;
	int8 def_order, def_cutoff_allowed, def_resonance_allowed;
	uint16 version, minorversion;
	int32 samplepos, samplesize;
	InstList *instlist[INSTHASHSIZE];
	char **inst_namebuf;
	SFExclude *sfexclude;
	SFOrder *sforder;
	struct _SFInsts *next;
	FLOAT_T amptune;
	MBlockList pool;
} SFInsts;

#endif
