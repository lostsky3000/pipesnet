
local args = {...}

local pipes = require('pipes')
local log = pipes.log

local sock = require('pipes.socket')

if args[1] == 'client' then
    local cfg = args[2]
    log('client start, port = ', cfg.port)
    local id, err = sock.open({host='127.0.0.1', 
                                port=cfg.port,
                                timeout=10000
                                })
    if not id then
        log('open failed: ', err)
    else
        log('open succ, start: id = ', id)
        sock.start(id)
        local sendCnt = 0
        while(true)
        do
            sendCnt = sendCnt + 1
            sock.send(id, 'client msg '..sendCnt)
            local msg, sz = sock.read(id)
            if not msg then 
                log('conn down, id = ', id)
                break
            end
            log('client recv: ', msg)
            pipes.sleep(1000)
        end
    end
elseif args[1] == 'server-logic' then
    local id = args[2]
    log('start session, id = ',id)
    sock.start(id)
    while(true)
    do
        ---[[
        msg, sz = sock.read(id)
        --msg, sz = sock.readline(id, 'a')
        if not msg then
            log('conn down, '..tostring(sz)..', id='..tostring(id))
            break
        end
        --]]
        --[[
        local msg, sz = sock.readall(id)
        log('readall return, msg='..tostring(msg)..', sz='..tostring(sz))
        break
        --]]
        log('recv: '..msg..', strLen='..string.len(msg)..', sz='..tostring(sz))    
        sock.send(id, 'echo from svr: '..tostring(msg)) 
        if msg == 'close' then
            sock.close(id)
        end   
    end 
    log('conn closed, id = ', id)
else  -- start listen
    local cfg = {port=16666,
                --decoder={type='fieldlength',  lengthbytes=2 } 
            }
    log('start listen, port='..cfg.port)
    local idSvr, err = sock.listen(cfg)
    log('listen ret: id=', idSvr, ', err=', err)
    if idSvr then
        sock.accept(idSvr, function(id, host, port)
            log('onAccept: ', id, host, port)
            -- newservice for this sock
            pipes.newservice('Test', 0, 'server-logic', id)
        end)
        -- start sim-client
        for i=1, 1 do
            pipes.newservice('Test', 1, 'client', cfg)
        end
    end
end



