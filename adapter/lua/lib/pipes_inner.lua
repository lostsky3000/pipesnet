
local inr = {}

if not PIPES_C_LIB then
    PIPES_C_LIB = OPEN_PIPES_C_LIB() 
end
local _c = PIPES_C_LIB


--
local typeName = {
raw = 0,
string = 1,
newsvrret = 11, 
timer = 20,
net = 22,
reterr = 30,
stat = 40,
lua = 64
}
local typeCode
local function genTypeCode()
    typeCode = {}
    for k,v in pairs(typeName) do
        typeCode[v] = k
    end
end
--
function inr.istype(code, name)
    if not typeCode then
        genTypeCode()
    end
    if typeCode[code] and typeCode[code] == name then
        return true
    end
    return false
end
function inr.typecode(name)
    return typeName[name]
end
function inr.rawtostr(data, sz)
    return _c.rawtostr(data, sz)
end

local logger = LOGGER_NAME
function inr.logger()
    return logger
end

local defHandler = DEF_MSG_HANDLER
function inr.defhandler()
    return defHandler
end

local cmd = {
    newservice = 1, timeout = 2,
    send = 3, name = 4
}
local netcmd = {
    listen = 1, sessionStart = 2,
    close = 3, send = 4, open = 5
}

inr.cmd = cmd
inr.typename = typeName
inr.netcmd = netcmd

--
local _innerMsgCb = {}
function inr.regMsgCb(mType, cb)
    _innerMsgCb[mType] = cb
end
function inr.msgCb(mType)
    return _innerMsgCb[mType]
end

function inr.sysname(th)
    return 'LPPS_WORKER_THREAD_'..th
end

return inr