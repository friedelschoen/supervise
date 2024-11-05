
#include "dependency.h"
#include "service.h"
#include "supervise.h"
#include "utils.h"

#include <ctype.h>
#include <stdio.h>
#include <sys/wait.h>


void handlecommand(int command) {
	if (!isgraph(command))
		return;

	switch (command) {
		case 's': /* Start command */
			startservice();
			break;
		case 't': /* Stop command */
			stopservice();
			break;
		case '+': /* Dependency + */
			dependency_count++;
			if (dependency_count > 0)
				startservice();
			break;
		case '-': /* Dependency - */
			dependency_count--;
			if (dependency_count == 0)
				stopservice();
			break;
		default:
			fprintf(stderr, "warn: unknown command: %c\n", command);
			break;
	}
}

void handlechild(pid_t pid, int stat) {
	for (int i = 0; i < nsupervisors; i++)
		if (pid == supervisors[i].pid) {
			printf("supervisor %s stopped\n", supervisors[i].name);
			startsupervisor(&supervisors[i]);
			return;
		}

	if (status != STATUS_RUNNING || pid != service)
		return;

	setstatus(WIFSIGNALED(stat) ? STATUS_CRASHED : STATUS_EXITED);

	if (restart || dependency_count > 0)
		startservice();
	else
		disabledependencies();
}
