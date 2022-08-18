#ifndef TIMIDITY_WRDI_H_INCLUDED
#define TIMIDITY_WRDI_H_INCLUDED 1

static inline void print_ecmd(struct timiditycontext_t *c, char*, int*, int);
#ifdef HAVE_STRINGS_H
#include <strings.h>
#elif defined HAVE_STRING_H
#include <string.h>
#endif
#include <limits.h>
#include "mblock.h"
#include "common.h"
#include "controls.h"

#ifdef __BORLANDC__
extern void pr_ecmd(struct timiditycontext_t *c, char *cmd, int *args, int narg);
#define print_ecmd(cc, a, b, c) pr_ecmd(cc, a, b, c)
#else
static inline void print_ecmd(struct timiditycontext_t *c, char *cmd, int *args, int narg)
{
    char *p;
    size_t s = MIN_MBLOCK_SIZE;

    p = (char *)new_segment(c, &c->tmpbuffer, s);
    snprintf(p, s, "^%s(", cmd);

    if(*args == WRD_NOARG)
	strncat(p, "*", s - strlen(p) - 1);
    else {
	char c[CHAR_BIT*sizeof(int)];
	snprintf(c, sizeof(c)-1, "%d", args[0]);
	strncat(p, c, s - strlen(p) - 1);
    }
    args++;
    narg--;
    while(narg > 0)
    {
	if(*args == WRD_NOARG)
	    strncat(p, ",*", s - strlen(p) - 1);
	else {
	    char c[CHAR_BIT*sizeof(int)]; /* should be enough loong */
	    snprintf(c, sizeof(c)-1, ",%d", args[0]);
	    strncat(p, c, s - strlen(p) - 1);
	}
	args++;
	narg--;
    }
    strncat(p, ")", s - strlen(p) - 1);
    ctl->cmsg(CMSG_INFO, VERB_VERBOSE, "%s", p);
    reuse_mblock(c, &c->tmpbuffer);
}
#endif

#endif
