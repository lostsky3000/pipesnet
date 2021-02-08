

local pipes = require('pipes')
local sock = require('pipes.socket')
local log = pipes.log

local cfg = {port=16666,
                --decoder={type='fieldlength',  lengthbytes=2 } 
            }
log('start listen, port='..cfg.port)
local idSvr, err = sock.listen(cfg)
log('listen ret: id=', idSvr, ', err=', err)
if idSvr then
    sock.accept(idSvr, function(id, host, port)
        log('onAccept: id=',id,', remote=',host,':',port)
        
        sock.start(id)
        while(true)
        do
            local msg,sz = sock.read(id)
            if not msg then
                log('conn closed, id=',id,' err: ', sz)
                break
            end
            log('recv: ', msg,' sz=', sz)
            if msg == 'close' then
                sock.close(id)
            end
            --sock.send(id, 'echo from server: '..msg)
            for i=1, 10 do
                local tbTest = {}
                for j=1, 5000 do
                    table.insert(tbTest, 'a')
                end
                local str = table.concat(tbTest)
                sock.send(id, str)
                if i == 5 then
                    pipes.sleep(2000)
                end
            end
            
        end
    end)
end



