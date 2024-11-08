#include "utils.h"

#include <ctype.h>
#include <string.h>

char* strip(char* str) {
	char* end;
	while (isspace((unsigned char) *str))
		str++;
	end = strchr(str, '\0') - 1;
	while (end > str && isspace((unsigned char) *end))
		*end-- = '\0';
	return str;
}
