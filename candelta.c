#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "barrier.h"
#include "sq.h"
#include "t.h"

/* thread 2 listens to messages from thread 1 */
void *candelta_thread_main(void *arg)
{
	int ret;
	thread_t *t;

	t = (thread_t *)arg;

	/* getopt/etc. here */

	/* wait for all threads to start up */
	pthread_barrier_wait(t->pb);

	/* subscribe to some other thread's messages */
	can_subscribe(t->td.q);

	do {
		if (_msg_timedwait(&t->td, 1000) == 0) {
			dequeue(&t->td);
			pthread_mutex_unlock(&t.td->nd_mtx);
		}
	} while (ret == 0);

	return NULL;
}
