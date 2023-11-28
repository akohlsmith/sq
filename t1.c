#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "barrier.h"
#include "sq.h"
#include "t.h"

#define THREAD_NAME "one"

static thread_data_t *td;



void t1_subscribe(sq_t *q)
{
	td->list = sq_list_add(&td->list, q);
}


/* thread 1 listens to messages from thread 2/3 */
void *thread1(void *arg)
{
	int ret, fd;
	thread_t *t;
	unsigned long next_tx;

	t = (thread_t *)arg;

	/* getopt/etc. here */

	if ((td = _td(THREAD_NAME, QUEUE_LENGTH)) == NULL) {
		return NULL;
	}


	/* wait for all threads to start up */
	pthread_barrier_wait((pthread_barrier_t *)arg);

	do {
		struct timespec ts;

		/* wait for a message to be published to our queue or a timeout */
		pthread_mutex_lock(&td->nd_mtx);
		future_ts(&ts, 1);

		if ((ret = pthread_cond_timedwait(&td->newdata, &td->nd_mtx, &ts)) == 0) {
			dequeue(td);
		}

		if (now() > next_tx) {
			next_tx = now() + 10;
		}
	} while (ret == 0);

	return NULL;
}
