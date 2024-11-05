#pragma once

#include <time.h>

/* Status names for printing */
extern const char* status_names[];

/* Global variables */
extern int    status;
extern time_t status_change;

/* Utility functions */
void  setstatus(int stat);
char* strip(char* str);
