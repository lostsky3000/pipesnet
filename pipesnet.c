

#include "pipes_start.h"

#include <stdio.h>

#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "minheap.h"
#include "pipes_time.h"
#include "timing_wheel.h"
#include "pipes_mq.h"
#include "pipes.h"
#include "pipes_handle.h"
#include "pipes_plat.h"

#include "lua_pipes_start.h"
#include "cJSON.h"


void threadTest();
void minheapTest();
void timingWheelTest();
void mqTest();
void handleMgrTest();
void luaSeriTest();
void strTest();
void structTest();
void atomTest();
void alignmentTest();
void jsonTest();
/*
int main(int argc, char *argv[])
{
	//pid_t pid = getpid();
	
	//minheapTest();
	//timingWheelTest();
	//mqTest();
	//handleMgrTest();
	//luaSeriTest();
	//strTest();
	//structTest();
	//atomTest();
	//alignmentTest();
	//jsonTest();
	
	int ret = 0;
	
	ret = luapps_start_main(argc, argv);
	
	//threadTest();
	 
	return ret;
}
*/
// json test
void walkItem(cJSON* obj)
{
	if (cJSON_IsObject(obj))
	{
		cJSON* item = obj->child;
		while (item != NULL)
		{
			walkItem(item);
			item = item->next;
		}
	}
	else
	{
		int n = 1;
	}
}
void jsonTest()
{
	const char* str = "{\"name\":\"dada\", \"age\":25, \"score\":98.5, \"arr\":[]}";
	cJSON* root = cJSON_Parse(str);
	const char * err = cJSON_GetErrorPtr();
	cJSON* jName = cJSON_GetObjectItem(root, "name");
	char * ret = cJSON_SetValuestring(jName, "ddddd");
	cJSON_SetIntValue(jName, 27);
	cJSON* jAge = cJSON_GetObjectItem(root, "age");
	cJSON* jScore = cJSON_GetObjectItem(root, "score");
	cJSON_bool b = cJSON_IsNumber(jAge);
	b = cJSON_IsBool(jAge);
	b = cJSON_IsObject(jName);
	b = cJSON_IsObject(jAge);
	//
	walkItem(root);
	//const char* txt = cJSON_Print(root);
	const char* txt = cJSON_PrintUnformatted(root);
	printf("ret: %s\n", txt);
	cJSON_Delete(root);
	int n = 1;
}

// alignment test
struct st_align3
{
	char arr[4]; //int i1;
}
;
struct st_align2
{
	struct st_align3 st;
};
struct st_align
{
	char ch1;
	struct st_align2 st;
};
void alignmentTest()
{
	size_t sz;
	int tmp;
	sz = sizeof(struct st_align);
	
}

// atom test
#include "atomic.h"
struct atom_test
{
	int flag;
}
;
void atomTest()
{
	struct atom_test test;
	test.flag = 0;
	int ret = ATOM_CAS(&test.flag, 0, 0);
	ret = ATOM_CAS(&test.flag, 0, 100);
	ret = ATOM_CAS(&test.flag, 101, 101);
	ret = ATOM_CAS(&test.flag, 100, 0);
	ret = ATOM_CAS(&test.flag, 0, 0);
	int n = 1;
}

// struct test
struct st_test
{
	unsigned char cmd;
	char flag;
	uint16_t size;
	int id;
	struct st_test* next;
};
void structTest()
{
	size_t sz = 0;
	sz = sizeof(void*);
	sz = sizeof(struct st_test*);
	struct st_test stOri;
	
	sz = sizeof(stOri);
	void* buf = malloc(6);
	
	((unsigned char*)buf)[0] = 1;
	((char*)buf)[1] = 100;
	*(uint16_t*)(buf + 2) = 1680;
	*(int*)(buf + 4) = 9527;
	
	
	struct st_test* stDec = (struct st_test*)buf;
	
	free(buf);
	int n = 1;
}
// str test
void strTest()
{
	const char* str = "hehe123";
	size_t len = strlen(str);
	void* buf = malloc(len + 1);
	//memcpy(buf, str, len);
	strcpy(buf, str);
	const char* str2 = (const char*)buf;
	//
	const char* str3 = "hehe\0 123\0 test";
	
	int n = 1;
}
// lua seri test
#include "lua_seri.h"
void* lseri_realloc(
	void* ptr,
	size_t szOldData,
	size_t szNewPrefer,
	size_t szNewMin,
	size_t* szNewActual,
	void* udata)
{
	*szNewActual = szNewPrefer;
	void* newBuf = pipes_malloc(*szNewActual);
	memcpy(newBuf, ptr, szOldData);
	pipes_free(ptr);
	return newBuf;
}
void lseri_free(void*ptr, void*udata)
{
	pipes_free(ptr);
}
int lsericb(lua_State* L)
{
	size_t szOut = 0;
	size_t bufCap = 128;
	void* buf = pipes_malloc(bufCap);
	// pack
	void* outBuf = lua_seri_pack(L, 1, &szOut, buf, bufCap, NULL, lseri_realloc, lseri_free, NULL);
	
	// unpack
	lua_pop(L, lua_gettop(L));
	int retNum = lua_seri_unpack(L, outBuf, szOut, NULL, NULL);
	
	pipes_free(outBuf);
	return retNum;
}

