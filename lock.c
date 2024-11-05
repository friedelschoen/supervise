#include "lock.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

int acquirelock(void) {
	int fd = open("supervise/lock", O_CREAT | O_RDWR, 0666);
	if (fd == -1) {
		fprintf(stderr, "error: unable to open supervise/lock: %s\n", strerror(errno));
		return -1;
	}

	// Try to apply an exclusive lock
	if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
		if (errno == EWOULDBLOCK) {
			printf("Service is already supervised, exiting.\n");
			close(fd);
			return -1;    // Another supervisor is already running
		} else {
			perror("Error acquiring lock");
			close(fd);
			return -1;
		}
	}

	// Lock acquired, proceed with supervision
	return fd;    // Keep the lock file descriptor open to hold the lock
}

void releaselock(int fd) {
	if (fd != -1) {
		flock(fd, LOCK_UN);    // Release the lock
		close(fd);             // Close the file descriptor
	}
}

/* tests if lock (flock) is placed.
 * returns 0 if not placed or file does not exist.
 * returns 1 if file is locked
 * returns -1 when error and errno is set. */
int testlock(const char* path) {
	int fd;

	if ((fd = open(path, O_RDWR)) == -1) {
		/* file does not exist, so it can't be locked.  */
		return errno == ENOENT ? 0 : -1;
	}

	if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
		/* flock failed, but why?
		 * errno == EWOULDBLOCK, this lock would block aka. it is already locked by other process.
		 * if otherwise, something weird happened. */
		return errno == EWOULDBLOCK ? 1 : -1;
	}

	/* oh, we locked the file... we must unlock it. */
	flock(fd, LOCK_UN);
	close(fd);

	return 0;
}
