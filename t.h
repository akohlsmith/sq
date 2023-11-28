#ifndef _T_H_
#define _T_H_

typedef struct {
	int argc;
	char **argv;
	pthread_barrier_t *pb;
} thread_t;

typedef struct {
	const char *name;		/* thread this thread descriptor belongs to */
	int count;			/* message counter (++ for every message published) */
	int num_tx, num_rx;		/* number of publish() / pop() this thread has done */
	sq_t *q;			/* thread message queue */
	sq_list_t *list;		/* list of other threads subscribing to this thread's messages */
	pthread_cond_t newdata;		/* this thread's "got new data" condition variable */
	pthread_mutex_t nd_mtx;		/* mutex protecting newdata */
	unsigned long tx_time;		/* when this thread will transmit a new message */
} thread_data_t;

typedef enum {
	MSGID_NONE = 0,
	MSGID_CAN,
	MSGID_CONBATT,
	MSGID_BATTERY,
	MSGID_IO,
	MSGID_LOG
} msgid_t;

typedef struct {
	uint32_t msgid;
	uint8_t dlc;
	uint8_t data[64];
	uint32_t flags;
} canmsg_t;

typedef struct {
	uint8_t modulation[4];
} cbmsg_t;

typedef struct {

} battmsg_t;

typedef struct {

} iomsg_t;

typedef struct {

} logmsg_t;

typedef struct {
	msgid_t id;

	union {
		canmsg_t can;
		cbmsg_t cb;
		battmsg_t batt;
		iomsg_t io;
		logmsg_t log;
	} u;
} msg_t;

#define QUEUE_LENGTH	(64)

unsigned long now(void);
unsigned long rand_num(unsigned long max);
void future_ts(struct timespec *ts_out, unsigned int msec);
int process_msg(const char *tname, sq_elem_t *e);
sq_elem_t *generate_msg(sq_elem_t *dest_e, const char *tname, const char *s, int val);
int thread_msg_loop(thread_data_t *td);
thread_data_t *_td(const char *thread_name, int queue_len);
int dequeue(thread_data_t *td);
int _msg_timedwait(thread_data_t *td, unsigned int msec);

void t1_subscribe(sq_t *q);
void t2_subscribe(sq_t *q);
void t3_subscribe(sq_t *q);

void *thread1(void *arg);
void *thread2(void *arg);
void *thread3(void *arg);

#endif /* _T_H_ */