void luaSeriTest()
{
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	
	/*
	lua_pushnil(L);
	lua_pushstring(L, "heheda");
	lua_pushinteger(L, 321);
	luaL_checkstack(L, LUA_MINSTACK, NULL);
	int sz = lua_gettop(L);
	int i = 0;
	for (i=1; i<=sz; ++i)
	{
		printf("iType: %s\n", lua_typename(L , lua_type(L, i)));
	}
	*/
	
	void* udata = pipes_malloc(128);
	lua_pushlightuserdata(L, udata);
	lua_setglobal(L, "USER_DATA");
	size_t sz = sizeof(*udata);
	
	lua_pushcfunction(L, lsericb);
	lua_setglobal(L, "seriTest");
	
	luaL_loadfile(L, "./service/seriTest.lua");
	int ret = lua_pcall(L, 0, 0, 0);
	if (ret != LUA_OK)
	{
		printf("pcall seriTest.lua error: %d, %s\n", ret, lua_tostring(L, -1));
	}
	lua_close(L);
	
	pipes_free(udata);
}

// handleMgrTest
struct ST_HANDLE_TEST
{
	int thread;
	char* name;
	uint64_t handle;
}
;
uint64_t get_ctx_handle(void* ctx)
{
	uint64_t handle = ((struct ST_HANDLE_TEST*)ctx)->handle;
	return handle;
}
void set_ctx_handle(void* ctx, uint64_t handle)
{
	
}
void dump_handle_test(int harbor, int handleNum, int* arrThread, int opNum, int* arrOp)
{
	printf("harbor = %d, handleNum = %d, opNum = %d\n", harbor, handleNum, opNum);
	int i;
	printf("thread: ");
	for (i=0; i<handleNum; ++i)
	{
		printf("%d, ", arrThread[i]);
	}
	printf("\n");
	//
	printf("op: ");
	for(i=0; i<opNum; ++i)
	{
		printf("%d, ", arrOp[i]);
	}
	printf("\n");
	int n = 1;
}
void handleMgrTest()
{
	
	srand(time(NULL));
	uint64_t totalCost = 0;
	uint64_t totalAdd = 0;
	uint64_t totalDel = 0;
	
	int testTimes = 100;
	//int testTimes = 1;  //debug
	//
	int harbor = rand() % 256;
	struct handle_storage* s = pipes_handle_new_storage(
		harbor, 
		64,
		get_ctx_handle,
		set_ctx_handle,
		pipes_malloc,
		pipes_free, 
		sizeof(struct ST_HANDLE_TEST*));
	//
	int i, j;
	for (i = 0; i < testTimes; ++i)
	{
		/**/
		
		int handleNum = 10000 + rand() % 10000;
		int arrThread[handleNum];
		int arrOp[handleNum * 2];
		
		/*
		int harbor = 182;
		int handleNum = 263;
		int arrThread[] = { }; //debug
		int arrOp[] = {};  //debug
		*/
		//
		int arrCtxAdded[handleNum];
		memset(arrCtxAdded, -1, sizeof(int) * handleNum);
		//printf("harbor = %d, handleNum = %d\n", harbor, handleNum);
		struct ST_HANDLE_TEST* arrHandlePtr[handleNum];
		memset(arrHandlePtr, 0, sizeof(struct ST_HANDLE_TEST*) * handleNum);
		//printf("thread: ");
		//init src data
		for (j = 0; j < handleNum; ++j)
		{
			arrThread[j] = rand() % 256;
			//printf("%d, ", arrThread[j]);
		}
		//printf("\n");
		//init ctx
		for(j=0; j<handleNum; ++j)
		{
			struct ST_HANDLE_TEST* ctx = pipes_malloc(sizeof(struct ST_HANDLE_TEST));
			arrHandlePtr[j] = ctx;
			ctx->thread = arrThread[j];
			//
			size_t nameLen = 16;
			ctx->name = pipes_malloc(nameLen);
			memset(ctx->name, 97, nameLen);
			sprintf(ctx->name, "%d", j);
		}
		//
		int aliveNum = 0;
		int addCnt = 0;
		int delCnt = 0;
		int opCnt = 0;
		//
		struct timeval tmValBegin;
		pipes_time_now(&tmValBegin);
		//
		while(delCnt < handleNum)
		{
			int isAdd = 1;
			if (addCnt < handleNum && delCnt < handleNum && aliveNum > 0)  // can add or del
				{
					isAdd = rand() % 2;
				}
			else if (delCnt < handleNum && aliveNum > 0)  // only can del
				{
					isAdd = 0;
				}
			else
			{
				int n = 1;
			}
			//
			//isAdd = arrOp[opCnt];   //debug
			//
			arrOp[opCnt] = isAdd;
			++opCnt;
			//printf("%d,\n", isAdd);
			//
			if(isAdd)
			{
				++totalAdd;
				struct ST_HANDLE_TEST* ctx = arrHandlePtr[addCnt];
				ctx->handle = pipes_handle_register_unsafe(ctx, ctx->thread, s);
				arrCtxAdded[addCnt] = addCnt;
				++aliveNum;
				++addCnt;
				// 
				const char* pName = pipes_handle_namehandle_unsafe(ctx->handle, ctx->name, s);  //add name check
				if (pName == NULL)
				{
					int n = 1;
				}
				// find test
				uint64_t tmpHandle = pipes_handle_findname_unsafe(ctx->name, s);  // find by name check
				if (tmpHandle != ctx->handle)
				{
					int n = 1;
				}
				if (pipes_handle_get_harbor(ctx->handle) != harbor)
				{
					int n = 1;
				}
				int tmpThread = pipes_handle_get_thread(ctx->handle);
				if (tmpThread != ctx->thread)
				{
					int n = 1;
				}
				/*
				struct ST_HANDLE_TEST* ctxFind = pipes_handle_grab_unsafe(ctx->handle, s);
				if (ctxFind == NULL)
				{
					dump_handle_test(harbor, handleNum, arrThread, opCnt, arrOp);
				}
				*/
			}else
			{
				++totalDel;
				// find one to del
				for(j=0; j<addCnt; ++j)
				{
					if (arrCtxAdded[j] >= 0)  //
					{
						break;
					}
				}
				if (j >= addCnt)
				{
					dump_handle_test(harbor, handleNum, arrThread, opCnt, arrOp);
				}
				struct ST_HANDLE_TEST* ctxFind = arrHandlePtr[arrCtxAdded[j]];
				arrCtxAdded[j] = -1;
				--aliveNum;
				++delCnt;
				//
				uint64_t tmpHandle = pipes_handle_findname_unsafe(ctxFind->name, s);  // find by name check
				if (tmpHandle != ctxFind->handle)
				{
					int n = 1;
				}
				if (pipes_handle_retire_unsafe(ctxFind->handle, s) == 0)
				{
					dump_handle_test(harbor, handleNum, arrThread, opCnt, arrOp);
				}
				// find test
				ctxFind = pipes_handle_grab_unsafe(ctxFind->handle, s);
				if (ctxFind != NULL)
				{
					dump_handle_test(harbor, handleNum, arrThread, opCnt, arrOp);
				}
			}
		}
		//
		struct timeval tmValEnd;
		pipes_time_now(&tmValEnd);
		uint64_t tmCost = tmValEnd.tv_sec * 1000 * 1000 + tmValEnd.tv_usec - tmValBegin.tv_sec * 1000 * 1000 - tmValBegin.tv_usec;
		totalCost += tmCost;
		//
		for (j=0; j<handleNum; ++j)
		{
			struct ST_HANDLE_TEST* ctx = arrHandlePtr[j];
			if (ctx->name)
			{
				pipes_free(ctx->name);
				ctx->name = NULL;
			}
			pipes_free(ctx);
		}
	}
	//
	printf("avg = %lu us/op, totalAdd = %lu, totalDel = %lu, totalCost = %lu\n",
		totalCost/(totalAdd+totalDel),
		totalAdd, totalDel, totalCost);
    int n = 1;
}

