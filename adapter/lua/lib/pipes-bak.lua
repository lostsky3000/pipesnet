
local pps = {}

local M_TYPE_RAW = 0
local M_TYPE_STR = 1
local M_TYPE_EXIT = 11
local M_TYPE_FINAL = 12
local M_TYPE_TIMEOUT = 20
local M_TYPE_NET = 22
local M_TYPE_RET_NOTEXIST = 30
local M_TYPE_RET_DEFAULT = 31
local M_TYPE_LUA = 64

local NETCMD = {TCP_SVR=1}


function pps.msgTypeLua()
    return M_TYPE_LUA
end
function pps.msgTypeStr()
    return M_TYPE_STR
end
function pps.msgTypeExit()
    return M_TYPE_EXIT
end

-- override sys method
local ppsco = coroutine
coroutine = nil


local SID_MIN = 1000000000
local SID_MAX = 2100000000
--
local sid = SID_MIN
local function newSid()
    sid = sid + 1
    if sid > SID_MAX then
        sid = SID_MIN
    end
    return sid
end

--
local c = OPEN_PIPES_C_LIB()

--
local unfreePtrs = {}
local function markPtr(ptr)
    if ptr then unfreePtrs[ptr] = 1 end
end
local function unmarkPtr(ptr)
    if ptr then unfreePtrs[ptr] = nil end
end
local function freeAllPtr()
    for k,v in pairs(unfreePtrs) do
        pps.free(k)
        print('freeAllPtr: ', k, SERVICE_PATH)
    end
end


local packPtrs = nil
function LPPS_PACK_PTR_ITER(ptr, idx)
    if not packPtrs then
        packPtrs = {}
    end
    packPtrs[ptr] = 1
end
local function packlua(...)
    packPtrs = nil  -- clear cur pack ptrs
    local data, sz = c.luapack(...)
    markPtr(data)
    return data, sz
end
local function unmarkPackPtrs()
    if packPtrs then -- unmarkPackPtrs
        for k,v in pairs(packPtrs) do
            unmarkPtr(k)
        end
        packPtrs = nil
    end
end

--
local unpackPtrCb
function LPPS_UNPACK_PTR_ITER(ptr, idx)
    if unpackPtrCb then
        unpackPtrCb(ptr, idx, c)
    end
end
function pps.unpack(data, sz, ptrCb)
    unpackPtrCb = ptrCb
    return c.luaunpack(data, sz, ptrCb)
end
--

local hasExit = false
--
function pps.newservice(toTh, src, name, ...)
    local data, sz = packlua(...)
    -- toThread, srcPath, name, paramType, param(void*), paramSize
    local id = c.newservice(toTh, src, name, M_TYPE_LUA, data, sz)
    if id then -- newservice succ, unmark ptr
        unmarkPtr(data)
        unmarkPackPtrs()
    else -- newservice failed, free ptr
        pps.free(data)
    end
    return id
end
local function sendraw(to, mType, mData, mSize)
    -- to(handle or name), session, type, void*, size, priority
    unmarkPtr(mData)
    unmarkPackPtrs()
    return c.send(to, 0, mType, mData, mSize)
end
function pps.send(to, ...)
    local data, sz = packlua(...)
    sendraw(to, M_TYPE_LUA, data, sz)
end                                                                                                                         

--
local callCbs = {}
local function callraw(to, mType, mData, mSize)
    local s = newSid()
    if callCbs[s] then error('duplicate session: '..s) end
    local co = ppsco.running()
    unmarkPtr(mData)
    unmarkPackPtrs()
    c.send(to, s, mType, mData, mSize)
    callCbs[s] = {th = co}
    return ppsco.yield()
end
function pps.call(to, ...)
    local data, sz = packlua(...)
    return callraw(to, M_TYPE_LUA, data, sz)
end
--
local mapCoRet = {}
local function retraw(mType, mData, mSize)
    local co = ppsco.running()
    local ret = mapCoRet[co]
    assert(ret, 'retraw error')
    if ret then -- has ret-task
        mapCoRet[co] = nil -- remove ret-task
        unmarkPtr(mData)
        c.send(ret.src, -ret.ss, mType, mData, mSize)
    end
