
#ifndef PIPES_ADAPTER_H
#define PIPES_ADAPTER_H

#include "lua_pipes_malloc.h"
#include "lua_pipes_core.h"

#define ADAPTER_MALLOC(sz) luapps_malloc(sz)

#define ADAPTER_FREE(p) luapps_free(p, 1)

#define ADAPTER_COPY_STR(ori, pSz) luapps_copy_string(ori, pSz)


#endif // !PIPES_ADAPTER_H


