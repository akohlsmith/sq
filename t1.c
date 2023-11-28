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
	int ret;
	pthread_barrier_t *pb;

	if ((td = _td(THREAD_NAME, QUEUE_LENGTH)) == NULL) {
		return NULL;
	}


	/* wait for all threads to start up */
	pthread_barrier_wait((pthread_barrier_t *)arg);

	/* subscribe to some other thread's messages */
	t2_subscribe(td.q);
	t3_subscribe(td.q);

	td.tx_time = now() + 2000 + rand_num(1000);
	do {
		ret = thread_msg_loop(&td);
	} while (ret == 0);

	return NULL;
}
