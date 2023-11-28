#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <poll.h>


/*
 * need asm/termbits for custom baudrate, but need termios
 * for cfmakeraw/tcflush/etc. Can't have both.
 *
 * I'm sure I'm holding it wrong.
 */
#ifdef __linux__
#include <asm/termbits.h>
#include <linux/serial.h>
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
void cfmakeraw(struct termios *termios_p);
int tcflush(int fd, int queue_selector);

#else
#include <termios.h>
#endif

#include "barrier.h"
#include "sq.h"
#include "t.h"

#define THREAD_NAME "conbatt"

static thread_data_t *td;

static int _tx(int fd)
{
	int ret;
	const unsigned char buf[] = { 0x1f, 0x01, 0x23, 0x00 };

	tcflush(fd, TCOFLUSH);
	if ((ret = write(fd, buf, sizeof(buf))) != sizeof(buf)) {
		perror("[" THREAD_NAME "] write");
	}

	return ret;
}


static int set_speed(int fd, unsigned int speed)
{
	int ret;

#ifdef __APPLE__
#ifndef IOSSIOSPEED
#define IOSSIOSPEED _IOW('T', 2, speed_t)
#endif
	ret = 0;

#else
	struct termios2 tio2;

	ret = ioctl(fd, TCGETS2, &tio2);
	tio2.c_cflag &= ~CBAUD;
	tio2.c_cflag |= BOTHER;
	tio2.c_ispeed = 5000000;
	tio2.c_ospeed = 5000000;
	ret = ioctl(fd, TCSETSF2, &tio2);
#endif

	return ret;
}


static int open_port(const char *port)
{
	int fd, ret;
	struct termios tio;
	speed_t s;

	fd = open(port, O_WRONLY | O_NONBLOCK | O_SYNC | O_NOCTTY);
	if (fd < 0) {
		perror("[" THREAD_NAME "] open");
		return fd;
	}

	cfmakeraw(&tio);
	tio.c_cc[VMIN] = 0;
	tio.c_cc[VTIME] = 10;

    	//ret = tcgetattr(fd, &tio);
	tio.c_cflag &= ~(CSTOPB | PARENB | CSIZE);
	tio.c_cflag |= CS8;

	tio.c_cflag &= ~CREAD;
	tio.c_cflag |= CLOCAL;
	tcsetattr(fd, TCSANOW, &tio);

	set_speed(fd, 5000000);
	return fd;
}


void t1_subscribe(sq_t *q)
{
	td->list = sq_list_add(&td->list, q);
}


void *thread1(void *arg)
{
	int ret, fd;
	thread_t *t;
	unsigned long next_tx;

	t = (thread_t *)arg;

	/* getopt/etc. here */

	if ((td = _td(THREAD_NAME, QUEUE_LENGTH)) == NULL) {
		return NULL;
	}

	fd = open_port("/dev/ttySC0");

	/* wait for all threads to start up */
	pthread_barrier_wait((pthread_barrier_t *)arg);

	tcflush(fd, TCIOFLUSH);
	do {
		/* wait one msec for messages from other threads */
		if (_msg_timedwait(td, 1) == 0) {
			dequeue(td);
		}

		/* is it time to transmit? */
		if (now() > next_tx) {
			_tx(fd);
			next_tx = now() + 10;
		}
	} while (ret == 0);

	close(fd);
	return NULL;
}
