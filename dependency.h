#pragma once

#include <limits.h>
#include <sys/types.h>

/* Structure for supervisor */
struct supervisor {
	char  name[NAME_MAX];
	pid_t pid;
};

/* Global variables */
extern int                dependency_count;
extern struct supervisor* supervisors;
extern int                nsupervisors;

/* Functions */
void loopdependencies(void (*callback)(char*));
void disabledependency(char* depend);
void enabledependency(char* depend);

void handlecommand(int command);
void handlechild(pid_t pid, int stat);

void startsupervisor(struct supervisor* service);
