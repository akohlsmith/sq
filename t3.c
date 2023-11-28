#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "barrier.h"
#include "sq.h"
#include "t.h"

#define THREAD_NAME "three"

static thread_data_t *td;

/* "subscribe" to this thread's messages */
void t3_subscribe(sq_t *q)
{
	td->list = sq_list_add(&td->list, q);
}


/* thread 3 listens to messages from thread 1/2 */
void *thread3(void *arg)
{
	int ret;
	thread_t *t;

	t = (thread_t *)arg;

	/* getopt/etc. here */

	if ((td = _td(THREAD_NAME, QUEUE_LENGTH)) == NULL) {
		return NULL;
	}

	/* subscribe to some other thread's messages */
	t1_subscribe(td->q);
	t2_subscribe(td->q);

	td->tx_time = now() + 2000 + rand_num(1000);
	do {
		ret = thread_msg_loop(td);
	} while (ret == 0);

	return NULL;
}
