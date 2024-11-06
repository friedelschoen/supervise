#include "supervise.h"

#include "arg.h"
#include "dependency.h"
#include "service.h"
#include "utils.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
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
const char* servicedir    = NULL;
char        myself[PATH_MAX];


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
	int         nostart = 0;
	const char* svdir;
	char        path[PATH_MAX];

	signal(SIGCHLD, sigchild);

	if (!realpath(argv[0], myself))
		strncpy(myself, argv[0], sizeof(myself) - 1);

	ARGBEGIN
	switch (OPT) {
		case 's':
			nostart = 1;
			break;
	}
	ARGEND

	if (argc == 0) {
		fprintf(stderr, "error: too few arguments.\n");
		return 1;
	}

	if ((svdir = getenv("SVDIR")) && *svdir) {
		servicedir = svdir;
	} else {
		// Calculate absolute path for input_path
		if (!realpath(argv[0], path)) {
			fprintf(stderr, "error: unable to get realpath of servicedir: %s\n", strerror(errno));
			return 1;
		}

		// Extract directory from the path
		char* last_slash = strrchr(path, '/');
		if (last_slash) {
			*last_slash = '\0';
		} else {
			fprintf(stderr, "error: invalid path provided\n");
			return 1;
		}

		servicedir = path;

		// Set SVDIR to the computed servicedir if not initially set
		if (setenv("SVDIR", servicedir, 1) != 0) {
			fprintf(stderr, "error: unable to set SVDIR: %s\n", strerror(errno));
			return 1;
		}
	}

	if (chdir(argv[0]) != 0) {
		char path2[PATH_MAX];
		snprintf(path2, sizeof(path2), "%s/%s", servicedir, argv[0]);

		if (chdir(path2) != 0) {
			perror("error: could not change directory");
			return 1;
		}
	}

	mkdir("supervise", 0777);
	setstatus(STATUS_WAITING);

	if (!nostart) {
		restart = 1;
		startservice();
	}

	controlloop();
	return 0;
}