// mq test
void mqTest()
{
	srand(time(NULL));
	
	int i, j;
	int testTimes = 100000;
	for (i = 0; i < testTimes; ++i)
	{
		struct message_queue* q = pipes_mq_create_cap(100);
		/**/
		int numPush = 5 + rand() % 1000;
		int numInsert = 5 + rand() % 1000;
		int arrPush[numPush];
		int arrInsert[numInsert];
		int arrOP[numPush + numInsert];
		// init push arr
		for(j = 0 ; j < numPush ; ++j)
		{
			arrPush[j] = rand() % 100000;
		}
		// init insert arr
		for(j = 0 ; j < numInsert ; ++j)
		{
			arrInsert[j] = rand() % 100000;
		}
		
		/*
		int numPush = 22;
		int numInsert = 32;
		int arrPush[] = { 70014, 39400, 36820, 62604, 78976, 55105, 77166, 270, 1481, 11621, 89175, 
23873, 10001, 66502, 83826, 30162, 31716, 84476, 95186, 15278, 42716, 30108 };
		int arrInsert[] = {9732, 69344, 60882, 73664, 45275, 43847, 55733, 24605, 77416, 42100, 64005, 
14236, 4704, 59334, 85693, 81870, 59604, 87175, 9843, 65131, 27400, 19845, 47985, 27578, 
66359, 79702, 12054, 61546, 11332, 71122, 8006, 21064};
		int arrIsPush[] = {1};
		int arrOP[numPush + numInsert];
		*/
		
		// rand push & insert
		struct pipes_message msg;
		int addCnt = 0;
		int pushCnt = 0;
		int insertCnt = numInsert - 1;
		while (pushCnt < numPush || insertCnt > -1)
		{	
			int isPush = 1;
			if (pushCnt < numPush && insertCnt > -1)
			{
				if (rand() % 2 == 0)   //push
					{
						isPush = 1;
					}
				else    // insert
					{
						isPush = 0;
					}
			}
			else if (pushCnt < numPush)  // can only push
				{
					isPush = 1;
				}
			else  // can only insert
			{
				isPush = 0;
			}
			
			//isPush = arrIsPush[addCnt]; //debug
			if(addCnt == 0)
			{
				int n = pipes_mq_size_unsafe(q);
				int m = 1;
			}
			
			if (isPush)
			{
				msg.size = arrPush[pushCnt++];
				pipes_mq_push_unsafe(q, &msg);
			}
			else
			{
				msg.size = arrInsert[insertCnt--];
				pipes_mq_inserthead_unsafe(q, &msg);
			}
			arrOP[addCnt++] = isPush;
			int szQueue = pipes_mq_size_unsafe(q);
			if (szQueue != addCnt)  // error
			{
				pipes_mq_size_unsafe(q);
				printf("queueNum=%d, numPush=%d, numInsert=%d, addCnt=%d\n", szQueue, numPush, numInsert, addCnt);
				printf("OP: ");
				for (j=0; j<addCnt; ++j)
				{
					printf("%d, ", arrOP[j]);
				}
				printf("\n");
				printf("InsertSrc: ");
				for (j = 0; j < numInsert; ++j)
				{
					printf("%d, ", arrInsert[j]);
				}
				printf("\n");
				printf("PushSrc: ");
				for (j = 0; j < numPush; ++j)
				{
					printf("%d, ", arrPush[j]);
				}
				printf("\n");
				pipes_mq_size_unsafe(q);
			}
		}
		// check
		for(j=0; j<numInsert; ++j)
		{
			arrOP[j] = arrInsert[j];
		}
		for (j=numInsert; j<numInsert + numPush; ++j)
		{
			arrOP[j] = arrPush[j - numInsert];
		}
		int cnt = 0;
		while ( pipes_mq_pop_unsafe(q, &msg) > 0 )
		{
			if (arrOP[cnt++] != msg.size)
			{
				int n = 1;
			}
		}
		if (cnt != numInsert + numPush)
		{
			int n = 1;
		}
	}
	
	int n = 1;
	
}

