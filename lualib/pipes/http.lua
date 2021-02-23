

local h = {}
local s = require('pipes.socket')

function h.newserver(cfg, cb)
    local ret, err
    while(true)
    do
        local idSvr, e = s.listen({port=cfg.port})
        if not idSvr then -- listen failed
            err = e
            break
        end
        s.accept(idSvr, function(id, host, port)
            local obj = {id=id}
            setmetatable(obj, {__index = h})
            cb(obj, host, port)
        end)
        ret = {id = idSvr}
        setmetatable(ret, {__index = h})
        break
    end
    return ret, err
end

local function trim(str)
    str = string.gsub(str, '^%s+', '')
    str = string.gsub(str, '%s+$', '')
    return str
end
local function parseKV(str, sep)
    local pos = string.find(str, sep)
    if not pos then return nil end
    local key = string.sub(str, 1, pos - 1)
    local val = string.sub(str, pos + 1)
    key = trim(key)
    val = trim(val)
    if key == '' then
        key = nil
    end
    if val == '' then
        val = nil
    end
    return key, val
end
local function parseReq(id)
    local msg, sz
    local str = require('pipes_string')
    local req
    while(true)
    do
        -- read req-line:  GET /url HTTP/1.0
        msg = s.readline(id)
        local arr = str.split(msg)
        local method = arr[1]
        local url = arr[2]
        local ver = arr[3]
        if method ~= 'GET' and method ~= 'POST' then
            break
        end
        if ver ~= 'HTTP/1.1' then
            break
        end
        -- read headers
        local headers = {}
        local e
        while(true)
        do
            msg = s.readline(id)
            if msg == '' then break end  -- headers done
            local k, v = parseKV(msg, ':')
            if not k or k == '' then  -- invalid header
                e = 'invalid header: '..msg
                break
            end
            headers[k] = v
        end
        if e then break end  -- parse headers failed
        req = {method=method, url=url, ver=ver, headers=headers}
        break
    end
    return req
end

function h:readreq()
    local id = self.id
    if not id then
        error('invalid http session')
    end
    if self._start then
        return false, 'readreq has called'
    end
    s.start(id)
    self._start = true
    local ok, req = pcall(parseReq, id)
    if not ok then
        return false, req
    end
    if req.method == 'POST' then  -- read req-body

    end
    return req
end

function h:resp(rsp)
    local id = self.id
    if not id then
        error('invalid http session')
    end
end

function h:close()
    if self.id then
        s.close(self.id)
        self.id = nil
    end
end


return h



