#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <linux/can.h>
#include <linux/can/netlink.h>
#include <linux/can/raw.h>
#include <linux/sockios.h>

#include "barrier.h"
#include "sq.h"
#include "t.h"


static int _open_port(const char *dev)
{
	int fd, val;
	struct ifreq r;
	struct sockaddr_can addr;

	if ((fd = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
		perror("[CAN] open");
		return -1;
	}

	strncpy(r.ifr_name, dev, IFNAMSIZ);
	ioctl(fd, SIOCGIFINDEX, &r);

	addr.can_family = AF_CAN;
	addr.can_ifindex = r.ifr_ifindex;
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("[CAN] bind");
		close(fd);
		return -1;
	}

	fcntl(fd, F_SETFL, O_NONBLOCK);

	return fd;
}


static int _tx_hb(int fd)
{
	int ret;
	struct can_frame f;

	f.can_id = 0x77f;
	f.len = 1;
	f.data[0] = 0x05;
	write(fd, &f, sizeof(f));

	return 0;
}


static int _rx_can(thread_t *t, int fd)
{
	int ret;
	struct can_frame f;

	do {
		int i;

		if ((ret = read(fd, &f, sizeof(f))) == sizeof(f)) {
			struct timeval tv;
			canmsg_t c;
			sq_elem_t e;

			/* grab the timestamp for this packet */
			ioctl(fd, SIOCGSTAMP, &tv);

			memset(&c, 0, sizeof(c));
			c.id = f.can_id;
			c.dlc = f.len;
			memcpy(&c.data, f.data, c.dlc);
			c.usec = tv.tv_sec * 1000000;
			c.usec += tv.tv_usec;

			e.dlen = sizeof(c);
			e.data = &c;
			e.flags = SQ_FLAG_VOLATILE;
			if ((ret = sq_publish(t->td.list, &e)) != SQ_ERR_NO_ERROR) {
				fprintf(stderr, "[CAN] sq_publish returned %d\n", ret);
			}
#if 0
			fprintf(stderr, "[CAN] rx %03x %02x", f.can_id, f.len);
			for (i = 0; i < f.len; i++) {
				fprintf(stderr, " %02hhx", f.data[i]);
			}
			fprintf(stderr, "\n");
#endif

		} else if (ret < 0 && errno != EAGAIN) {
			perror("[CAN]");
		}
	} while (ret == sizeof(f));

	return ret;
}


static int _rx_msg(thread_t *t)
{
	return 0;
}


void *can_thread_main(void *arg)
{
	int fd, ret;
	thread_t *t;
	unsigned long hb_time;

	t = (thread_t *)arg;

	/* getopt/etc. here */

	if ((fd = _open_port("can0")) < 0) {
		fprintf(stderr, "cannot open CAN interface\n");
		return NULL;
	}

	/* wait for all threads to start up */
	pthread_barrier_wait(t->pb);

	hb_time = 0;
	do {
		struct pollfd pfd;

		pfd.fd = fd;
		pfd.events = POLLIN | POLLERR;
		pfd.revents = 0;

		if ((ret = poll(&pfd, 1, 10)) > 0) {
			if (pfd.revents & POLLIN) {
				_rx_can(t, pfd.fd);
			}
		}

		if (_msg_timedwait(&t->td, 10) == 0) {
			_rx_msg(t);
		}
		pthread_mutex_unlock(&t->td.nd_mtx);


		if (now() >= hb_time) {
//			_tx_hb(fd);
			hb_time = now() + 500;
		}

		ret = 0;
	} while (ret == 0);

	return NULL;
}