// timing-wheel test
uint64_t tmw_now = 1; //1577811661000;
int tmw_totaltask = 10000000;
int tmw_add_cnt = 0;
int tmw_cb_cnt = 0;
void s_tmwheel_callback(void* udata, uint64_t tmNow)
{
	++tmw_cb_cnt;	
	uint64_t expReq = *(uint64_t*)udata;
	int64_t off = expReq - tmw_now;
	//printf("timeout off=%ld\n", off);
	if(off >= 30 || off <= -30)
	{
		printf("timeout slow, off=%ld, req=%lu, now=%lu", off, expReq, tmw_now);
	}
	free(udata);
}
void timingWheelTest()
{
	srand(time(NULL));
	struct tw_timer* timer = tmwheel_create_timer(
		20,
		50,
		tmw_now, 
		s_tmwheel_callback, 
		malloc, 
		free, 
		100, 
		300);
	//
	//uint64_t arrExp[] = { 1033512, 1003848, 1010375, 1057720, 1019443, 1007658, 1013386, 1013168, 1014594, 1020241};
	uint64_t arrExp[2][98] = { 
		{
			97, 1047915, 1028872, 1026450, 1030178, 1058859, 1023134, 1053445, 1032071, 1009567, 1006444, 1059258, 1051174, 
				1029410, 1020827, 1054698, 1016586, 1017628, 1027268, 1014723, 1022424, 1013500, 1009195, 1044959, 1018659, 
				1020917, 1034074, 1038929, 1000121, 1042545, 1046110, 1003969, 1030410, 1051284, 1006721, 1036890, 1050093, 
				1029806, 1006637, 1058467, 1015675, 1013031, 1057675, 1006800, 1042392, 1018452, 1037800, 1058928, 1036030, 
				1041371, 1049953, 1058404, 1031173, 1035450, 1043313, 1049783, 1056317, 1053689, 1005014, 1032741, 1036184, 
				1051075, 1036660, 1042897, 1018661, 1019684, 1056089, 1008705, 1049440, 1002677, 1007122, 1005065, 1015658, 
				1041099, 1048167, 1034352, 1059501, 1025918, 1009583, 1011833, 1007239, 1059486, 1046540, 1014714, 1034887, 
				1006155, 1004447, 1007506, 1059795, 1045764, 1016549, 1012281, 1013141, 1029512, 1031480, 1031752, 1049146, 1027520
		},
        {3,  1014918, 1030177, 1000213}
		};
	size_t sz = sizeof(arrExp);
	sz = sizeof(arrExp[0]);
	int addRndTimes = sizeof(arrExp) / sizeof(arrExp[0]);
	int addRndCnt = 0;
	int i;
	while(tmw_add_cnt < tmw_totaltask || tmw_cb_cnt != tmw_add_cnt)
	{
		tmw_now += 30;  // time increase
		//
		if(tmw_add_cnt < tmw_totaltask)  // can add more task
		{
			int off = tmw_totaltask - tmw_add_cnt;
			
			int addNum = off;
			if (off > 5000)
			{
				addNum = 1000 + rand() % (5000 - 1000);
			}
			else if (off > 200)
			{
				addNum = 100 + rand() % (off - 100);
			}
			//int addNum = arrExp[addRndCnt++][0]; 

			printf("add start, num=%d: ", addNum);
			for (i=0; i<addNum; ++i)
			{
				uint64_t* pExpire = malloc(sizeof(uint64_t));
				uint64_t tmp1 = rand() % 1000;
				uint64_t tmp2 = 30; //rand() % 3600;
				uint64_t tmp3 = 1;  //rand() % (24 * 7);
				uint64_t delay = 20 + tmp1 * tmp2 * tmp3;
				if (tmw_add_cnt == 0)  // 1st task, set to max delay
				{
					delay = 20 + 1000 * 3600 * 24;
				}
				if (delay > 1580403661000)  //debug
				{
					int n = 1;
				}
				*pExpire = tmw_now + delay; 
				//*pExpire = arrExp[addRndCnt - 1][i + 1];
				
				//printf("%lu, ", *pExpire);
				++tmw_add_cnt;
				tmwheel_add_task(*pExpire, pExpire, timer, tmw_now);
			}
			printf("\n");
		}
		
		struct timeval tmval;
		pipes_time_now(&tmval);
		uint64_t beginMs = pipes_time_toms(&tmval);
		uint64_t preTaskNum = tmwheel_cur_total_task(timer);
		// advance timer
		uint64_t delay = tmwheel_advance_clock(tmw_now, timer);
		if (delay > 0)
		{
			tmw_now += (delay - 30 < 0 ? 0 : delay - 30);
		}
		uint64_t postTaskNum = tmwheel_cur_total_task(timer);
		int64_t dltTask = preTaskNum - postTaskNum;
		if (dltTask > 100)   // has task expired, calc cost
		{
			pipes_time_now(&tmval);
			uint64_t endMs = pipes_time_toms(&tmval);
			if (endMs - beginMs > 5)
			{
				printf("cost calc, taskNum=%ld, cost=%lu\n", dltTask, endMs - beginMs);
			}
		}
		
	}
	int n = 1;
}

