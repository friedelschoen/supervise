#pragma once

int acquirelock(void);

void releaselock(int fd);

/* tests if lock (flock) is placed.
 * returns 0 if not placed or file does not exist.
 * returns 1 if file is locked
 * returns -1 when error and errno is set. */
int testlock(const char* path);
