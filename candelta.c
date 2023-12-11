#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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


static void _dump_list(void)
{
	idlist_entry_t *l;

	pthread_mutex_lock(&idlist_mtx);

	printf("CAN IDs\n");
	for (l = idlist; l; l = l->next) {
		printf("%03x %5d %4.2f(%4.2f)ms %4.2f(%4.2f)ms\n", l->id, l->num, l->fast.avg/1000.0f, l->fast.peak/1000.0f, l->slow.avg/1000.0f, l->slow.peak/1000.0f);
	}
	printf("-------\n\n");

	pthread_mutex_unlock(&idlist_mtx);
}


/*
 * creates a JSON string in the format
 *    "data: [ [ 1, 2, 3, 4, 5 ], [ 41, 42, 43, 44, 45 ], [ 81, 82, 83, 84, 85 ] ]\n\n"
 * where each number is the time between status frames (ID 0x380 + id) for the given CAN node id
 *
 * the resulting pointer should be free()'d when the caller is done with it.
 */
static char *_dump_list_json(void)
{
	idlist_entry_t *l;
	int i, phase, node, len;
	char *s;
	float vals[3][5];

	/* initialize the list */
	for (phase = 1; phase <= 3; phase++) {
		for (node = 1; node <= 5; node++) {
			vals[phase - 1][node - 1] = 0.0f;
		}
	}

	/* now go through the list of seen CAN IDs and pick out the ones we are interested in */
	pthread_mutex_lock(&idlist_mtx);
	for (l = idlist; l; l = l->next) {

		switch (l->id) {
		case 0x381: phase = 1; node = 1; break;
		case 0x382: phase = 1; node = 2; break;
		case 0x383: phase = 1; node = 3; break;
		case 0x384: phase = 1; node = 4; break;
		case 0x385: phase = 1; node = 5; break;
		case 0x3a9: phase = 2; node = 1; break;
		case 0x3aa: phase = 2; node = 2; break;
		case 0x3ab: phase = 2; node = 3; break;
		case 0x3ac: phase = 2; node = 4; break;
		case 0x3ad: phase = 2; node = 5; break;
		case 0x3d1: phase = 3; node = 1; break;
		case 0x3d2: phase = 3; node = 2; break;
		case 0x3d3: phase = 3; node = 3; break;
		case 0x3d4: phase = 3; node = 4; break;
		case 0x3d5: phase = 3; node = 5; break;
		default: phase = 0; break;
		};

		if (phase > 0) {
			vals[phase - 1][node - 1] = l->fast.avg / 1000.0f;
		}
	}
	pthread_mutex_unlock(&idlist_mtx);

	/*
	 * this is just a stupid way to run snprintf() twice.
	 * the first time, len is 0 so snprintf() will return the amount of data needed.
	 * the second time we try to allocate the memory and run it again, this time letting
	 * snprintf() write it out. If the allocation fails, don't let snprintf() write.
	 */
	for (i = 0; i < 2; i++) {
		if (i == 0) {
			s = NULL;
			len = 0;

		} else {
			if ((s = malloc(len)) == NULL) {
				fprintf(stderr, "couldn't allocate %d bytes for JSON string\n", len);
				len = 0;
			}
		}

		len = snprintf(s, len, "data: [[%.2f,%.2f,%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f,%.2f,%.2f]]\r\n\r\n",
			vals[0][0], vals[0][1], vals[0][2], vals[0][3], vals[0][4],
			vals[1][0], vals[1][1], vals[1][2], vals[1][3], vals[1][4],
			vals[2][0], vals[2][1], vals[2][2], vals[2][3], vals[2][4]);
	}

	return s;
}


static int _socket(int port)
{
	int fd, val;
	struct sockaddr_in sa;

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		return -1;
	}

	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);

	val = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		perror("bind");
		close(fd);
		return -1;
	}

	fcntl(fd, F_SETFL, O_NONBLOCK);

	if (listen(fd, 1) < 0) {
		perror("listen");
		close(fd);
		return -1;
	}

	return fd;
}


static int _socket_rx(int fd)
{
	const char *header = "HTTP/1.0 200 OK\nServer: Foo/1.0 Bar/1.0\nDate: Sat, 09 Dec 2023 22:18:53 GMT\nAccess-Control-Allow-Origin: *\nContent-Type: text/event-stream; charset=utf-8\n\n";
	char buf[64];
	int nread;

	do {
		nread = read(fd, buf, sizeof(buf));
fprintf(stderr, "_socket_rx(%d) read %d\n", fd, nread);
	} while (nread == sizeof(buf));

	nread = write(fd, header, strlen(header));
fprintf(stderr, "wrote %d\n", nread);
	return 0;
}


void *candelta_thread_main(void *arg)
{
	int ret, sockfd, client_fd;
	uint32_t dump_time;
	struct pollfd pfd[2];

	t = (thread_t *)arg;

	/* getopt/etc. here */

	sockfd = _socket(8089);

	/* wait for all threads to start up */
	pthread_barrier_wait(t->pb);

	/* subscribe to some other thread's messages */
	can_subscribe(t->td.q);

	pthread_mutex_init(&idlist_mtx, NULL);

	dump_time = 0;
	client_fd = 0;
	do {
		int nfd;

		nfd = 0;

		/* listening fd */
		pfd[0].fd = sockfd;
		pfd[0].events = POLLIN | POLLERR;
		pfd[0].revents = 0;
		nfd++;

		/* client fd (if connected) */
		if (client_fd > 0) {
			pfd[1].fd = client_fd;
			pfd[1].events = POLLIN | POLLERR;
			pfd[1].revents = 0;
			nfd++;
		}

		if ((ret = poll(&pfd[0], nfd, 10)) > 0) {
			if (pfd[0].revents & POLLIN) {
				struct sockaddr_in ca;
				int ca_len;

				ca_len = sizeof(ca);
				if ((client_fd = accept(pfd[0].fd, (struct sockaddr *)&ca, &ca_len)) > 0) {
					//_socket_rx(client_fd);
					//fprintf(stderr, "new connection\n");
				}
			}

			if (pfd[1].revents & POLLIN) {
fprintf(stderr, "pfd1 IN\n");
				_socket_rx(client_fd);
			}

			if (pfd[1].revents & POLLERR) {
fprintf(stderr, "pfd1 ERR\n");
				fprintf(stderr, "closed connection\n");
				close(client_fd);
				client_fd = 0;
			}
		}

		if (ret < 0) {
			perror("poll");
		}

		if (_msg_timedwait(&t->td, 10) == 0) {
			_dequeue(&t->td);
		}
		pthread_mutex_unlock(&t->td.nd_mtx);

		/* is it time to dump data? */
		if (now() > dump_time) {
			if (client_fd) {
				char *buf;

				if ((buf = _dump_list_json())) {
					write(client_fd, buf, strlen(buf));
					free(buf);
				}
			}

			_dump_list();
			dump_time = now() + 1000;
		}

		ret = 0;
	} while (ret == 0);

	return NULL;
}
