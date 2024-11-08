#include "supervise.h"

#include "arg.h"
#include "handler.h"
#include "service.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>

int         restart       = 0;
pid_t       service       = 0;
time_t      status_change = 0;
int         status        = -1;
const char* servicedir    = NULL;
char        myself[PATH_MAX];

const char* status_names[] = { "waiting", "terminated", "crashed", "error", "running" };

void supervise_setstatus(int stat) {
	FILE* fp;

	if (stat == status)
		return;

	status        = stat;
	status_change = time(NULL);

	if (!(fp = fopen("supervise/status", "w+"))) {
		fprintf(stderr, "error: unable to open supervise/status: %s\n", strerror(errno));
		return;
	}
	fwrite(&status, sizeof(status), 1, fp);
	fwrite(&status_change, sizeof(status_change), 1, fp);
	fclose(fp);
}

void supervise_mainloop(void) {
	char  command;
	int   fd, stat, ret;
	pid_t pid;

	/* Create control FIFO for receiving commands */
	mkfifo("supervise/control", 0755);

	/* Open the FIFO in non-blocking mode */
	if ((fd = open("supervise/control", O_RDONLY | O_NONBLOCK)) == -1) {
		fprintf(stderr, "error: unable to open supervise/control: %s\n", strerror(errno));
		return;
	}

	struct pollfd pfd;
	pfd.fd     = fd;
	pfd.events = POLLIN | POLLHUP; /* We're interested in data to read */

	while (1) {
		if ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
			handler_child(pid, stat);

		if ((ret = poll(&pfd, 1, POLLINTERVAL)) <= 0)
			continue;

		/* Check if there's data to read */
		if (pfd.revents & POLLIN) {
			int n;

			while ((n = read(fd, &command, 1)) > 0)
				handler_command(command);

			if (n == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
				/* Error reading from FIFO */
				fprintf(stderr, "error: unable to read from supervise/control: %s\n",
				        strerror(errno));
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
		handler_child(pid, stat);
}

static int setservicedir(const char* inputservice) {
	const char* svdir;
	char*       base;
	char        path[PATH_MAX];

	if ((svdir = getenv("SVDIR")) && *svdir) {
		servicedir = svdir;
		return 0;
	}

	// Calculate absolute path for input_path
	if (!realpath(inputservice, path)) {
		fprintf(stderr, "error: unable to resolve path of servicedir: %s\n", strerror(errno));
		return -1;
	}

	// Extract directory from the path
	if ((base = strrchr(path, '/')))
		*base = '\0';

	servicedir = path;

	// Set SVDIR to the computed servicedir if not initially set
	if (setenv("SVDIR", servicedir, 1) != 0) {
		fprintf(stderr, "error: unable to set SVDIR: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

__attribute__((noreturn)) void usage(int exitcode) {
	printf("usage: supervise [-h] <directory>\n");
	exit(exitcode);
}

int main(int argc, char** argv) {
	int nostart = 0;

	signal(SIGCHLD, sigchild);

	if (!realpath(argv[0], myself))
		strncpy(myself, argv[0], sizeof(myself) - 1);

	ARGBEGIN
	switch (OPT) {
		case 'h':
			usage(0);
		case 's':
			nostart = 1;
			break;
	}
	ARGEND

	if (argc == 0) {
		fprintf(stderr, "error: too few arguments.\n");
		usage(1);
	}

	setservicedir(argv[0]);

	if (chdir(argv[0]) != 0) {
		char path2[PATH_MAX];
		snprintf(path2, sizeof(path2), "%s/%s", servicedir, argv[0]);

		if (chdir(path2) != 0) {
			fprintf(stderr, "error: could not change directory: %s\n", strerror(errno));
			return 1;
		}
	}

	mkdir("supervise", 0777);
	supervise_setstatus(STATUS_WAITING);

	if (!nostart) {
		restart = 1;
		service_start();
	}

	supervise_mainloop();
	return 0;
}
