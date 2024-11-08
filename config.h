#pragma once

#define ENABLEDDEPS_ALLOCATE 8 /* Chunk size for expanding enableddeps */

static int sendcommand_retries  = 5; /* try x times before time-out */
static int sendcommand_interval = 1; /* seconds */
