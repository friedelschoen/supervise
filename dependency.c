#include "dependency.h"

#include "buffer.h"
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
		fprintf(stderr, "warn: unable to fork: %s\n", strerror(errno));
		sleep(1);
	}
	if (service->pid == 0) {
		/* child */
		execlp(myself, myself, "-s", service->name, NULL);
		fprintf(stderr, "error: unable to execute supervisor for %s: %s\n", service->name,
		        strerror(errno));
		_exit(1);
	}

	return;
}

static void addsupervior(const char* service) {
	supervisors = realloc(supervisors, (nsupervisors + 1) * sizeof(*supervisors));

	strncpy(supervisors[nsupervisors].name, service, sizeof(supervisors[nsupervisors].name) - 1);
	startsupervisor(&supervisors[nsupervisors]);

	nsupervisors++;

	return;
}

static int sendcommand(const char* service, int startit, const char* command) {
	char path[PATH_MAX];
	int  control_fp;
	int  retries = 5;    // Retry a few times if the write fails

	/* Send start command to dependency */
	snprintf(path, sizeof(path), "%s/supervise/control", service);

	while ((control_fp = open(path, O_WRONLY | O_NONBLOCK)) == -1 &&
	       (errno == ENOENT || errno == ENXIO)) {

		setstatus(STATUS_WAITING);
		if (errno == ENXIO && startit)
			addsupervior(service);

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

void enabledependencies(void) {
	char *buffer, *depend;
	FILE* fp;

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

		sendcommand(depend, 1, "+");
	}
	free(buffer);
}

void disabledependencies(void) {
	char *buffer, *depend;
	FILE* fp;

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

		sendcommand(depend, 0, "-");
	}
	free(buffer);
}
