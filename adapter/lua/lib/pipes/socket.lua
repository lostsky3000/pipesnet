
local util = require('pipes_util')
local push = util.push
local pop = util.pop
local top = util.top
local qsize = util.size
local isEmpty = util.isEmpty

local inner = require('pipes_inner')
local pps = require('pipes')

if not PIPES_C_LIB then
    PIPES_C_LIB = OPEN_PIPES_C_LIB() 
end
local _c = PIPES_C_LIB

if not PIPES_SOCK_LIB then
    PIPES_SOCK_LIB = OPEN_PIPES_SOCK_LIB()
end
local _cs = PIPES_SOCK_LIB

local s = {}

local _netc = inner.netcmd
local _sockPool = {}
local _freeSock
local _readWaits = {}

-- common fn begin
local function freeMsg(msg)
    if msg then
        pps.free(msg)
        --print('freeMsg: ', msg)
    end
end
-- common fn end

function isSockIn(id)
    return _sockPool[id]
end

local IDX_LISTEN = 1
local _idsMgr = {{},{},{}} -- listen
-- ids mgr begin
local function addId(id, idType)
    _idsMgr[idType][id] = true
end
local function delId(id, idType)
    _idsMgr[idType][id] = nil
end
local function hasId(id, idType)
    return _idsMgr[idType][id]
end
-- ids mgr end

function onSockIn(id)
    local sock = _sockPool[id]
    if sock then
        error('sock id duplicated: '..id)
    end
    sock = _cs.newsock(id)
    _sockPool[id] = sock
end

-- listen begin
local _listenCbs = {}
function s.listen(arg)
    local ss = util.genid()
    _cs.sockop(_netc.listen, arg, ss)
    _listenCbs[ss] = {th=pps.curthread()}
    return pps.wait()
end
-- listen end

-- accept begin
local _acceptCbs = {}
function s.accept(id, cb)
    if not hasId(id, IDX_LISTEN) then -- id has not listen
        error('accept, id has not listen yet: '..tostring(id))
    end
    if _acceptCbs[id] then -- has reg accept
        error('accept, cb has reg for id: '..tostring(id)..', '..tostring(_acceptCbs[id]))
    end
    _acceptCbs[id] = cb
end
local function onAccept(idListen, idConn, host, port)
    --onSockIn(idConn)
    local cb = _acceptCbs[idListen]
    --print('pps.sock, onAccept', idListen, idConn, tostring(cb))
    if cb then
        cb(idConn, host, port)
    end
end
-- accept end

-- start begin
function s.start(id)
    if hasId(id, IDX_LISTEN) then
        error('start, invalid id type: listen')
    end
    if isSockIn(id) then
        error('duplicated call start, id='..id)
    end
    local succ = _cs.sockop(_netc.sessionStart, id, pps.self())
    if succ then
        onSockIn(id)
    end
    return succ
end
-- start end

-- close begin
local function onSockClose(id)
    delId(id, IDX_LISTEN)
    -- check readWait
    local wait = _readWaits[id]
    if wait then
        _readWaits[id] = nil
        local sock = _sockPool[id]
        if sock and (wait.tp == 4 or wait.tp == 3) then  -- is readall or readline wait, return all cached data
            pps.wakeup(wait.th, _cs.readsock(sock, 4, true))
        else
            pps.wakeup(wait.th, false, 'conn closed')
        end
    end
    -- try to free cached-msg
    _freeSock(id)
end
function s.close(id)
    onSockClose(id)
    _cs.sockop(_netc.close, id)
end
-- close end

-- send begin
function s.send(id, data)
    if hasId(id, IDX_LISTEN) then
        error('send error, invalid id type: listen')
    end
    if not isSockIn(id) then -- conn off
        return false
    end
    return _cs.sockop(_netc.send, id, data)
end
-- send end

