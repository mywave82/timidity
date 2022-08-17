#ifndef TIMIDITY_SPEEX_H_INCLUDED
#define TIMIDITY_SPEEX_H_INCLUDED 1

#ifdef AU_SPEEX

extern void speex_set_option_quality(int);
extern void speex_set_option_vbr(int);
extern void speex_set_option_abr(int);
extern void speex_set_option_vad(int);
extern void speex_set_option_dtx(int);
extern void speex_set_option_complexity(int);
extern void speex_set_option_nframes(int);

#endif

#endif
