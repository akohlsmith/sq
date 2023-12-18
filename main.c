#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>

#include "barrier.h"
#include "sq.h"
#include "t.h"

static unsigned long base_msec;
static thread_t *thread_list;

static thread_t *cb_thread, *batt_thread, *can_thread;
static thread_t *candelta_thread;

static void _sub(thread_data_t *td, sq_t *q) { sq_list_add(&td->list, q); }
void conbatt_subscribe(sq_t *q) { _sub(&cb_thread->td, q); }
void batt_subscribe(sq_t *q) { _sub(&batt_thread->td, q); }
void can_subscribe(sq_t *q) { _sub(&can_thread->td, q); }

/* stupid helper to establish a "base time" to make human reading of time easier */
static unsigned long _t(void)
{
	static bool first = true;
	struct timeval tv;
	unsigned long msec;

	gettimeofday(&tv, NULL);
	msec = tv.tv_sec * 1000 + tv.tv_usec / 1000;
	if (first) {
		base_msec = msec;
		first = false;
	}

	return msec - base_msec;
}


/* returns the current time in msec */
unsigned long now(void)
{
	return _t();
}


/* creates a timespec which is for some number of msec in the future of the current time */
void future_ts(struct timespec *ts_out, unsigned int msec)
{
	struct timeval tv;
	long nsec;

	gettimeofday(&tv, NULL);
	nsec = tv.tv_usec * 1000 + msec * 1000000;

	ts_out->tv_sec = tv.tv_sec + (nsec / 1000000000);
	ts_out->tv_nsec = nsec % 1000000000;
}


/* returns a random number between 1 and max */
unsigned long rand_num(unsigned long max)
{
	bool first = true;

	if (first) {
		srandom(now() + base_msec);
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


/* process all waiting messages */
int dequeue(thread_data_t *td)
{
	int ret;
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

	return ret;
}


/*
 * returns 0 if we were awoken because of the condition variable, or -1 on timeout
 * NOTE: pthread_cond_timedwait() can return even if the cond var didn't change and
 * we didn't timeout. This is normal and expected.
 */
int _msg_timedwait(thread_data_t *td, unsigned int msec)
{
	int ret;
	struct timespec ts;

	pthread_mutex_lock(&td->nd_mtx);
	future_ts(&ts, msec);

	ret = pthread_cond_timedwait(&td->newdata, &td->nd_mtx, &ts);
	if (ret != 0) {
		ret = -1;
	}

	return ret;
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

	/* wait up to 100ms for a message from another thread */
	ret = _msg_timedwait(td, 100);

	t = now();
	did_something = false;

	if (ret != ETIMEDOUT) {
		//fprintf(stderr, "[%-5s] %5ld cond_timedwait returned %d\n", td->name, t, ret);
	}

	/* condition var changed */
	if (ret == 0) {
		ret = dequeue(td);
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


thread_t *_create_thread(const char *name, int argc, char **argv, void *(*thread_main)(void *), int queue_len)
{
	static bool first = true;
	static pthread_barrier_t pb;
	thread_t *new_thread;
	thread_data_t *td;
	int ret;

	/* initialize the barrier if this is our first time */
	if (first) {
		pthread_barrier_init(&pb, NULL, 4);
		first = false;
	}

	if ((new_thread = malloc(sizeof(*new_thread))) == NULL) {
		return NULL;
	}

	memset(new_thread, 0, sizeof(*new_thread));
	new_thread->argc = argc;
	new_thread->argv = argv;
	new_thread->pb = &pb;

	td = &new_thread->td;
	td->name = name;

	pthread_mutex_init(&td->nd_mtx, NULL);
	pthread_cond_init(&td->newdata, NULL);

	td->q = sq_init(td->name, NULL, queue_len, SQ_FLAG_NONE);
	sq_add_listener(td->q, &td->newdata);

	if ((ret = pthread_create(&new_thread->pt, NULL, thread_main, new_thread)) == 0) {
		new_thread->next = thread_list;
		thread_list = new_thread;

	} else {
		/* TODO: properly free whatever resource we've allocated above */
		/* sq_free(new_thread->td.q); */
		free(new_thread);
		new_thread = NULL;
	}

	return new_thread;
}

int main(int argc, char **argv)
{
	thread_t *t;

	cb_thread = _create_thread("CONBATT", argc, argv, conbatt_thread_main, QUEUE_LENGTH);
	batt_thread = _create_thread("BATT", argc, argv, batt_thread_main, QUEUE_LENGTH);
	can_thread = _create_thread("CAN", argc, argv, can_thread_main, QUEUE_LENGTH);
	candelta_thread = _create_thread("CDELTA", argc, argv, candelta_thread_main, QUEUE_LENGTH);

	/* wait for everyone to quit */
	for (t = thread_list; t; t = t->next) {
		pthread_join(t->pt, NULL);
	}

	return 0;
}
