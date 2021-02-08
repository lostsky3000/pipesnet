

local pipes = require('pipes')

local log = pipes.log

log('default msg handler start, thread: ', info.thread)


--[[
local function safeFreePtr(ptr)
    pipes.free(ptr)
end
local curSrc
local function unpackPtrCb(ptr, idx)
    log('free unhandle ptr, src=', curSrc, ', ptr=', ptr)
    local ok, err = pcall(safeFreePtr, ptr)
    if not ok then
        log('free unhandle ptr err, src=', curSrc, ', ptr=', ptr, ', err:', err)
    end
end
local typeLua = pipes.msgTypeLua()
local function safeMsgCb(source, session, mType, mData, mSize)
    if mData then
        curSrc = source
        log('free unhandle buf, src=', curSrc, ', buf=', mData, ', size=', mSize, ', type=', mType)
        if mType == typeLua then -- check ptr in lua
            pipes.unpack(mData, mSize, unpackPtrCb)
        else 
            --log('free unhandle buf, src=', curSrc, ', buf=', mData, ', size=', mSize, ', type=', mType)
        end
        pipes.free(mData)
    end
end
pipes.dispatch(function(source, session, mType, mData, mSize)
    local ok, err = pcall(safeMsgCb, source, session, mType, mData, mSize)
    if not ok then
        log('free unhandle buf error: ', err)
    end
    return 1
end)
--]]


