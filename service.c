#include "service.h"

#include "dependency.h"
#include "supervise.h"
#include "utils.h"

#include <signal.h>
#include <stdio.h>
#include <unistd.h>


extern int   status;
extern pid_t service;

void startservice(void) {
	if (status == STATUS_RUNNING)
		return;

	loopdependencies(enabledependency);

	while ((service = fork()) == -1) {
		fprintf(stderr, "warn: unable to fork\n");
		sleep(1);
	}

	if (service == 0) {
		execl("./run", "./run", NULL);
		perror("error: unable to execute service");
		_exit(1);
	}
	setstatus(STATUS_RUNNING);
}

void stopservice(void) {
	if (status != STATUS_RUNNING)
		return;

	kill(service, SIGTERM);
}
