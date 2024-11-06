#pragma once

#include <limits.h>
#include <sys/types.h>
#include <time.h>

#define POLLINTERVAL 10000 /* poll every 10 seconds */

/* Enum for service status */
enum {
	STATUS_WAITING,
	STATUS_EXITED,
	STATUS_CRASHED,
	STATUS_ERROR,
	STATUS_RUNNING,
};

/* Global variables */
extern time_t      status_change;
extern int         status;
extern pid_t       service;
extern int         restart;
extern const char* servicedir;
extern char        myself[PATH_MAX];

/* Functions */
void controlloop(void);
