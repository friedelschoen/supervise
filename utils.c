#include "utils.h"

#include "supervise.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

const char* status_names[] = { "waiting", "terminated", "crashed", "error", "running" };

void setstatus(int stat) {
	FILE* fp;

	if (stat == status)
		return;

	status        = stat;
	status_change = time(NULL);

	fp = fopen("supervise/status", "w+");
	if (fp) {
		fwrite(&status, sizeof(status), 1, fp);
		fwrite(&status_change, sizeof(status_change), 1, fp);
		fclose(fp);
	} else {
		perror("Error opening status file");
	}
}

char* strip(char* str) {
	char* end;
	while (isspace((unsigned char) *str))
		str++;
	end = strchr(str, '\0') - 1;
	while (end > str && isspace((unsigned char) *end))
		*end-- = '\0';
	return str;
}
