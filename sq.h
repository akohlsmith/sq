#ifndef _SQ_H_
#define _SQ_H_

/*
 * simple queue
 * multiple producer, multiple consumer
 * does not try to get fancy with lockless design, just a simple queue
 * requires dynamic memory allocation - like I said, just a simple/basic queue.
 *
 * each queue is represented by a single sq_t struct
 * the queue contains a number of individual elements, each represented by a sq_elem_t struct.
 * add to the queue with sq_push(), remove from the queue with sq_pop()
 *
 * if you're interested in waiting for data to be pushed to the queue, create a condition var
 * and call sq_add_listener() -- your cond var will be broadcast to when new data is pushed.
 *
 * you can send an element to multiple queues by using sq_publish() -- takes a sq_list_t of
 * queues to push() to and a sq_elem_t that will be pushed to all queues on the list.
 *
 * queue flags and element flags are described below, but some notes:
 *
 * SQ_FLAG_VOLATILE - if an element has this flag, it means that the data pointer will not
 * stick around. push() will allocate memory and copy the data to the new buffer, and it also
 * means that when you pop(), you must use/copy the data before you free() the element, because
 * the data was allocated along with the element struct itself.
 *
 * SQ_FLAG_FREE - if an element has this flag then the data pointer must be explicitly free()'d
 *
 * when you pop() data from the queue, some of the queue flags are copied into the element flags
 * since they might be useful:
 *
 * SQ_FLAG_OVERRUN - this means one or more push() operations on this queue have failed before
 * the pop() call, so there has been data loss
 *
 * if SQ_FLAG_NOWAIT is passed to sq_init(), then (almost) all lock calls can fail and the sq_*
 * function might return SQ_ERR_WOULDBLOCK. not an error so much as an indication that the
 * sq_* call must be retried. Similar to O_NONBLOCK for read() and write().
 */

/* queue entry */
typedef struct sq_elem_t {
	struct sq_elem_t *next;
	void *data;				/* data for this entry */
	unsigned int dlen;			/* lengh of data */
	unsigned int flags;			/* entry flags */
} sq_elem_t;


/*
 * queue listener list entry
 * each one of these is a cond var that will be woken up on push()
 */
typedef struct sq_listeners_t {
	struct sq_listeners_t *next;
	pthread_cond_t *newdata;
} sq_listeners_t;


typedef struct {
	const char *name;			/* name of the queue, only for debug */
	void *ctx;				/* opaque object, not used by sq at all */
	sq_elem_t *head;			/* first element in the queue */
	sq_elem_t *tail;			/* last element in the queue */
	pthread_mutex_t mtx;			/* queue mutex */
	pthread_cond_t notfull;			/* cond var for push() to wait on when queue is full */
	unsigned int flags;			/* queue flags */
	unsigned int len, maxlen;		/* number of items in queue / max number of items allowed */

	pthread_mutex_t listeners_mtx;		/* listener mutex */
	sq_listeners_t *listeners;		/* list of listeners for this queue, each is woken up on push() */
} sq_t;


/*
 * subscriber list struct
 * each one of these points to a queue that publish() will push() to
 */
typedef struct sq_list_t {
	struct sq_list_t *next;
	sq_t *q;
} sq_list_t;


#define SQ_FLAG_NONE		(0)
#define SQ_FLAG_NOWAIT		(1 << 0)	/* queue functions not allowed to sleep */
#define SQ_FLAG_VOLATILE	(1 << 1)	/* on push(): must alloc+copy data too  on pop(): data will disappear when e is free()'d */
#define SQ_FLAG_FREE		(1 << 2)	/* on pop(): caller must free(e->data) before free(e) */
#define SQ_FLAG_FULL		(1 << 30)	/* pushing this element filled the queue */
#define SQ_FLAG_OVERRUN		(1 << 31)	/* queue is full and not allowed to sleep waiting to be freed, so some data was discarded */

#define SQ_MASK_ALLOC		(SQ_FLAG_VOLATILE | SQ_FLAG_FREE)
#define SQ_MASK_QSTATE		(SQ_FLAG_OVERRUN | SQ_FLAG_FULL)

#define SQ_ERR_NO_ERROR		(0)
#define SQ_ERR_EMPTY		(-1)
#define SQ_ERR_NOMEM		(-2)
#define SQ_ERR_FULL		(-3)
#define SQ_ERR_WOULDBLOCK	(-4)

int sq_push(sq_t *q, sq_elem_t *e);
int sq_pop(sq_t *q, sq_elem_t **e);
sq_t *sq_init(const char *name, void *ctx, int maxlen, unsigned int flags);
void sq_add_listener(sq_t *q, pthread_cond_t *data_cond);
sq_list_t *sq_list_add(sq_list_t **list, sq_t *q);
int sq_publish(sq_list_t *list, sq_elem_t *e);

#endif /* _SQ_H_ */
