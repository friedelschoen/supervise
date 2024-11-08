#pragma once

#include <unistd.h>

void handler_command(int command);
void handler_child(pid_t pid, int stat);
