#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "barrier.h"
#include "sq.h"
#include "t.h"

/* thread 3 listens to messages from thread 1/2 */
void *can_thread_main(void *arg)
{
	int ret;
	thread_t *t;

	t = (thread_t *)arg;

	/* getopt/etc. here */

	/* wait for all threads to start up */
	pthread_barrier_wait(t->pb);

	/* subscribe to some other thread's messages */
	conbatt_subscribe(t->td.q);
	batt_subscribe(t->td.q);

	t->td.tx_time = now() + 2000 + rand_num(1000);
	do {
		ret = thread_msg_loop(&t->td);
	} while (ret == 0);

	return NULL;
}
