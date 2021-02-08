
#ifndef PIPES_START_H
#define PIPES_START_H

#include "pipes_api.h"


struct pipes_start_config
{
	int harbor;
	int worker_num;
	struct pipes_adapter_config adapter_cfg;
};


int pipes_start(struct pipes_start_config* config);


#endif 



