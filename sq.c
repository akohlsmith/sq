#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>

#include "sq.h"

/*
 * creates an element and adds it to the queue.
 * if the element has SQ_FLAG_VOLATILE, will malloc() enough for the element and its data
 * once pushed, walks through the listener list and notifies anyone waiting
 *
 * returns SQ_ERR_NO_ERROR on successfull add, other SQ_ERR as needed
 */
int sq_push(sq_t *q, sq_elem_t *e)
{
	sq_listeners_t *l;
	sq_elem_t *new_e;
	int alloc_len;

	//fprintf(stderr, "[%-5s] sq_push(q=%p, e=%p, data=%p, len=%d, flags=0x%08x)\n", q->name, q, e, e->data, e->dlen, e->flags);
	/* use trylock() first in case q->flags has SQ_FLAG_NOWAIT set */
	if (pthread_mutex_trylock(&q->mtx) != 0) {
		if (q->flags & SQ_FLAG_NOWAIT) {
			return SQ_ERR_WOULDBLOCK;

		} else {
			pthread_mutex_lock(&q->mtx);
		}
	}

	/* either return right away or block if the queue is full */
	while (q->len >= q->maxlen) {
		if (q->flags & SQ_FLAG_NOWAIT) {
			q->flags |= SQ_FLAG_OVERRUN;
			return SQ_ERR_FULL;

		/* queue is full; wait on q->notfull to wake us */
		} else {
			//fprintf(stderr, "[%-5s] - queue full, cond_wait()\n", q->name);
			pthread_cond_wait(&q->notfull, &q->mtx);
		}
	};

	/* allocate a new element (and maybe its data too) */
	alloc_len = sizeof(*e);
	if (e->flags & SQ_FLAG_VOLATILE) {
		alloc_len += e->dlen;
	}

	if ((new_e = malloc(alloc_len)) == NULL) {

		/* set overrun flag because we had no memory to add data, so data got lost */
		q->flags |= SQ_FLAG_OVERRUN;
		return SQ_ERR_NOMEM;
	}

	/* if the data is volatile, copy it */
	new_e->next = NULL;
	if (e->flags & SQ_FLAG_VOLATILE) {
		new_e->data = new_e + sizeof(*new_e);
		new_e->dlen = e->dlen;
		memcpy(new_e->data, e->data, e->dlen);

		/* mask off any old allocation flags and explicitly set VOLATILE */
		new_e->flags = SQ_FLAG_VOLATILE | (e->flags & ~SQ_MASK_ALLOC);

	/* data isn't volatile, just point to it */
	} else {
		new_e->data = e->data;
		new_e->dlen = e->dlen;
		new_e->flags = e->flags;
	}

	//fprintf(stderr, "[%-5s]      new_e=%p, q->head=%p, q->tail=%p\n", q->name, new_e, q->head, q->tail);
	/* is this the first element in the queue? */
	if (q->head == NULL) {
		q->head = new_e;
		q->tail = q->head;

	/* not the first, just add to the queue */
	} else {
		q->tail->next = new_e;
		q->tail = q->tail->next;
	}

	q->len++;
	//fprintf(stderr, "[%-5s]      q->head=%p, q->tail=%p, len=%d\n", q->name, q->head, q->tail, q->len);
	pthread_mutex_unlock(&q->mtx);

	/* wake up everyone listening on this queue */
	pthread_mutex_lock(&q->listeners_mtx);

	for (l = q->listeners; l; l = l->next) {
		//fprintf(stderr, "[%-5s] push wakeup: %p\n", q->name, l->newdata);
		pthread_cond_broadcast(l->newdata);
	}
	pthread_mutex_unlock(&q->listeners_mtx);

	return SQ_ERR_NO_ERROR;
}


/*
 * retrieves the next element from the queue
 * the element returned must be freed by the caller when they are done with it
 *
 * if the element has SQ_FLAG_VOLATILE set, then the caller must take care not to
 * free the element itself until they are done with the data as well, because the
 * element data was allocated with the element struct when it was pushed.
 *
 * If the element has SQ_FLAG_FREE set, then the caller must separately free()
 * the element data pointer.
 *
 * the returned element flags are also updated to include information about the
 * queue itself; SQ_FLAG_OVERRUN is particularly useful to know if there was
 * data loss due to the queue being full.
 *
 * e is updated with the queue element retreived or is set to NULL if the queue is empty.
 *
 * returns SQ_ERR_NO_ERROR on success, various SQ_ERR otherwise.
 */
