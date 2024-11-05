#pragma once

#include <unistd.h>

void handlecommand(int command);
void handlechild(pid_t pid, int stat);
