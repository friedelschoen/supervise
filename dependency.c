#include "dependency.h"

#include "buffer.h"
#include "lock.h"
#include "supervise.h"
#include "utils.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


int                dependency_count = 0;
struct supervisor* supervisors      = NULL;
int                nsupervisors     = 0;


void startsupervisor(struct supervisor* service) {
	while ((service->pid = fork()) == -1) {
		fprintf(stderr, "warn: unable to fork");
		sleep(1);
	}
	if (service->pid == 0) {
		/* child */
		execlp(myself, myself, "-d", service->name, NULL);
		fprintf(stderr, "error: unable to execute supervisor for %s: %s\n", service->name,
		        strerror(errno));
		_exit(1);
	}

	printf("started supervisor\n");

	return;
}

static void addsupervior(const char* service) {
	supervisors = realloc(supervisors, (nsupervisors + 1) * sizeof(*supervisors));

	strncpy(supervisors[nsupervisors].name, service, sizeof(supervisors[nsupervisors].name) - 1);
	startsupervisor(&supervisors[nsupervisors]);

	nsupervisors++;

	return;
}

static int sendcommand(const char* service, const char* command) {
	char path[PATH_MAX];
	int  control_fp;
	int  retries = 3;    // Retry a few times if the write fails

	/* Send start command to dependency */
	snprintf(path, sizeof(path), "%s/supervise/control", service);

	while ((control_fp = open(path, O_WRONLY | O_NONBLOCK)) == -1 && errno == ENOENT &&
	       errno == ENXIO) {
		setstatus(STATUS_WAITING);
		sleep(1);
	}

	if (control_fp == -1) {
		fprintf(stderr, "error: could not open control for %s\n", service);
		return -1;
	}

	while (retries-- > 0) {
		if (write(control_fp, command, strlen(command)) != -1) {
			break;    // Success
		} else if (errno != EAGAIN && errno != EWOULDBLOCK) {
			perror("Failed to send command to dependency");
		}
		sleep(1);    // Wait before retrying
	}
	close(control_fp);

	return 0;
}

void loaddependencies(void) {
	char  path[PATH_MAX];
	char *buffer, *depend;
	FILE* fp;
	int   ret;

	if (!(fp = fopen("depends", "r")))
		return;

	if (!(buffer = loadbuffermalloc(fp, NULL)))
		return;

	fclose(fp);

	/* Loop through dependencies */
	while ((depend = strsep(&buffer, "\n"))) {
		depend = strip(depend);
		if (*depend == '\0')
			continue;

		/* Check if the supervisor lock exists */
		snprintf(path, sizeof(path), "%s/supervise/lock", depend);
		ret = testlock(path);
		if (ret == -1) {
			fprintf(stderr, "warn: unable to test lock of service %s: %s\n", depend,
			        strerror(errno));
			continue;
		}
		if (ret == 0) {
			fprintf(stderr, "start supervisor for %s\n", depend);
			addsupervior(depend);
		} else {
			fprintf(stderr, "supervisor for %s already started\n", depend);
		}

		sendcommand(depend, "+");
	}
	free(buffer);
}
