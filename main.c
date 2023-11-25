#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

#include "barrier.h"
#include "sq.h"
#include "t.h"

static unsigned long base_t;

/* stupid helper to establish a "base time" to make human reading of time easier */
static unsigned long _t(void)
{
	static bool first = true;
	time_t t;

	time(&t);
	if (first) {
		base_t = t;
		first = false;
	}

	return (unsigned long)t - base_t;
}


/* returns the current time in msec */
unsigned long now(void)
{
	return _t() * 1000;
}


/* creates a timespec which is for some number of msec in the future of the current time */
void future_ts(struct timespec *ts_out, unsigned int msec)
{
	time_t t;

	time(&t);
	ts_out->tv_sec = t;
	ts_out->tv_nsec = msec * 1000000;

	if (ts_out->tv_nsec > 1000000000) {
		ts_out->tv_nsec -= 1000000000;
		ts_out->tv_sec += 1;
	}
}


/* returns a random number between 1 and max */
unsigned long rand_num(unsigned long max)
{
	bool first = true;

	if (first) {
		srandom(now() + base_t);
		first = false;
	}

	return 1 + (random() % max);
}


/* takes a received message and deals with it */
int process_msg(const char *tname, sq_elem_t *e)
{
	if (e) {
		fprintf(stderr, "[%-5s] %5ld rx \"%s\"\n", tname, now(), (char *)e->data);

		if (e->flags & SQ_FLAG_FREE) {
			free(e->data);
		}

		free(e);
	}

	return 0;
}


/* creates a new message and fills out the provided sq_elem_t struct */
sq_elem_t *generate_msg(sq_elem_t *dest_e, const char *tname, const char *s, int val)
{
	int len;
	char *buf;
	const char *fmt = "[%-5s] %03d %s";

	len = snprintf(NULL, 0, fmt, tname, val, s);
	if ((buf = malloc(len)) == NULL) {
		return NULL;
	}

	/*
	 * VOLATILE - we want sq_push() to make a copy of the data
	 * FREE - we malloc()'d the message data because this function might be called re-entrantly, so caller must free()
	 */
	dest_e->data = buf;
	dest_e->dlen = len;
	dest_e->flags = SQ_FLAG_VOLATILE | SQ_FLAG_FREE;
	snprintf(buf, len + 1, fmt, tname, val, s);
	return dest_e;
}


/*
 * demo message loop function
 * called by each thread in their own loop
 * waits 100ms for anyone to send the thread a message
 * processes any messages that were sent our way
 * if it's time to transmit a message of our wn, do so
 */
int thread_msg_loop(thread_data_t *td)
{
	int ret;
	unsigned long t;
	struct timespec ts;
	bool did_something;

	/* wait for a message to be published to our queue or a timeout */
	pthread_mutex_lock(&td->nd_mtx);
	future_ts(&ts, 100);
	ret = pthread_cond_timedwait(&td->newdata, &td->nd_mtx, &ts);

	t = now();
	did_something = false;

	if (ret != ETIMEDOUT) {
		//fprintf(stderr, "[%-5s] %5ld cond_timedwait returned %d\n", td->name, t, ret);
	}

	/* condition var changed */
	if (ret == 0) {
		sq_elem_t *e;

		do {
			ret = sq_pop(td->q, &e);
			if (ret == SQ_ERR_NO_ERROR) {
				if (e) {
					process_msg(td->name, e);
		 			++td->num_rx;
				}

			} else if (ret != SQ_ERR_EMPTY) {
				fprintf(stderr, "[%-5s] sq_pop returned %d\n", td->name, ret);
			}
		} while (ret == SQ_ERR_NO_ERROR);

		did_something = true;
	}

	/* time to transmit? */
	if (t > td->tx_time) {
		sq_elem_t e;

		fprintf(stderr, "[%-5s] %5ld tx\n", td->name, t);
		if (generate_msg(&e, td->name, "hello", td->count)) {
 			if ((ret = sq_publish(td->list, &e)) != SQ_ERR_NO_ERROR) {
 				fprintf(stderr, "[%-5s] sq_publish returned %d\n", td->name, ret);
 			}

 			++td->count;
 			++td->num_tx;
 		}

		if (e.flags & SQ_FLAG_FREE) {
			free(e.data);
		}

		td->tx_time = t + rand_num(2500);
		did_something = true;
	}

	if (did_something) {
		fprintf(stderr, "[%-5s]         (tx %d rx %d)\n", td->name, td->num_tx, td->num_rx);
	}

	pthread_mutex_unlock(&td->nd_mtx);
	return 0;
}


int main(int argc, char **argv)
{
	pthread_t t1, t2, t3;
	pthread_barrier_t pb;

	pthread_barrier_init(&pb, NULL, 3);

	/* create the threads, passing each the barrier so they can all wait for each other to start up */
	pthread_create(&t1, NULL, thread1, (void *)&pb);
	pthread_create(&t2, NULL, thread2, (void *)&pb);
	pthread_create(&t3, NULL, thread3, (void *)&pb);

	/* wait for everyone to quit */
	pthread_join(t1, NULL);
	pthread_join(t2, NULL);
	pthread_join(t3, NULL);

	return 0;
}
