

local pipes = require('pipes')
local log = pipes.log
local sock = require('pipes.socket')

log('HttpTest start')


local idSvr, err = sock.listen({port=12002})
if not idSvr then
    log('listen failed: ', err)
    pipes.exit()
end
sock.accept(idSvr, function(id, host, port)
    sock.start(id)
    while(true)
    do
        local msg, sz = sock.readline(id)
        if not msg then
            break
        end
        log(msg)
    end
end)
