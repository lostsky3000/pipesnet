

local args = table.pack(...)

--[[
for i=1, args.n do
    print('launch arg: ', i, args[i])
end
--]]

local pipes = require('pipes')
local inner = require('pipes_inner')


local threads = pipes.env('thread')

-- start logger service
---[[
local logger = pipes.newidxservice('lpps_logger', threads - 1)
local loggerName = inner.logger()
pipes.name(loggerName, logger)

-- start thread sys service
for i=1, threads do
    local id = pipes.newidxservice('lpps_worker_thread', i - 1)
    pipes.name(inner.sysname(i - 1), id)
end

--[[
-- start default msg handler
local defHandler = inner.defhandler()
local totalThread = pipes.stat('threads')
for i=1, totalThread do
    local th = i - 1
    local idDefMsg = pipes.newservice('lpps_def_msg_handler', th, defHandler..'_'..th)
end
--]]

---[[
-- start boot service
local boot = args[1]
pipes.newservice(boot)
-- exit
pipes.exit()

--]]