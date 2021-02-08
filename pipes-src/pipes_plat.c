
#include "pipes_plat.h"
#include "pipes_macro.h"

#ifdef SYS_IS_LINUX
#include <unistd.h>
#else

#endif 

int pipes_plat_cpunum()
{
#ifdef SYS_IS_LINUX
	int num = sysconf(_SC_NPROCESSORS_ONLN);
	return num;
#else
	
#endif 

}



