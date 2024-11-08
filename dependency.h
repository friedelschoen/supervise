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
void dependency_iterator(void (*callback)(const char*));
void dependency_disable(const char* depend);
void dependency_enable(const char* depend);
int  dependency_start(struct supervisor* service);
