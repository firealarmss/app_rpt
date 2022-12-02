
/*! \file
 *
 * \brief Generic serial I/O routines
 */

#include "asterisk.h"

#include <termios.h>

#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"

#include "app_rpt.h"
#include "rpt_serial.h"

int serial_open(char *fname, int speed, int stop2)
{
	struct termios mode;
	int fd;

	fd = open(fname, O_RDWR);
	if (fd == -1) {
		ast_log(LOG_WARNING, "Cannot open serial port %s\n", fname);
		return -1;
	}

	memset(&mode, 0, sizeof(mode));
	if (tcgetattr(fd, &mode)) {
		ast_log(LOG_WARNING, "Unable to get serial parameters on %s: %s\n", fname, strerror(errno));
		return -1;
	}
#ifndef	SOLARIS
	cfmakeraw(&mode);
#else
	mode.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	mode.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	mode.c_cflag &= ~(CSIZE | PARENB | CRTSCTS);
	mode.c_cflag |= CS8;
	if (stop2)
		mode.c_cflag |= CSTOPB;
	mode.c_cc[VTIME] = 3;
	mode.c_cc[VMIN] = 1;
#endif

	cfsetispeed(&mode, speed);
	cfsetospeed(&mode, speed);
	if (tcsetattr(fd, TCSANOW, &mode)) {
		ast_log(LOG_WARNING, "Unable to set serial parameters on %s: %s\n", fname, strerror(errno));
		return -1;
	}
	usleep(100000);
	ast_debug(3, "Opened serial port %s\n", fname);
	return (fd);
}

int serial_rxready(int fd, int timeoutms)
{
	int myms = timeoutms;

	return (ast_waitfor_n_fd(&fd, 1, &myms, NULL));
}

int serial_rxflush(int fd, int timeoutms)
{
	int res, flushed = 0;
	char c;

	while ((res = serial_rxready(fd, timeoutms)) == 1) {
		if (read(fd, &c, 1) == -1) {
			res = -1;
			break;
			flushed++;
		}
	}
	return (res == -1) ? res : flushed;
}

int serial_rx(int fd, char *rxbuf, int rxmaxbytes, unsigned timeoutms, char termchr)
{
	char c;
	int i, j, res;

	if ((!rxmaxbytes) || (rxbuf == NULL)) {
		return 0;
	}
	memset(rxbuf, 0, rxmaxbytes);
	for (i = 0; i < rxmaxbytes; i++) {
		if (timeoutms) {
			res = serial_rxready(fd, timeoutms);
			if (res < 0)
				return -1;
			if (!res) {
				break;
			}
		}
		j = read(fd, &c, 1);
		if (j == -1) {
			ast_log(LOG_WARNING, "read failed: %s\n", strerror(errno));
			return -1;
		}
		if (j == 0)
			return i;
		rxbuf[i] = c;
		if (termchr) {
			rxbuf[i + 1] = 0;
			if (c == termchr)
				break;
		}
	}
	if (i && rpt_debug_level() >= 6) {
		ast_debug(6, "i = %d\n", i);
		ast_debug(6, "String returned was:\n");
		for (j = 0; j < i; j++)
			ast_debug(6, "%02X ", (unsigned char) rxbuf[j]);
		ast_debug(6, "\n");
	}
	return i;
}

int serial_txstring(int fd, char *txstring)
{
	int txbytes;

	txbytes = strlen(txstring);

	ast_debug(6, "sending: %s\n", txstring);

	if (write(fd, txstring, txbytes) != txbytes) {
		ast_log(LOG_WARNING, "write failed: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int serial_io(int fd, char *txbuf, char *rxbuf, int txbytes, int rxmaxbytes, unsigned int timeoutms, char termchr)
{
	int i;

	ast_debug(7, "fd = %d\n", fd);

	if ((rxmaxbytes) && (rxbuf != NULL)) {
		if ((i = serial_rxflush(fd, 10)) == -1)
			return -1;
		ast_debug(7, "%d bytes flushed prior to write\n", i);
	}

	if (write(fd, txbuf, txbytes) != txbytes) {
		ast_log(LOG_WARNING, "write failed: %s\n", strerror(errno));
		return -1;
	}

	return serial_rx(fd, rxbuf, rxmaxbytes, timeoutms, termchr);
}

int setdtr(struct rpt *myrpt, int fd, int enable)
{
	struct termios mode;

	if (fd < 0)
		return -1;
	if (tcgetattr(fd, &mode)) {
		ast_log(LOG_WARNING, "Unable to get serial parameters for dtr: %s\n", strerror(errno));
		return -1;
	}
	if (enable) {
		cfsetspeed(&mode, myrpt->p.iospeed);
	} else {
		cfsetspeed(&mode, B0);
		usleep(100000);
	}
	if (tcsetattr(fd, TCSADRAIN, &mode)) {
		ast_log(LOG_WARNING, "Unable to set serial parameters for dtr: %s\n", strerror(errno));
		return -1;
	}
	if (enable)
		usleep(100000);
	return 0;
}

int openserial(struct rpt *myrpt, char *fname)
{
	struct termios mode;
	int fd;

	fd = open(fname, O_RDWR);
	if (fd == -1) {
		ast_log(LOG_WARNING, "Cannot open serial port %s\n", fname);
		return -1;
	}
	memset(&mode, 0, sizeof(mode));
	if (tcgetattr(fd, &mode)) {
		ast_log(LOG_WARNING, "Unable to get serial parameters on %s: %s\n", fname, strerror(errno));
		return -1;
	}
#ifndef	SOLARIS
	cfmakeraw(&mode);
#else
	mode.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	mode.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	mode.c_cflag &= ~(CSIZE | PARENB | CRTSCTS);
	mode.c_cflag |= CS8;
	mode.c_cc[VTIME] = 3;
	mode.c_cc[VMIN] = 1;
#endif

	cfsetispeed(&mode, myrpt->p.iospeed);
	cfsetospeed(&mode, myrpt->p.iospeed);
	if (tcsetattr(fd, TCSANOW, &mode))
		ast_log(LOG_WARNING, "Unable to set serial parameters on %s: %s\n", fname, strerror(errno));
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD))
		setdtr(myrpt, fd, 0);
	usleep(100000);
	ast_debug(1, "Opened serial port %s\n", fname);
	return (fd);
}