

local args = {...}

local pipes = require('pipes')
local sock = require('pipes.socket')
local log = pipes.log

local mode = args[1]

if mode == 'init' then
    local cfg = args[2]
    if not cfg or not cfg.port then
    log('[CONSOLE] start failed, listen port not specify')
        pipes.exit()
    end
    local cfgSvr = {
            host='0.0.0.0',
            --host='127.0.0.1', 
            port=cfg.port}
    local idSvr, err = sock.listen(cfgSvr)
    if not idSvr then
        log('[CONSOLE] listen at ',cfgSvr.host,':',cfgSvr.port,' failed')
        pipes.exit()
    end
    log('[CONSOLE] listen at ',cfgSvr.host,':',cfgSvr.port,' succ')
    sock.accept(idSvr, function(id, host, port)
        pipes.newservice('lpps_console', 'server', id)
    end)
elseif mode ~= 'server' then
    pipes.exit()
else
    local cs = require('pipes_console')
    local id = args[2]
    sock.start(id)
    sock.send(id, '[CONSOLE] Hi, pipesnet console\n')
    while(true)
    do
        local line, sz = sock.readline(id)
        if not line then -- conn off
            break
        end
        local quit, echo = cs.procCmd(line)
        if echo then
            sock.send(id, '[CONSOLE] '..tostring(echo)..'\n')
        end
        if quit then
            sock.close(id)
            break
        end
    end
    log('[CONSOLE] client off')
    pipes.exit()
end
