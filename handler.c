#include "handler.h"

#include "dependency.h"
#include "service.h"
#include "supervise.h"

#include <ctype.h>
#include <sys/wait.h>


void handler_command(int command) {
	if (!isgraph(command))
		return;

	switch (command) {
		case 's': /* Start command */
			restart = 1;
			service_start();
			break;
		case 't': /* Stop command */
			restart = 0;
			service_stop();
			break;
		case '+': /* Dependency + */
			dependency_count++;
			if (dependency_count > 0)
				service_start();
			break;
		case '-': /* Dependency - */
			dependency_count--;
			if (dependency_count == 0)
				service_stop();
			break;
	}
}

void handler_child(pid_t pid, int stat) {
	for (int i = 0; i < nsupervisors; i++)
		if (pid == supervisors[i].pid) {
			dependency_start(&supervisors[i]);
			return;
		}

	if (status != STATUS_RUNNING || pid != service)
		return;

	supervise_setstatus(WIFSIGNALED(stat) ? STATUS_CRASHED : STATUS_EXITED);

	if (restart || dependency_count > 0)
		service_start();
	else
		dependency_iterator(dependency_disable);
}
