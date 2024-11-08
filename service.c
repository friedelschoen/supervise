#include "service.h"

#include "dependency.h"
#include "supervise.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>


extern int   status;
extern pid_t service_pid;

void service_start(void) {
	if (status == STATUS_RUNNING)
		return;

	service_terminated = 0;
	dependency_iterator(dependency_enable);

	while ((service_pid = fork()) == -1) {
		fprintf(stderr, "warn: unable to fork: %s, retrying...\n", strerror(errno));
		sleep(1);
	}

	if (service_pid == -1) {
		fprintf(stderr, "error: unable to fork: %s\n", strerror(errno));
		return;
	}

	if (service_pid == 0) {
		execl("./run", "./run", NULL);
		fprintf(stderr, "error: unable to execute service: %s\n", strerror(errno));
		_exit(1);
	}
	supervise_setstatus(STATUS_RUNNING);
}
