#ifndef TIMIDITY_RCP_H_INCLUDED
#define TIMIDITY_RCP_H_INCLUDED 1

#define USER_EXCLUSIVE_LENGTH 24
#define MAX_EXCLUSIVE_LENGTH 1024

struct timiditycontext_t;
struct timidity_file;

extern int read_rcp_file(struct timiditycontext_t *c, struct timidity_file *tf, char *magic0, char *fn);

#endif
