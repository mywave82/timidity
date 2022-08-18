
#ifndef ___MT19937AR_H_
#define ___MT19937AR_H_

struct timiditycontext_t;
#define MT19937AR_N 624
#define MT19937AR_M 397
#define MATRIX_A 0x9908b0dfUL   /* constant vector a */

extern void init_by_array(struct timiditycontext_t *c, unsigned long [], unsigned long);
extern unsigned long genrand_int32(struct timiditycontext_t *c);
extern double genrand_real1(struct timiditycontext_t *c);

#endif /* ___MT19937AR_H_ */
