#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "barrier.h"
#include "sq.h"
#include "t.h"

#define THREAD_NAME "two"

static thread_data_t *td;

/* "subscribe" to this thread's messages */
void t2_subscribe(sq_t *q)
{
	td->list = sq_list_add(&td->list, q);
}


/* thread 2 listens to messages from thread 1 */
void *thread2(void *arg)
{
	int ret;
	thread_t *t;

	t = (thread_t *)arg;

	/* getopt/etc. here */

	if ((td = _td(THREAD_NAME, QUEUE_LENGTH)) == NULL) {
		return NULL;
	}

	/* wait for all threads to start up */
	pthread_barrier_wait((pthread_barrier_t *)arg);

	/* subscribe to some other thread's messages */
	t1_subscribe(td->q);

	td->tx_time = now() + 2000 + rand_num(1000);
	do {
		ret = thread_msg_loop(td);
	} while (ret == 0);

	return NULL;
}
