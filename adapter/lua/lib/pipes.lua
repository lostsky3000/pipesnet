
local util = require('pipes_util')
local push = util.push
local pop = util.pop
local isEmpty = util.isEmpty

--
local inner = require('pipes_inner')
local typeLua = inner.typecode('lua')
--
local pps = {}
if not PIPES_C_LIB then
    PIPES_C_LIB = OPEN_PIPES_C_LIB() 
end
local _c = PIPES_C_LIB
local _co = coroutine


local _tasks = {}
local _freeThs = {}
local _coThs = {}

local _exit = false

local function newThread()
    local th = {}
    local co = _co.create(function()
        local q
        while(true)
        do
            if _exit then
                break
            end
            local t
            if q then
                t = pop(q)
            end
            if not t then -- no task in seq
                t = pop(_tasks)
                if t then
                    q = t.q
                    if q then
                        t = pop(q)
                    end
                end
            end
            --
            if t then  -- exec task
                --print('will pcall', pps.curthread().co, t.f, q)
                local ok, ret = pcall(t.f, select(1, table.unpack(t.arg)))
                if not ok then
                    pps.error(ret)
                end
            else -- no more task, yield & add to free-thread-queue
                if q then
                    q.done = true 
                    q = nil
                end
                push(_freeThs, th)
                pps.wait()
            end
        end
    end)
    th.co = co
    _coThs[co] = th
    return th
end
local function notifyThread()
    local th = pop(_freeThs)
    if not th then -- no free thread, create
        th = newThread()
    end
    pps.wakeup(th)
    return th
end
local function wrapTask(f, ...)
    local t = {f=f, arg=table.pack(...)}
    return t
end
local function addTask(t)
    push(_tasks, t)
end
local function execTask(f, ...)
    local t = wrapTask(f, ...)
    addTask(t)
    return notifyThread()
end

local function send(cmd, ...)
    return _c.send(cmd, ...)
end
local function typeCode(typeOri)
    local tp = type(typeOri)
    if tp == 'string' then
        local ret = inner.typecode(typeOri)
        if not ret then error('unknown protocol: '..tostring(typeOri)) end
        return ret
    end
    return typeOri
end
--
local _msgPacks = {}
local function regProtocol(mType, fPack, fUnpack)
    mType = typeCode(mType)
    _msgPacks[mType] = {pk = fPack, unpk = fUnpack}
end
local function doUnpack(mType, msg, sz)
    local pack = _msgPacks[mType]
    if not pack then 
        error('unreg protocol for unpack: '..tostring(mType)) 
    end
    if not pack.unpk then
        error('unpack not reg for protocol: '..tostring(mType))
    end
    return pack.unpk(msg, sz)
end
local function doPack(mType, ...)
    local pack = _msgPacks[mType]
    if not pack then 
        error('unreg protocol for pack: '..tostring(mType)) 
    end
    if not pack.pk then
        error('pack not reg for protocol: '..tostring(mType))
    end
    return pack.pk(...)
end
--
function pps.reg_protocol(mType, fPack, fUnpack)
    return regProtocol(mType, fPack, fUnpack)
end
function pps.unpack(data, sz)
    return _c.luaunpack(data, sz)
end
function pps.pack(...)
    return _c.luapack(...)
end
function pps.error(err)
    _c.error(err)
end
function pps.exec(f, ...)
    return execTask(f, ...)
end
function pps.wait()
    return _co.yield()
end
function pps.wakeup(th, ...)
    --print('pps.wakeup, ', th.co, ', stat:',_co.status(th.co))
    return _co.resume(th.co, ...)
end
function pps.curthread()
    local co = _co.running()
    return _coThs[co]
end
function pps.yield()
    pps.sleep(0)
end
function pps.fork(f, ...)
    local t = wrapTask(f, ...)
    pps.timeout(0, function()
        addTask(t)
        notifyThread()
    end)
end

local _cmd = inner.cmd
local _rspCbs = {}
local function _newservice(src, th, ...)
    --srcPath, toThread, session, paramType(opt), param(opt), paramSize(opt)
    local ss = util.genid()
    local handle = send(_cmd.newservice, src, th, ss, typeLua, doPack(typeLua, ...))
    if not handle then
        error('newservice failed 1')
    end
    _rspCbs[ss] = {th=pps.curthread()}
    local ret = pps.wait()
    if not ret then 
        error('newservice failed 2')
    end
    return handle
end
function pps.newservice(src, ...)
    return _newservice(src, -1, ...)
