#include "supervise.h"

#include "arg.h"
#include "dependency.h"
#include "lock.h"
#include "service.h"
#include "utils.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>


int         restart       = 0;
pid_t       service       = 0;
time_t      status_change = 0;
int         status        = -1;
char        myself[PATH_MAX];
const char* servicename = 0;


void controlloop(void) {
	char  command;
	int   stat, ret;
	pid_t pid;

	/* Create control FIFO for receiving commands */
	mkfifo("supervise/control", 0755);

	/* Open the FIFO in non-blocking mode */
	int fd = open("supervise/control", O_RDONLY | O_NONBLOCK);
	if (fd == -1) {
		perror("Error opening FIFO");
		return;
	}

	struct pollfd pfd;
	pfd.fd     = fd;
	pfd.events = POLLIN | POLLHUP; /* We're interested in data to read */

	while (1) {
		if ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
			handlechild(pid, stat);

		if ((ret = poll(&pfd, 1, POLLINTERVAL)) <= 0)
			continue;

		/* Check if there's data to read */
		if (pfd.revents & POLLIN) {
			int n;

			while ((n = read(fd, &command, 1)) > 0)
				handlecommand(command);

			if (n == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
				/* Error reading from FIFO */
				perror("Error reading FIFO");
				break;
			}
		} else if (pfd.revents & POLLHUP) {
			// we do not have any writers, waitin'...
			sleep(5);
		}
	}
	close(fd);
}

static void sigchild(int signum) {
	(void) signum;
	pid_t pid;
	int   stat;

	if ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
		handlechild(pid, stat);
}

int main(int argc, char** argv) {
	int         lockfd;
	int         asdependency = 0;
	const char* servicedir;

	signal(SIGCHLD, sigchild);

	if (!realpath(argv[0], myself))
		strncpy(myself, argv[0], sizeof(myself) - 1);

	ARGBEGIN
	switch (OPT) {
		case 'd':
			asdependency = 1;
			break;
	}
	ARGEND

	if (argc == 0) {
		fprintf(stderr, "error: too few arguments.\n");
		return 1;
	}

	servicedir = argv[0];
	if (chdir(servicedir) != 0) {
		perror("error: could not change directory");
		return 1;
	}

	servicename = strchr(servicedir, '/') ? strrchr(servicedir, '/') + 1 : servicedir;

	fprintf(stderr, "%s :: starting supervisor\n", servicename);

	mkdir("supervise", 0777);
	setstatus(STATUS_WAITING);

	lockfd = acquirelock();

	fprintf(stderr, "%s :: starting dependencies\n", servicename);
	loaddependencies();

	if (!asdependency) {
		restart = 1;
		startservice();
	}

	fprintf(stderr, "%s :: listening for commands\n", servicename);
	controlloop();
	releaselock(lockfd);
	return 0;
}
