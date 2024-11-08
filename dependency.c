#include "dependency.h"

#include "buffer.h"
#include "config.h"
#include "supervise.h"
#include "utils.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


int                dependency_count = 0;
struct supervisor* supervisors      = NULL;
int                nsupervisors     = 0;

char** enableddeps     = NULL;
int    enableddeps_len = 0;
int    enableddeps_cap = 0;

/* Check if a dependency is enabled */
static int dependency_is_enabled(const char* dependency) {
	for (int i = 0; i < enableddeps_len; i++) {
		if (strcmp(enableddeps[i], dependency) == 0) {
			return 1;
		}
	}
	return 0;
}

int dependency_start(struct supervisor* service) {
	while ((service->pid = fork()) == -1) {
		fprintf(stderr, "warn: unable to fork: %s, retrying...\n", strerror(errno));
		sleep(1);
	}
	if (service->pid == -1) {
		fprintf(stderr, "error: unable to fork: %s\n", strerror(errno));
		return -1;
	}

	if (service->pid == 0) {
		execlp(myself, myself, "-s", service->name, NULL);
		fprintf(stderr, "error: unable to execute supervisor for %s: %s\n", service->name,
		        strerror(errno));
		_exit(1);
	}
	return 0;
}

static void dependency_add(const char* service) {
	supervisors = realloc(supervisors, (nsupervisors + 1) * sizeof(*supervisors));
	strncpy(supervisors[nsupervisors].name, service, sizeof(supervisors[nsupervisors].name) - 1);
	dependency_start(&supervisors[nsupervisors]);
	nsupervisors++;
}

static int sendcommand(const char* service, int startit, const char* command) {
	char path[PATH_MAX];
	int  control_fp;
	int  retries = sendcommand_retries;

	snprintf(path, sizeof(path), "%s/supervise/control", service);

	while ((control_fp = open(path, O_WRONLY | O_NONBLOCK)) == -1 &&
	       (errno == ENOENT || errno == ENXIO)) {

		supervise_setstatus(STATUS_WAITING);
		if (errno == ENXIO && startit)
			dependency_add(service);

		sleep(sendcommand_interval);
	}

	if (control_fp == -1) {
		fprintf(stderr, "error: unable to open supervise/control for %s: %s\n", service,
		        strerror(errno));
		return -1;
	}

	while (retries-- > 0 && write(control_fp, command, strlen(command)) >= 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			fprintf(stderr, "warn: unable to send command to %s: %s, retrying...\n", service,
			        strerror(errno));
		sleep(sendcommand_interval);
	}
	close(control_fp);

	return 0;
}

void dependency_iterator(void (*callback)(const char*)) {
	char *buffer, *depend;
	char  path[PATH_MAX];
	FILE* fp;

	if (!(fp = fopen("depends", "r")))
		return;

	if (!(buffer = malloc_load_buffer(fp, NULL)))
		return;

	fclose(fp);

	while ((depend = strsep(&buffer, "\n"))) {
		depend = strip(depend);
		if (*depend == '\0')
			continue;

		if (access(depend, F_OK) != 0) {
			snprintf(path, sizeof(path), "%s/%s", servicedir, depend);
			if (access(path, F_OK) != 0) {
				fprintf(stderr, "error: dependency not found: %s\n", depend);
				continue;
			}
			depend = path;
		}

		callback(depend);
	}
	free(buffer);
}

/* Enable a dependency if not already enabled */
void dependency_enable(const char* depend) {
	if (dependency_is_enabled(depend))
		return;

	sendcommand(depend, 1, "+");

	/* Expand the array if capacity is reached */
	if (enableddeps_len >= enableddeps_cap) {
		enableddeps_cap += ENABLEDDEPS_ALLOCATE;
		enableddeps = realloc(enableddeps, enableddeps_cap * sizeof(char*));
		assert(enableddeps != NULL);
	}

	/* Add the new dependency */
	enableddeps[enableddeps_len] = strdup(depend);
	assert(enableddeps[enableddeps_len] != NULL);
	enableddeps_len++;
}

/* Disable a dependency if currently enabled */
void dependency_disable(const char* depend) {
	if (!dependency_is_enabled(depend))
		return;

	sendcommand(depend, 0, "-");

	for (int i = 0; i < enableddeps_len; i++) {
		if (strcmp(enableddeps[i], depend) != 0)
			continue;

		/* Free the entry */
		free(enableddeps[i]);

		/* Move the last element to fill the gap */
		if (i < enableddeps_len - 1)
			enableddeps[i] = enableddeps[enableddeps_len - 1];

		enableddeps_len--;

		return;
	}
}

void enableddeps_free(void) {
	for (int i = 0; i < enableddeps_len; i++)
		free(enableddeps[i]);

	free(enableddeps);
	enableddeps     = NULL;
	enableddeps_len = 0;
	enableddeps_cap = 0;
}