end
function pps.newidxservice(src, th, ...)
    return _newservice(src, th, ...)
end

function pps.timeout(delay, cb)
    local ss = util.genid()
    send(_cmd.timeout, delay, ss)  -- delay, session
    _rspCbs[ss] = {cb=cb}
end
function pps.sleep(delay)
    local ss = util.genid()
    send(_cmd.timeout, delay, ss)  -- delay, session
    _rspCbs[ss] = {th=pps.curthread()}
    return pps.wait()
end

--
local _tickSs
local function doTick(delay, ss, cnt, cb)
    if delay < 1 then
        error('invalid tick delay: '..tostring(delay))
    end
    _tickSs = ss
    send(_cmd.timeout, delay, ss, cnt)  -- delay, session
    _rspCbs[ss] = {cb=cb}
end
local _tickCb = nil
local _tickDelay
local fnTickCb
fnTickCb = function(src, ss, cnt)
    --print('tickCb: ', cnt, ss, _tickSs, _tickCb)
    if ss ~= _tickSs or not _tickCb then
        return
    end
    if cnt == 1 then  -- re-reg
        local ss = util.genid()
        doTick(_tickDelay, ss, 500, fnTickCb)
    end
    _tickCb()
end
function pps.tick(delay, cb)
    if _tickCb then
        error('reg tick duplicately')
    end
    _tickDelay = delay
    _tickCb = cb
    local ss = util.genid()
    doTick(delay, ss, 500, fnTickCb)
end
function pps.untick()
    if _tickCb then
        _tickCb = nil
        _rspCbs[_tickSs] = nil
    end
end

function pps.now()
    return _c.time()
end
function pps.free(ptr)
    return _c.free(ptr)
end
function pps.send(to, mType, ...)
    mType = typeCode(mType)
    local msg, sz = doPack(mType, ...)
    send(_cmd.send, to, 0, mType, msg, sz)  -- to, session, mType, msg, sz
end
function pps.call(to, mType, ...)
    mType = typeCode(mType)
    local msg, sz = doPack(mType, ...)
    local ss = util.genid()
    send(_cmd.send, to, ss, mType, msg, sz)
    _rspCbs[ss] = {th=pps.curthread()}
    return pps.wait()
end

local _retsWait = {}
function pps.ret(mType, ...)
    local th = pps.curthread()
    local tb = _retsWait[th]
    if not tb then
        error('ret not expect')
    end
    _retsWait[th] = nil
    mType = typeCode(mType)
    local msg, sz = doPack(mType, ...)
    send(_cmd.send, tb.src, -tb.ss, mType, msg, sz)
end
function pps.name(name, addr)
    return send(_cmd.name, name, addr)
end
function pps.exit()
    if _exit then return end
    _exit = true
    _c.exit()
    pps.wait()
end
function pps.shutdown()
    _c.shutdown()
    pps.exit()
end

local logger = inner.logger()
function pps.log(...)
    local msg, sz = doPack(typeLua, ...)
    send(_cmd.send, logger, 0, typeLua, msg, sz)  -- to, session, mType, msg, sz
end

-- stat begin
local _stat = {id=1, mem=2, mqlen=3, message=4, memth=5, svcnum=6}
local function statCode(sType)
    if type(sType) == 'string' then
        local s = _stat[sType]
        if not s then
            error('unknown stat type: '..sType)
        end
        return s
    end
    error('invalid state type: '..tostring(sType))
end
local function isThValid(th)
    th = tonumber(th)
    local ths = tonumber(pps.env('thread'))
    if th >= 0 and th < ths then
        return true
    end
end
function pps.stat(sType, a1)
    local code = statCode(sType)
    if sType == 'memth' and a1 then  -- specify thread
        a1 = tonumber(a1)
        local _, thSelf = pps.self()
        if a1 ~= thSelf then  -- query other thread mem
            if not isThValid(a1) then return false, 'thread invalid: '..a1 end
            local ok, mem = pps.call(inner.sysname(a1), 'lua', 'memth')
            if not ok then return false, mem end
            return mem
        end
    end
    if sType == 'mem' then
        if a1 then  -- specify service id
            a1 = tonumber(a1)
            local ok, mem = pps.call(a1, 'stat', 'mem')
            if not ok then return false, mem end
            return mem
        end
        local mem = _c.stat(code)
        local sock = require('pipes.socket')
        return mem + sock.mem()
    end
    return _c.stat(code, a1)
end
function pps.self()
    return _c.stat(_stat.id)
