#ifndef _BARRIER_H_
#define _BARRIER_H_

#if defined(WITH_BARRIER) || defined(CYGWIN) || (defined(__APPLE__) && defined (__MACH__))

typedef struct {
      int needed;
      int called;
      pthread_mutex_t mutex;
      pthread_cond_t cond;
} barrier_t;

int barrier_init(barrier_t *barrier, int needed);
int barrier_destroy(barrier_t *barrier);
int barrier_wait(barrier_t *barrier);

#define pthread_barrier_t barrier_t
#define pthread_barrier_attr_t barrier_attr_t
#define pthread_barrier_init(b,a,n) barrier_init(b,n)
#define pthread_barrier_destroy(b) barrier_destroy(b)
#define pthread_barrier_wait(b) barrier_wait(b)

#endif

#endif /* _BARRIER_H_ */