end
function pps.ret(...)
    local co = ppsco.running()
    local ret = mapCoRet[co]
    assert(ret, 'ret error')
    if ret then -- has ret-task
        mapCoRet[co] = nil -- remove ret-task
        local data, sz = packlua(...)
        unmarkPtr(data)
        unmarkPackPtrs()
        c.send(ret.src, -ret.ss, M_TYPE_LUA, data, sz)
    end
end

--
local tmoutCbs = {}
function pps.timeout(delay, cb)
    if type(cb) == 'function' then -- is function
        local s = newSid()
        if tmoutCbs[s] then error('timeout sid error: '..s) end
        tmoutCbs[s] = {cb = cb}
        return c.timeout(delay, s)
    end
    error('invalid timeout cb: '..type(cb))
end
function pps.sleep(delay)
    local s = newSid()
    if tmoutCbs[s] then error('sleep sid error: '..s) end
    local co = ppsco.running()
    tmoutCbs[s] = {th = co}
    c.timeout(delay, s)
    return ppsco.yield()
end


function pps.tick(dur, cb)
    assert(dur and dur > 0)
    assert(cb and type(cb)=='function')
    local s = newSid()
    local function tcb(cnt)
        if cnt <= 1 then
            c.timeout(dur, s, 100)
        end
        cb()
    end
    tmoutCbs[s] = {tick = tcb}
    c.timeout(dur, s, 200)
    return s
end
function pps.untick(id)
    tmoutCbs[id] = nil
end

--
local cbTaskHead = nil
local cbTaskTail = nil

local cbThHead = nil
local cbThTail = nil
local cbThNum = 0

local function newCbThread()
    local th = {}
    local function thLoop()
        local co = th.co
        while(true)
        do
            if cbTaskHead then
                local t = cbTaskHead
                cbTaskHead = cbTaskHead.nxt
                if not cbTaskHead then -- cbtask queue is empty
                    cbTaskTail = nil
                end
                -- cb
                local needRet
                if t.ss and t.ss > 0 then -- has valid session, need ret
                    mapCoRet[co] = {src=t.src, ss=t.ss}
                    needRet = true
                end
                local cbRet = t.cb(t.src, t.ss, t.tp, t.data, t.sz)  -- cb, source, session, mType, mData, mSize
                if needRet then -- need ret
                    local retInfo = mapCoRet[co]
                    if retInfo then  -- has not ret, ret default
                        retraw(M_TYPE_RET_DEFAULT, nil, 0)
                    end
                end
                if not cbRet and t.data then  -- msg not consumed, free udata
                    pps.free(t.data)
                end
            else  -- no more cb-task, add to free-thread queue
                if cbThTail then
                    cbThTail.nxt = th
                else
                    cbThHead = th
                end
                cbThTail = th
                ppsco.yield()  -- wait for new cb-task
            end
        end
    end
    th.co = ppsco.create(function()
        local ok, err = pcall(thLoop)
        if not ok then
            pps.error(err)
        end
    end)
    cbThNum = cbThNum + 1
    --print('cbThNum: ', cbThNum)
    return th
end

local function procCb(cb, source, session, mType, mData, mSize)
    -- add task
    local t = { cb=cb, src=source, ss=session, data=mData, tp=mType, sz=mSize}
    if cbTaskTail then  -- cbtask queue is not empty
        cbTaskTail.nxt = t
    else
        cbTaskHead = t 
    end
    cbTaskTail = t
    --
    local th
    if cbThHead then  -- th-pool has free thread, pop to use
        th = cbThHead
        cbThHead = cbThHead.nxt
        if not cbThHead then  -- th-pool empty
            cbThTail = nil
        end
    else -- no free thread, new
        th = newCbThread()
    end
    ppsco.resume(th.co)
end

