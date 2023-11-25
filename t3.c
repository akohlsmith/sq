#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "barrier.h"
#include "sq.h"
#include "t.h"

#define THREAD_NAME "three"

static thread_data_t td;

/* "subscribe" to this thread's messages */
void t3_subscribe(sq_t *q)
{
	td.list = sq_list_add(&td.list, q);
}


/* thread 3 listens to messages from thread 1/2 */
void *thread3(void *arg)
{
	int ret;
	pthread_barrier_t *pb;

	memset(&td, 0, sizeof(td));
	td.name = THREAD_NAME;
	pthread_mutex_init(&td.nd_mtx, NULL);
	pthread_cond_init(&td.newdata, NULL);

	td.q = sq_init(THREAD_NAME, NULL, 64, SQ_FLAG_NONE);
	sq_add_listener(td.q, &td.newdata);

	/* wait for all threads to start up */
	pthread_barrier_wait((pthread_barrier_t *)arg);

	/* subscribe to some other thread's messages */
	t1_subscribe(td.q);
	t2_subscribe(td.q);

	td.tx_time = now() + 2000 + rand_num(1000);
	do {
		ret = thread_msg_loop(&td);
	} while (ret == 0);

	return NULL;
}
