#include "supervise.h"

#include "arg.h"
#include "dependency.h"
#include "handler.h"
#include "service.h"

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define TAI_OFFSET 4611686018427387914ULL /* seconds */

int         restart            = 0;
pid_t       service_pid        = 0;
time_t      status_change      = 0;
int         status             = -1;
int         service_terminated = 0;
const char* servicedir         = NULL;
char        myself[PATH_MAX];

const char* status_names[] = { "waiting", "terminated", "crashed", "error", "running" };

/*
% ls supervise
|rw------- control	- w-opened FIFO
.rw------- lock		- flock-locked file
|rw------- ok		- w-opened FIFO
.rw-r--r-- pid		- human-readable pid
.rw-r--r-- stat     - human-readable status
.rw-r--r-- status	- machine-readable status
*/
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

	uint64_t tai_seconds = status_change + TAI_OFFSET;
	tai_seconds          = htobe64(tai_seconds);    // Convert to big-endian

	// Write STATUS CHANGE (TAI format, Little endian)
	fwrite(&tai_seconds, sizeof(tai_seconds), 1, fp);

	// Write NANOSEC (Unix nanoseconds)
	uint32_t nanosec = 0;
	fwrite(&nanosec, sizeof(nanosec), 1, fp);

	// Write PID (Little endian)
	uint32_t pid_be = htole32(service_pid);
	fwrite(&pid_be, sizeof(pid_be), 1, fp);

	// Write PS, WU, TR, SR fields as single bytes
	uint8_t ps = 0;                                           /* is paused */
	uint8_t wu = restart || dependency_count > 0 ? 'u' : 'd'; /* wants up */
	uint8_t tr = service_terminated;                          /* is terminated */
	uint8_t sr = status == STATUS_RUNNING; /* state (0 is down, 1 is running, 2 is finishing) */

	fwrite(&ps, sizeof(ps), 1, fp);
	fwrite(&wu, sizeof(wu), 1, fp);
	fwrite(&tr, sizeof(tr), 1, fp);
	fwrite(&sr, sizeof(sr), 1, fp);

	fclose(fp);

	if (!(fp = fopen("supervise/stat", "w+"))) {
		fprintf(stderr, "error: unable to open supervise/stat: %s\n", strerror(errno));
		return;
	}

	fprintf(fp, "%s", status_names[status]);
	fclose(fp);

	if (!(fp = fopen("supervise/pid", "w+"))) {
		fprintf(stderr, "error: unable to open supervise/pid: %s\n", strerror(errno));
		return;
	}

	if (status == STATUS_RUNNING) {
		fprintf(fp, "%d", service_pid);
	}
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

	int okfd = 0, lockfd = 0;

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

	mkfifo("supervise/ok", 0600);
	if ((okfd = open("supervise/ok", O_RDONLY | O_NONBLOCK)) == -1) {
		fprintf(stderr, "error: unable to open supervise/ok: %s\n", strerror(errno));
		return 1;
	}

	if ((lockfd = open("supervise/lock", O_APPEND | O_CREAT, 0600)) == -1) {
		fprintf(stderr, "error: unable to open supervise/lock: %s\n", strerror(errno));
		return 1;
	}
	if (flock(lockfd, LOCK_EX | LOCK_NB)) {
		fprintf(stderr, "error: unable to lock supervise/lock: %s\n", strerror(errno));
		return 1;
	}

	if (!nostart) {
		restart = 1;
		service_start();
	}

	supervise_mainloop();

	/* clean-up */
	close(okfd);
	close(lockfd);
	return 0;
}