// minheap test
int s_minheap_compare(void* data1, void*data2)
{
	int num1 = *(int*)data1;
	int num2 = *(int*)data2;
	return num1 - num2;
}
void minheapTest()
{
	srand(time(NULL));
	struct minheap_queue* queue = minheap_create_queue(400, s_minheap_compare, malloc, free);
	//
	int i, j;
	int testTimes = 100000;
	//
	//
	struct timeval tm;
	pipes_time_now(&tm);
	uint64_t tmBegin = pipes_time_toms(&tm);
	for (i = 0; i < testTimes; ++i)
	{
		int valMax = 10000 + rand()%100000000;
		int num = 1 + rand() % 2000;
		int arrNum[num];
		int arrRet[num];
		//gen src
		for (j=0; j<num; ++j)
		{
			arrNum[j] = rand() % valMax;
		}
		// add
		for (j = 0; j < num; ++j)
		{
			minheap_add_node(&arrNum[j], queue);
		}
		// pop, check 
		void* ptr = minheap_pop_min(queue);
		int curMin = *(int*)ptr;
		arrRet[0] = curMin;
		int tmpCnt = 0;
		int err = 0;
		while ( (ptr = minheap_pop_min(queue)) != NULL)
		{
			int tmp = *(int*)ptr;
			arrRet[++tmpCnt] = tmp;
			if (tmp < curMin)   //exception
			{
				err = 1;
			}
		}
		if (err)   // error, dump
		{
			printf("Error, idx=%d ======\n", i);
			printf("src: ");
			for (j = 0; j < num; ++j)
			{
				printf("%d, ", arrNum[j]);
			}
			printf("\n");
			printf("ret: ");
			for (j = 0; j < num; ++j)
			{
				printf("%d, ", arrRet[j]);
			}
			printf("\n");
		}
	}
	pipes_time_now(&tm);
	uint64_t tmEnd = pipes_time_toms(&tm);
	uint64_t tmCost = tmEnd - tmBegin;
	printf("tmCost: %lu ms\n", tmCost);
	int n = 1;
}


