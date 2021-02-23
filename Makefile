
BIN := pipesnet

LIB_DIR = 3rd-src
LUA_DIR := $(LIB_DIR)/lua
LUA_INC := $(LUA_DIR)
LUA_A := $(LUA_DIR)/liblua.a

PIPES_DIR = pipes-src
LUA_LIB_DIR = lualib-src

CFLAGS += -I$(LUA_INC) -I$(PIPES_DIR) -I$(LIB_DIR)/cjson -I$(LUA_LIB_DIR)
LDFLAGS += -L./$(LUA_DIR)
LIBS += -llua -lm -lpthread -ldl

PIPES_OBJS := main.o minheap.o timing_wheel.o \
            pipes_handle.o pipes_malloc.o pipes_mq.o pipes_plat.o \
            pipes_runtime.o pipes_server.o pipes_socket.o \
            pipes_start.o pipes_tcp.o pipes_thread.o \
            pipes_time.o
PIPES_OBJS := $(foreach obj, $(PIPES_OBJS), $(PIPES_DIR)/$(obj))

LUA_LIB_OBJS := lua_pipes_api.o lua_pipes_core.o lua_pipes_env.o \
            lua_pipes_json.o lua_pipes_malloc.o lua_pipes_socket.o \
            lua_pipes_start.o lua_seri.o
LUA_LIB_OBJS := $(foreach obj, $(LUA_LIB_OBJS), $(LUA_LIB_DIR)/$(obj))

LIB_OBJS := $(LIB_DIR)/cjson/cJSON.o

$(BIN): $(PIPES_OBJS) $(LUA_LIB_OBJS) $(LIB_OBJS) $(LUA_A)
	@$(CC) -o $(BIN) $(PIPES_OBJS) $(LUA_LIB_OBJS) $(LIB_OBJS) $(LDFLAGS) $(LIBS)

$(LUA_A):
	cd $(LUA_DIR) && $(MAKE) linux

.PHONY: clean
clean:
	cd $(LUA_DIR) && $(MAKE) clean
	@rm -f $(BIN) $(PIPES_OBJS) $(LUA_LIB_OBJS) $(LIB_OBJS)

dump:
	echo $(PIPES_OBJS)


