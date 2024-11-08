#include "buffer.h"

#include <errno.h>
#include <stdlib.h>

char* malloc_load_buffer(FILE* fp, size_t* pbuflen) {
	size_t buflen;
	char*  buffer;
	fseek(fp, 0, SEEK_END);
	buflen = ftell(fp);
	rewind(fp);

	if (!(buffer = malloc(buflen + 1))) /* +1 for null terminator */
		return NULL;

	if (fread(buffer, 1, buflen, fp) != buflen) {
		free(buffer);
		return NULL;
	}

	for (size_t i = 0; i < buflen; i++) {
		if (!buffer[i]) {
			errno = EINVAL;
			free(buffer);
			return NULL;
		}
	}

	buffer[buflen] = '\0'; /* Null-terminate the buffer */
	if (pbuflen)
		*pbuflen = buflen;
	return buffer;
}
