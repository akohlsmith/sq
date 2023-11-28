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

static int _tx(int fd)
{
	int ret;
	const unsigned char buf[] = { 0x1f, 0x01, 0x23, 0x00 };

	tcflush(fd, TCOFLUSH);
	if ((ret = write(fd, buf, sizeof(buf))) != sizeof(buf)) {
		perror("[CONBATT] write");
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

	speed_t s;

	s = speed;
	ret = ioctl(fd, IOSSIOSPEED, s);

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

	fd = open(port, O_WRONLY | O_NONBLOCK | O_SYNC | O_NOCTTY);
	if (fd < 0) {
		perror("[CONBATT] open");
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
	if ((ret = tcsetattr(fd, TCSANOW, &tio)) == 0) {
		ret = set_speed(fd, 5000000);
	}

	/* if things didn't work out, close and return error */
	if (ret < 0) {
		perror("[CONBATT] speed");
		close(fd);
		fd = -1;
	}

	return fd;
}


void *conbatt_thread_main(void *arg)
{
	int ret, fd;
	thread_t *t;
	unsigned long next_tx;

	t = (thread_t *)arg;

	/* getopt/etc. here */

	fd = open_port("/dev/ttySC0");

	/* wait for all threads to start up */
	pthread_barrier_wait(t->pb);

	tcflush(fd, TCIOFLUSH);
	do {
		/* wait one msec for messages from other threads */
		if (_msg_timedwait(&t->td, 1) == 0) {
			dequeue(&t->td);
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
