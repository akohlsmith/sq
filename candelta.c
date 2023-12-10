#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "barrier.h"
#include "sq.h"
#include "t.h"

typedef struct {
	float alpha;
	float peak_decay;
	float avg;
	float peak;
} ema_t;

typedef struct idlist_entry_t {
	struct idlist_entry_t *next;

	uint32_t id;
	uint32_t usec;
	uint32_t num;
	ema_t fast;
	ema_t slow;
} idlist_entry_t;

static pthread_mutex_t idlist_mtx;
static idlist_entry_t *idlist;


static void _ema_init(ema_t *ema, float alpha)
{
	memset(ema, 0, sizeof(*ema));
	ema->alpha = alpha;
}


#define SLOW_EMA_ALPHA	(0.2f)
#define FAST_EMA_ALPHA	(0.8f)

static void _ema_update(ema_t *ema, float val)
{
	float x;

	ema->avg = ema->avg * (1.0f - ema->alpha) + val * ema->alpha;
	ema->peak -= ema->peak_decay;

	if (ema->peak < 0.0f) {
		ema->peak = 0.0f;
		ema->peak_decay = 0.0f;
	}

	if (ema->peak < val) {
		ema->peak = val;
		ema->peak_decay = val * 0.025f;
	}
}


/*
 * creates and/or updates the idlist_entry_t for a given canmsg
 * returns true if this is a new entry, false otherwise
 */
static bool _update_listentry(idlist_entry_t **pl, canmsg_t *c)
{
	bool is_new;
	uint32_t dt;
	idlist_entry_t *l;

	if (*pl == NULL) {
		if ((l = malloc(sizeof(*l))) == NULL) {
			return false;
		}

		memset(l, 0, sizeof(*l));
		_ema_init(&l->slow, SLOW_EMA_ALPHA);
		_ema_init(&l->fast, FAST_EMA_ALPHA);
		*pl = l;
		is_new = true;

	} else {
		l = *pl;
		is_new = false;
	}


	/* TODO: look at flags, disregard or count bad frames, etc. */

	if (l->usec > c->usec) {
		/*
		 * TODO: if this happens more than just the very first time
		 * then it indicates some kind of odd time issue that should
		 * be investigated within this utility, not the network itself.
		 */

		l->usec = c->usec;
	}

	dt = c->usec - l->usec;
	_ema_update(&l->slow, (float)dt);
	_ema_update(&l->fast, (float)dt);

	l->id = c->id;
	l->usec = c->usec;
	l->num++;

	return is_new;
}


static int _process_one(sq_elem_t *e)
{
	canmsg_t *c;
	idlist_entry_t *l, *last_l;
	bool is_new;

	c = (canmsg_t *)e->data;
	pthread_mutex_lock(&idlist_mtx);

	for (l = idlist, last_l = NULL; l; l = l->next) {
		last_l = l;

		if (l->id == c->id) {
			break;
		}
	}

	/*
	 * at this point:
	 *     l points to the list entry for this ID or NULL if we haven't seen it before.
	 *     last_l points to the last entry in the list or NULL if there is no list.
	 */

	/* update (or fill out if it's new) the entry data for this ID */
	is_new = _update_listentry(&l, c);

	/*
	 * now add the entry to the end of the list of entries.
	 * if there is no list of entries, then this entry is
	 * the start of the list.
	 */
	if (is_new) {
		if (last_l) {
			fprintf(stderr, "appending to ID %03x for new ID %03x\n", last_l->id, l->id);
			last_l->next = l;
		} else {
			fprintf(stderr, "creating new list starting with ID %03x\n", l->id);
			idlist = l;
		}
	}

	pthread_mutex_unlock(&idlist_mtx);
}


/* process all waiting messages */
static int _dequeue(thread_data_t *td)
{
	int ret;
	sq_elem_t *e;

	do {
		ret = sq_pop(td->q, &e);
		if (ret == SQ_ERR_NO_ERROR) {
			if (e) {
				_process_one(e);
	 			++td->num_rx;
			}

		} else if (ret != SQ_ERR_EMPTY) {
			fprintf(stderr, "[%-5s] sq_pop returned %d\n", td->name, ret);
		}
	} while (ret == SQ_ERR_NO_ERROR);

	return ret;
}


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

	pthread_mutex_init(&idlist_mtx, NULL);

	do {
		if (_msg_timedwait(&t->td, 1000) == 0) {
			_dequeue(&t->td);
			pthread_mutex_unlock(&t->td.nd_mtx);
		}
	} while (ret == 0);

	return NULL;
}
