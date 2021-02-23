

local args = {...}

local pipes = require('pipes')
local sock = require('pipes.socket')
local log = pipes.log

local mode = args[1]


local cmdStart = 'test start'
local clientNum = 1000

local dict = {}
for i=97, 122 do  -- a~z
    table.insert(dict, string.char(i))
end
for i=48, 57 do  -- 0~9
    table.insert(dict, string.char(i))
end
for i=65, 90 do  -- A~Z
    table.insert(dict, string.char(i))
end
local dictLen = #dict


if not mode then
    local host = '192.168.123.7'
    local port = 11001
    local idSvr
    for i=1, 10 do
        log('listen start at ', host, ':', port)
        idSvr = sock.listen({host=host, port=port})
        if idSvr then
            break
        end
        log('listen failed at ', host, ':', port)
        port = port + 1
    end
    if not idSvr then
        log('listen failed finally, exit')
        pipes.exit()
    end
    log('listen succ at ',host,':',port)
    -- start accept
    sock.accept(idSvr, function(id, host, port)
        --log('LISTEN, id=', id)
        pipes.newservice('Test4', 'sock-server', id)
    end)
    --start client
    for i=1, clientNum do
        pipes.newservice('Test4', 'sock-client', i, host, port)
        pipes.sleep(50)
    end
elseif mode == 'sock-client' then
    local idx = args[2]
    local cfg = {host=args[3], port=math.tointeger(args[4]), timeout=10000}
    --log('CLI: open start, idx=', idx)
    local id, err = sock.open(cfg)
    if not id then
        log('CLI: open failed, idx=', idx,' err: ', err)
        pipes.exit()
    end
    --log('CLI: open succ, id=', id)
    sock.start(id)
    math.randomseed(os.time())
    local sendNum = math.random(10000, 200000)
    local sendNumLen = string.len(tostring(sendNum))
    log('CLI_',id,' sendNum=',sendNum, ', idx=',idx)
    sock.send(id, cmdStart..sendNumLen..sendNum)
    local szRecv = 0
    while(true)
    do
        local msg, sz = sock.read(id)
        if not msg then
            log('CLI: disconn')
            break
        end
        szRecv = szRecv + sz
        --log('CLI: recv, sz=', szRecv, ', id=',id)
    end
    if szRecv ~= sendNum then
        log('CLI_',id,' error, snd&rcv not match, send=',sendNum,', recv=',szRecv)
    end
    pipes.exit()
elseif mode == 'sock-server' then
    local id = args[2]
    sock.start(id)
    --log('SVR: start, id=', id)
    math.randomseed(os.time())
    while(true)
    do
        local msg, sz = sock.read(id, string.len(cmdStart))
        if not msg then
            --log('SVR: disconn, ', sz)
            break
        end
        --log('SVR: recv, ', msg)
        if msg == cmdStart then
            local sendNumLen = sock.read(id, 1)   -- read sendNum
            local sendNum = sock.read(id, math.tointeger(sendNumLen))
            sendNum = math.tointeger(sendNum)
            log('SVR_',id,' sendNum=',sendNum)
            local left = sendNum
            local cnt = 0
            while(left > 0)
            do
                local send = math.random(2000, 10000)
                if send > left then
                    send = left
                end
                local tb = {}
                for i=1, send do
                    table.insert(tb, dict[cnt%dictLen + 1])
                    cnt = cnt + 1
                end
                local str = table.concat(tb)
                sock.send(id, str)
                left = left - send
            end
            if cnt ~= sendNum then
                log('sdfsdfsdfsdf cnt=',cnt,', send=',sendNum)
            end
            log('SVR_',id,' close')
            sock.close(id)
            break
        end
    end
    pipes.exit()
end