int sq_pop(sq_t *q, sq_elem_t **e)
{
	int ret;

	/* use trylock() first in case q->flags has SQ_FLAG_NOWAIT set */
	if (pthread_mutex_trylock(&q->mtx) != 0) {
		if (q->flags & SQ_FLAG_NOWAIT) {
			return SQ_ERR_WOULDBLOCK;

		} else {
			pthread_mutex_lock(&q->mtx);
		}
	}

	if (q->len) {
		sq_elem_t *new_e;

		new_e = q->head;
		q->head = new_e->next;
		q->len--;

		/*
		 * queue is no longer full.
		 * Copy the queue stats over to the popped element
		 * and clear the queue flags.
		 */
		q->flags &= ~SQ_FLAG_FULL;
		new_e->flags &= ~SQ_MASK_QSTATE;
		new_e->flags |= (q->flags &= SQ_MASK_QSTATE);
		q->flags &= ~SQ_MASK_QSTATE;

		*e = new_e;
		ret = SQ_ERR_NO_ERROR;

		/* wake up anyone waiting to push to this queue */
		pthread_cond_broadcast(&q->notfull);

	} else {
		*e = NULL;
		ret = SQ_ERR_EMPTY;
	}

	pthread_mutex_unlock(&q->mtx);
	return ret;
}


/* adds a new listener to the queue's listener list */
void sq_add_listener(sq_t *q, pthread_cond_t *data_cond)
{
	sq_listeners_t *new_l;

	/* create a new listeners_t and fill it out */
	if ((new_l = malloc(sizeof(*new_l)))) {
		pthread_mutex_lock(&q->listeners_mtx);

		new_l->next = NULL;
		new_l->newdata = data_cond;

		/* add the new listener to the end of the list */
		if (q->listeners) {
			sq_listeners_t *l;

			for (l = q->listeners; l->next && l->newdata != data_cond; l = l->next) ;

			/* don't add a listener that's already on the list */
			if (l->newdata == data_cond) {
				free(new_l);
				new_l = NULL;

			} else {
				l->next = new_l;
			}

		/* there is no list. the new listener starts the list */
		} else {
			q->listeners = new_l;
		}

		pthread_mutex_unlock(&q->listeners_mtx);
		//fprintf(stderr, "[%-5s] added listener %p\n", q->name, new_l->newdata);
	}
}


/*
 * adds a queue to the end of a list of queues
 * correctly handles an empty list, and does not add queue
 * if it's already on the list
 *
 * returns the start of the list or NULL if the queue couldn't be added
 */
sq_list_t *sq_list_add(sq_list_t **list, sq_t *q)
{
	sq_list_t *new_l;

	if ((new_l = malloc(sizeof(*new_l))) == NULL) {
		return NULL;
	}

	new_l->next = NULL;
	new_l->q = q;

	/* add the queue to the end of the list of queues */
	if (*list) {
		sq_list_t *l;

		for (l = *list; l->next && l->q != q; l = l->next) ;

		/* don't add a queue that's already on the list */
		if (l->q == q) {
			//fprintf(stderr, "queue for %s already on this list, not adding\n", q->name);
			free(new_l);

		} else {
			//fprintf(stderr, "added queue for %s to this list\n", q->name);
			l->next = new_l;
		}

	/* this is the first entry in the list */
	} else {
		//fprintf(stderr, "empty list, creating new one\n");
		*list = new_l;
	}

	return *list;
}


/*
 * takes an element and adds it to every queue in the list of queues
 * correctly handles an empty list (i.e. list can be NULL)
 * DOES NOT STOP IF A QUEUE FAILED TO ADD THE ELEMENT
 *
 * returns SQ_ERR_NO_ERROR if all the element was successfully pushed
 * to all queues in the list, or the last error received
 */
int sq_publish(sq_list_t *list, sq_elem_t *e)
{
	int ret;
	sq_list_t *l;

	for (l = list, ret = SQ_ERR_NO_ERROR; l; l = l->next) {
		int l_ret;

		if ((l_ret = sq_push(l->q, e)) != SQ_ERR_NO_ERROR) {
			ret = l_ret;
		}

		//fprintf(stderr, "\n[%-5s] publish: push(%p, %p) returned %d\n", l->q->name, l->q, e, l_ret);
	}

	return ret;
}



/*
 * allocates and initializes a new queue.
 * useful flags include
 *     SQ_FLAG_NOWAIT - do not block waiting for the queue lock
 *
 * returns the newly-minted queue or NULL on memory allocation failure.
 */
sq_t *sq_init(const char *name, void *ctx, int maxlen, unsigned int flags)
{
	sq_t *new_q;

	if ((new_q = malloc(sizeof(*new_q)))) {
		memset(new_q, 0, sizeof(*new_q));
		new_q->name = name;
		new_q->ctx = ctx;
		new_q->head = NULL;
		new_q->tail = NULL;
		new_q->listeners = NULL;
		new_q->len = 0;
		new_q->maxlen = maxlen;
		new_q->flags = flags;

		pthread_mutex_init(&new_q->mtx, NULL);
		pthread_mutex_init(&new_q->listeners_mtx, NULL);
		pthread_cond_init(&new_q->notfull, NULL);
	}

	return new_q;
}
