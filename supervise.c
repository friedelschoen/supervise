#include "arg.h"
#include "buffer.h"
#include "lock.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define POLLINTERVAL 10000 /* poll every 10sec */

enum {
	STATUS_WAITING,
	STATUS_EXITED,
	STATUS_CRASHED,
	STATUS_ERROR,
	STATUS_RUNNING,
};

struct supervisor {
	char  name[NAME_MAX];
	pid_t pid;
};

time_t      status_change    = 0;
int         status           = STATUS_EXITED;
int         dependency_count = 0;
pid_t       service          = 0;
int         restart          = 0;
const char* servicedir;
char        myself[PATH_MAX];

struct supervisor* supervisors  = NULL;
int                nsupervisors = 0;

const char* status_names[] = {
	[STATUS_WAITING] = "waiting", [STATUS_EXITED] = "terminated", [STATUS_CRASHED] = "crashed",
	[STATUS_ERROR] = "error",     [STATUS_RUNNING] = "running",
};


/* Function to set the current status and update the status file */
static void setstatus(int stat) {
	FILE* fp;

	if (stat == status) /* already set */
		return;

	status        = stat;
	status_change = time(NULL);

	fprintf(stderr, "set status %s\n", status_names[stat]);

	fp = fopen("supervise/status", "w+");

	if (fp) {
		fwrite(&status, sizeof(status), 1, fp);
		fwrite(&status_change, sizeof(status_change), 1, fp);
		fclose(fp);
	} else {
		perror("Error opening status file");
	}
}

void startservice(void) {
	if (status == STATUS_RUNNING)
		return;

	while ((service = fork()) == -1) {
		fprintf(stderr, "warn: unable to fork");
		sleep(1);
	}

	if (service == 0) {
		/* child */
		execl("./run", "./run", NULL);
		fprintf(stderr, "error: unable to execute service: %s\n", strerror(errno));
		_exit(1);
	}
	printf("- %d\n", service);

	setstatus(STATUS_RUNNING);
}

void stopservice(void) {
	if (status != STATUS_RUNNING)
		return;

	kill(status, SIGTERM);
}

static char* strip(char* str) {
	char* end;
	while (isspace((unsigned char) *str)) /* Cast to unsigned char for isspace */
		str++;
	end = strchr(str, '\0') - 1;
	while (end > str && isspace((unsigned char) *end))
		*end-- = '\0';
	return str;
}

static void startsupervisor(struct supervisor* service) {
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

/* Function to load dependencies and check readiness */
static void loaddependencies(void) {
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

		/* Send start command to dependency */
		snprintf(path, sizeof(path), "%s/supervise/control", depend);
		int control_fp;
		int retries = 3;    // Retry a few times if the write fails

		while ((control_fp = open(path, O_WRONLY)) == -1 && errno == ENOENT) {
			setstatus(STATUS_WAITING);
			sleep(1);
		}

		if (control_fp != -1) {
			while (retries-- > 0) {
				if (write(control_fp, "+", 1) != -1) {
					break;    // Success
				} else if (errno != EAGAIN && errno != EWOULDBLOCK) {
					perror("Failed to send command to dependency");
				}
				sleep(1);    // Wait before retrying
			}
			close(control_fp);
		} else {
			fprintf(stderr, "error: could not open control for %s\n", depend);
		}
	}
	free(buffer);
}


void handlecommand(int command) {
	printf("%s, command %c\n", servicedir, command);
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
			fprintf(stderr, "Unknown command: %c\n", command);
			break;
	}
}

static void handlechild(pid_t pid, int stat) {
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
}

void controlloop(void) {
	char  command;
	int   stat, ret;
	pid_t pid;

	/* Create control FIFO for receiving commands */
	mkfifo("supervise/control", 0755);

	/* Open the FIFO in non-blocking mode */
	int fd = open("supervise/control", O_RDONLY | O_NONBLOCK);
	if (fd == -1) {
		perror("Error opening FIFO");
		return;
	}

	struct pollfd pfd;
	pfd.fd     = fd;
	pfd.events = POLLIN; /* We're interested in data to read */

	while (1) {
		if ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
			handlechild(pid, stat);

		if ((ret = poll(&pfd, 1, POLLINTERVAL)) <= 0)
			continue;

		/* Check if there's data to read */
		if (pfd.revents & POLLIN) {
			int n;

			while ((n = read(fd, &command, 1)) > 0)
				handlecommand(command);

			if (n == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
				/* Error reading from FIFO */
				perror("Error reading FIFO");
				break;
			}
		}
	}
	close(fd);
}

static void sigchild(int signum) {
	(void) signum;

	pid_t pid;
	int   stat;

	if ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
		handlechild(pid, stat);
}

int main(int argc, char** argv) {
	int lockfd;
	int asdependency = 0;

	signal(SIGCHLD, sigchild);

	if (!realpath(argv[0], myself))
		strncpy(myself, argv[0], sizeof(myself) - 1);

	ARGBEGIN
	switch (OPT) {
		case 'd':
			asdependency = 1;
	}
	ARGEND

	if (argc == 0) {
		fprintf(stderr, "error: too few arguments.\n");
		return 1;
	}

	servicedir = argv[0];

	/* Change to the service directory */
	if (chdir(servicedir) != 0) {
		perror("error: could not change directory");
		return 1;
	}

	/* Set up supervision directory and run control loop */
	mkdir("supervise", 0777);
	setstatus(STATUS_WAITING);

	lockfd = acquirelock();

	/* Load dependencies and check readiness */
	loaddependencies();

	if (!asdependency) {
		restart = 1;

		startservice();
	}

	controlloop();

	releaselock(lockfd);
	return 0;
}