-- open begin
local _openCbs = {}
function s.open(arg)
    local ss = util.genid()
    _cs.sockop(_netc.open, arg, ss)
    _openCbs[ss] = {th=pps.curthread()}
    return pps.wait()
end
-- open end

-- read begin
function _read(id, tp, arg1, arg2)
    local sock = _sockPool[id]
    if not sock then -- conn off
        return false, 'conn off'
    end
    local wait = _readWaits[id]
    if wait then
        error('read duplicate')
    end
    local ret1, ret2 = _cs.readsock(sock, tp, arg1, arg2)
    if ret1 then
        return ret1, ret2
    else -- no data match, wait
        wait = {th=pps.curthread(), tp=tp, arg1=arg1, arg2=arg2}
        _readWaits[id] = wait
        return pps.wait()
    end
end
function s.read(id, sz)
    return _read(id, 1, sz)
end
function s.readint(id, len, flag)
    return _read(id, 2, len, flag)
end
function s.readsep(id, sep)
    if not sep then
        error('readsep error, sep not specify')
    end
    return _read(id, 3, sep)
end
function s.readall(id)
    return _read(id, 4)
end
function s.readline(id)
    return _read(id, 5)
end
-- read end

local function onRecv(id, msgRaw, szRaw)
    --print('onRecv, id='..id..', szRaw='..szRaw)
    local sock = _sockPool[id]
    if not sock then -- conn has closed
        return false
    end
    _cs.pushsockbuf(sock, msgRaw)
    -- check readWait
    local wait = _readWaits[id]
    if wait then  -- has wait
        --print('onRecv, will check readwait')
        local ret1, ret2 = _cs.readsock(sock, wait.tp, wait.arg1, wait.arg2)
        if ret1 then -- has match data
            _readWaits[id] = nil   -- remove wait
            --print('onRecv wakeup read, ret1=',ret1,' ret2=',ret2)
            pps.wakeup(wait.th, ret1, ret2)
        end
    end
    return true
end
-- reg net msg cb
inner.regMsgCb(inner.typename.net, 
function(src, ss, msg, sz)
    -- print('recv net 1: msg=',msg, ' sz=', sz)
    local cmd, id = _cs.nethead(msg, sz)
    -- print('recv net 2: cmd=',cmd,' id=',id, 'msg=',msg, ' sz=', sz)
    local canFree = true
    if cmd == 3 then -- tcp recv
        canFree = not onRecv(id, msg, sz)
    elseif cmd == 2 then -- accept
        onAccept(_cs.netunpack(msg, sz))
    elseif cmd == 5 then -- tcp closed
        onSockClose(id)
    elseif cmd == 6 then  -- connect ret
        ss = -ss
        local cb = _openCbs[ss]
        _openCbs[ss] = nil
        local id, err = _cs.netunpack(msg, sz)
        if id then -- conn succ
            --onSockIn(id)
        end
        pps.wakeup(cb.th, id, err)
    elseif cmd == 1 then  -- listen ret
        ss = -ss
        local cb = _listenCbs[ss]
        _listenCbs[ss] = nil
        local id, err = _cs.netunpack(msg, sz)
        if id then  -- listen succ, mark
            addId(id, IDX_LISTEN)
        end
        pps.wakeup(cb.th, id, err)
    end
    if canFree then
        freeMsg(msg)
    end
    return 1
end)

-- 
_freeSock = function(id)
    ---[[
    if not id then return end
    local sock = _sockPool[id]
    if not sock then return end
    _cs.freesock(sock)
    _sockPool[id] = nil
    --]]
end
local function freeAllSock()
    for id,v in pairs(_sockPool) do
        _freeSock(id)
    end
    _sockPool = {}
end
setmetatable(s, {
    __gc = function(t)
        -- free all cached msg
        --print('socket gc, free all cached msg')
        freeAllSock()
    end
})

function s.mem()
    local mem = 0
    for _, s in pairs(_sockPool) do
        mem = mem + _cs.mem(s)
    end
    return mem
end

return s

