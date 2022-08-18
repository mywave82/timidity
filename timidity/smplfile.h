#ifndef TIMIDITY_SMPLFILE_H_INCLUDED
#define TIMIDITY_SMPLFILE_H_INCLUDED 1

struct timiditycontext_t;
Instrument *extract_sample_file(struct timiditycontext_t *c, char *sample_file);

#endif
