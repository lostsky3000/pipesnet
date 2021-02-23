

local pipes = require('pipes')
local sock = require('pipes.socket')
local log = pipes.log

local selfId, thread = pipes.self()
log('Test2 start, thread=', thread)

local cfg = {   host='www.baidu.com', 
                port=80,
                timeout=10000
            }
log('start connect ', cfg.host,':',cfg.port)

local id, err = sock.open(cfg)   
if not id then
    log('connect failed: ', err)
else
    log('connect succ')
    sock.start(id)
    sock.send(id, 'HTTP heheda\n')
    while(true)
    do
        local msg, sz = sock.read(id)
        if not msg then
            log('disconn: ', sz)
            break
        end
        log(msg)
   end
end

