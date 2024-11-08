#include "handler.h"

#include "dependency.h"
#include "service.h"
#include "supervise.h"

#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>


int signalmap[] = {
	['t'] = SIGTERM, ['k'] = SIGKILL, ['p'] = SIGSTOP, ['c'] = SIGCONT, ['a'] = SIGALRM,
	['h'] = SIGHUP,  ['i'] = SIGINT,  ['q'] = SIGQUIT, ['1'] = SIGUSR1, ['2'] = SIGUSR2,
};

/*
up    (u): starts the services, pin as started
down  (d): stops the service, pin as stopped
once  (o): starts the service, pin as started once
term  (t): same as down
kill  (k): sends kill, pin as stopped
pause (p): pauses the service
cont  (c): resumes the service
alarm (a): sends alarm
hup   (h): sends hup
int   (i): sends interrupt
quit  (q): sends quit
usr1  (1): sends usr1
usr2  (2): sends usr2
exit  (x): does nothing (actually exits the runsv instance)
*/
void handler_command(int command) {
	if (!isgraph(command))
		return;

	switch (command) {
		case 'u': /* Start command */
		case 'o': /* Start command */
			restart = command = 'u';
			service_start();
			break;

		case 'd': /* term: sends term, don't restart */
		case 't': /* term: sends term */
		case 'k': /* kill: sends kill, pin as stopped */
		case 'p': /* pause: pauses the service */
		case 'c': /* cont: resumes the service */
		case 'a': /* alarm: sends alarm */
		case 'h': /* hup: sends hup */
		case 'i': /* int: sends interrupt */
		case 'q': /* quit: sends quit */
		case '1': /* usr1: sends usr1 */
		case '2': /* usr2: sends usr2 */
			if (status != STATUS_RUNNING)
				break;

			if (command == 'd')
				restart = 0;

			kill(service_pid, signalmap[command]);
			break;

		case '+': /* Dependency + */
			dependency_count++;
			if (dependency_count > 0)
				service_start();
			break;
		case '-': /* Dependency - */
			dependency_count--;
			if (dependency_count == 0 && !restart && status == STATUS_RUNNING)
				kill(service_pid, SIGTERM);
			break;
	}
}

void handler_child(pid_t pid, int stat) {
	for (int i = 0; i < nsupervisors; i++)
		if (pid == supervisors[i].pid) {
			dependency_start(&supervisors[i]);
			return;
		}

	if (status != STATUS_RUNNING || pid != service_pid)
		return;

	supervise_setstatus(WIFSIGNALED(stat) ? STATUS_CRASHED : STATUS_EXITED);
	service_terminated = WIFSIGNALED(stat) && WTERMSIG(stat) == SIGTERM;

	if (restart || dependency_count > 0)
		service_start();
	else
		dependency_iterator(dependency_disable);
}
