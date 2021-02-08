#ifndef PIPES_TIME_H
#define PIPES_TIME_H

#include "pipes_macro.h"

#include <stdint.h>

#ifdef SYS_IS_LINUX
#include <stdlib.h>
#include <sys/time.h>
#else
struct timeval;
#endif





void pipes_time_now(struct timeval*);

uint64_t pipes_time_toms(struct timeval*);


#endif // !PIPES_TIME_H