end
local function onStatQuery(src, ss, ...)
    local tb = {...}
    local cmd = tb[1]
    if cmd == 'mem' then  -- query self mem
        local mem, err = pps.stat('mem')
        pps.ret('stat', mem)
    end
end
-- stat end

--
function pps.env(key)
    return _c.env(key)
end
function pps.localid(id)
    return _c.localid(id)
end
function pps.local2Id(localId)
    return _c.local2id(localId)
end

-- dispatch
local types = inner.typename
local _msgCbs = {}
local function _dispatch(code, cb)
    _msgCbs[code] = cb
end
function pps.dispatch(mType, cb)
    local code = typeCode(mType)
    if code == types.net or code == types.stat then
        error('dispatch type reserved: '..tostring(mType))
    end
    _dispatch(code, cb)
end
_dispatch(types.stat, onStatQuery)  -- reg stat-query cb
local function freeMsg(mType, msg)
    --[[
    if msg and (mType == types.lua or mType == types.string) then
        pps.free(msg)
    end
    --]]
    if msg then
        pps.free(msg)
    end
end
local function procMsg(src, ss, mType, msg, sz)
    if _exit then 
        freeMsg(mType, msg)
        return 1
    end
    --print('procMsg ss='..ss..', type='..mType..', thread='..tostring(_co.running()), sz)
    if mType == types.net then -- handler has reg by pipes.socket
        local cbMsg = inner.msgCb(mType)
        if cbMsg then
            cbMsg(src, ss, msg, sz)
        else  -- no msg handle
            freeMsg(mType, msg)
        end
        return 1
    end
    -- source, session, type, msg, size
    if ss < 0 then -- is response
        ss = -ss
        local cb = _rspCbs[ss]
        if cb then
            if cb.th then   -- is call response
               --print('recv call rsp, ', ss, cb.th.co, msg)
               _rspCbs[ss] = nil
               if mType == types.reterr then
                   pps.wakeup(cb.th, false, doUnpack(mType, msg, sz))
               else
                   pps.wakeup(cb.th, true, doUnpack(mType, msg, sz))
               end
            elseif cb.cb then -- is ret 
               --print('recv ret cb: ', ss, mType, msg, sz)
               if mType ~= types.timer or sz <= 1 then
                   _rspCbs[ss] = nil
               end
               cb.cb(src, ss, doUnpack(mType, msg, sz))
            end
        end
        freeMsg(mType, msg)
        return 1
    end
    local cbMsg = _msgCbs[mType]
    if cbMsg then
        local th
        if ss > 0 then -- need ret
            th = pps.curthread()
            _retsWait[th] = {ss=ss, src=src}
        end
        cbMsg(src, ss, doUnpack(mType, msg, sz))
        if ss > 0 then -- check ret
            if _retsWait[th] then -- has not ret
                _retsWait[th] = nil
                send(_cmd.send, src, -ss, types.reterr, 'no ret', 0)
            end
        end
    end
    --
    freeMsg(mType, msg)
    return 1
end

-- reg msg cb
_c.dispatch(function(src, ss, mType, msg, sz)
    --print('msg cb, ', ss, mType, msg, sz)
    pps.exec(procMsg, src, ss, mType, msg, sz)
    return 1
end)

function pps.queue()
    local seq = {}
    local function fn(f, ...)
        local t = {f=f, arg=table.pack(...)}
        if not seq.q then
            seq.q = {done = true}
        end
        push(seq.q, t)
        if seq.q.done then  -- all task done, re-add to task-queue
            seq.q.done = nil
            push(_tasks, seq)
        end
        --print('seq add f', pps.curthread().co, f, seq.q.n)
    end
    return fn
end

function pps.console(cfg)
    return pps.newservice('lpps_console', 'init', cfg)
end

-- reg default protocol
regProtocol(types.raw, nil, function(msg, sz)
    return msg, sz
end)
regProtocol(types.string, 
function(...)
    local tb = table.pack(...)
    if tb.n == 1 then
        return tostring(tb[1])
    end
    if tb.n > 1 then
        local str = table.concat(tb)
        return str
    end
end,
inner.rawtostr)
regProtocol(types.newsvrret, nil, function(msg, sz)
    return sz
end)
regProtocol(types.timer, nil, function(msg, sz)
    return sz
end)
regProtocol(types.reterr, 
function(err)
    return tostring(err), 0
end,
function(msg, sz)
    return inner.rawtostr(msg, sz)
end)
regProtocol(types.lua, pps.pack, pps.unpack)
regProtocol(types.stat, pps.pack, pps.unpack)

return pps
