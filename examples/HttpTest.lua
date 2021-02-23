

local pipes = require('pipes')
local log = pipes.log
local http = require('pipes.http')

log('HttpTest start')

local svr, err = http.newserver({port=11001}, function(ss, host, port)
    log('http newin: ', ss.id)
    local req, err = ss:readreq()
    if req then
        log('method=', req.method, ', url=', req.url, ', ver=', req.ver)
        for k, v in pairs(req.headers) do
            log('header: ', k, '=', v)
        end
    else 
        log('readreq err: ', err)
    end
    ss:close()
end)

if svr then
    log('http server succ')
else
    log('http server failed')
end

