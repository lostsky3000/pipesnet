

local pipes = require('pipes')

local log = pipes.log

log('boot start')


pipes.console({port=11112})


--local json = require('pipes.json')
--local str = '{"name":"dada", "age":25, "score":98.5, "arr":[123, "hehe123", 456, {"sub":"ddd"}, [55,66,77,88]], "arr2":[], "obj2":{} }'
--local jObj = json.decode(str)
--for k,v in pairs(jObj) do
--    print('item, '..tostring(k)..'='..tostring(v))
--end
--str = json.encode(jObj)
--print(str)

--
pipes.newservice('HttpTest')

pipes.exit()


--[[
local sock = require('pipes.socket')
log('start open')
local id, err = sock.open({
                host='www.baidu.com', 
                port=443,
                timeout=15000, 

                })   
if id then
    log('boot conn succ, id='..id)
    sock.start(id)
    sock.send(id, '123\n')
    while(true)
    do
        local rsp, sz = sock.read(id)
        if not rsp then
            break
        end
        log(rsp)
    end
    log('boot disconn')
else
    log('boot conn failed, err='..err)
end
--]]

--[[
for i=1, 10 do
    pipes.sleep(1000)
    log('sleep done ', i)
end
--]]


--[[
        --pipes.sleep(10000)
        log('start net session: '..id)
        sock.start(id)
        local str, sz = sock.read(id, 20)
        log('read start msg 1: '..tostring(str)..', sz='..sz)
        str, sz = sock.read(id, 30)
        log('read start msg 2: '..tostring(str)..', sz='..sz)
        pipes.timeout(10000, function()
            pipes.exit()
        end)
        while(true)
        do
            local str, sz = sock.read(id, 1024)
            if not str then
                break
            end
            log('dddd recv: sz='..sz..', msg: '..tostring(str))
        end
        --pipes.sleep(45000)
        --log('asdasdasd')
        --sock.start(id)
--]]