local userMsgCb
c.dispatch(function(source, session, mType, mData, mSize)
    markPtr(mData)
    if mType == M_TYPE_FINAL then  -- service finalize, free all unmark ptrs
        freeAllPtr()
        return 1
    end
    if mType == M_TYPE_TIMEOUT then  -- timeout
        local tmcb = tmoutCbs[session]
        --print('on msg timeout, ss=', session, ', tmcb=', tmcb)
        --asdfasdf.asdf()  -- crash trigger
        if tmcb then
            if tmcb.th then  -- thread yield
                tmoutCbs[session] = nil
                ppsco.resume(tmcb.th, true)
            elseif tmcb.cb then -- no thread yield, use thread-pool
                tmoutCbs[session] = nil
                procCb(tmcb.cb)
            elseif tmcb.tick then
                --print('tick cb,', mSize)
                procCb(tmcb.tick, mSize)
            else -- error
                tmoutCbs[session] = nil
            end
        end
        return 1
    end
    if session < 0 and mType ~= M_TYPE_NET then  -- is call-ret
        local s = -session
        local callCb = callCbs[s]
        callCbs[s] = nil
        -- print('recv ret: ', callCb.th, ppsco.status(callCb.th), session, mType, mData, mSize)  -- debug
        if callCb then
            if mType == M_TYPE_LUA or mType == M_TYPE_RAW then  --  data
                --ppsco.resume(callCb.th, true, pps.unpacklua(mData, mSize))
                --pps.free(mData)
                ppsco.resume(callCb.th, true, mType, mData, mSize)
            elseif mType == M_TYPE_RET_NOTEXIST then
                ppsco.resume(callCb.th, false, "dest service doesn't exists")
            elseif mType == M_TYPE_RET_DEFAULT then
                ppsco.resume(callCb.th, false, 'dest service not ret')
            end
            return 1
        end
    end
    if not userMsgCb then --no userMsgCb
        if mType == M_TYPE_EXIT then
            freeAllPtr()
            return 1
        end
        -- something wrong
        return 1
    end
    procCb(userMsgCb, source, session, mType, mData, mSize)
    return 1
end)

function pps.dispatch(cb)
   assert(not userMsgCb, 'duplicate reg dispatch')
   userMsgCb = cb
end

--
function pps.now()
    return c.time()
end
function pps.free(ptr, ref)
    if not ptr then return nil end
    if not unfreePtrs[ptr] then  -- uncollected ptr, warn
        pps.log('free uncollected ptr: ', ptr, pps.info().id)
    end
    unmarkPtr(ptr)
    -- print('pps.free: ', ptr)
    local ref = c.free(ptr, ref)
    if ref >= 0 then
        return ref
    end
    error('free error, ref: '..ref) 
end
function pps.newbuf(sz, ref)
    assert(sz and sz > 0)
    local ptr = c.malloc(sz, ref)
    markPtr(ptr)
    return ptr
end

function pps.rawtostr(udata, sz)
    return c.rawtostr(udata, sz)
end

--
local ctx = LPPS_CONTEXT_INFO
function pps.info()
    return {id=ctx.id, totalThread=ctx.totalThread, thread=ctx.thread}
end
--

function pps.coroutine()
    return ppsco
end

local internal = require('pipes_internal')
local loggerName = internal.loggerName()
function pps.log(...)
    local tb = table.pack(...)
    for i=1, tb.n do
        tb[i] = tostring(tb[i])
    end
    local str = table.concat(tb)
    -- to(handle or name), session, type, void*, size
    c.send(loggerName, 0, M_TYPE_STR, str)
    --
    -- local info = pps.info()
    -- print(info.id, ": ", str)
end
function pps.exit()
    if hasExit then return nil end
    hasExit = true
    return c.exit()
end
function pps.mem()
    return c.mem()
end
function pps.error(err)
    pps.log(err)
    c.error(err)
end

--
function pps.tcpserver(host, port, backlog)
    local sid = newSid()
    -- host, port, backlog, sid
    c.tcpsvr(host, port, backlog, sid)
end

return pps