// thread test
pthread_mutex_t g_mutex;
pthread_cond_t g_cond;
static int g_num = 0;
static void* thread_func_nosync(void* arg)
{
	int idx = *(int*)arg;
	printf("thread start, idx=%d\n", idx);
	int i;
	if (idx == 0)
	{	
		
		pthread_mutex_lock(&g_mutex);
		for (i=0; i<10; ++i)
		{
			pthread_cond_signal(&g_cond);
		}
		pthread_mutex_unlock(&g_mutex);
	}
	else
	{	
		sleep(3);
		pthread_mutex_lock(&g_mutex);
		pthread_cond_wait(&g_cond, &g_mutex);
		printf("cond wait done\n");
		pthread_mutex_unlock(&g_mutex);
	}
	
	return NULL;
}
void threadTest()
{
	pthread_mutex_init(&g_mutex, NULL);
	pthread_cond_init(&g_cond, NULL);
	
	const int thNum = 2;
	pthread_t fds[thNum];
	int arrArg[thNum];
	int i;
	for (i=0; i<thNum; ++i)
	{
		arrArg[i] = i;
		pthread_create(&fds[i], NULL, thread_func_nosync, &arrArg[i]);
	}
	for (i=0; i<thNum; ++i)
	{
		pthread_join(fds[i], NULL);
	}
	printf("all thread done\n");
	printf("g_num = %d\n", g_num);
}