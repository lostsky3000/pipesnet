

-- srcPath, paramType, paramData, paramSize

local args = table.pack(...)
--[[
for i=1, args.n do
    print('svsloader arg: ', i, args[i])
end
--]]

SERVICE_PATH = args[1]
-- print("SERVICE_PATH: ", SERVICE_PATH)
-- print("LUA_PATH: ", LUA_PATH)
-- print("LUA_CPATH: ", LUA_CPATH)
-- print("LUA_SERVICE: ", LUA_SERVICE)

local srcPath = LUA_SERVICE..';'..LUA_PATH

local main, pattern
local err = {}
for pat in string.gmatch(srcPath, "([^;]+);*") do
	local filename = string.gsub(pat, "?", SERVICE_PATH)
	local f, msg = loadfile(filename)
	if not f then
		table.insert(err, msg)
	else
		pattern = pat
		main = f
		break
	end
end

-- 
if LUA_PATH then
    package.path = LUA_PATH
end
if LUA_SERVICE then
    package.path = package.path .. ';' .. LUA_SERVICE
end
if LUA_CPATH then
    package.cpath = LUA_CPATH
end
--print("package.path: ", package.path)
--print("package.cpath: ", package.cpath)
LUA_PATH = nil
LUA_CPATH = nil
LUA_SERVICE = nil

local pipes = require('pipes')

if not main then
    pipes.error(table.concat(err, "\n"))
	--error(table.concat(err, "\n"))
end

local inner = require('pipes_inner')

if args[3] then -- has init param
    if inner.istype(args[2], 'string') then
        local str = inner.rawtostr(args[3], args[4])
        pipes.exec(main, str)
    else
        pipes.exec(main, select(1, pipes.unpack(args[3], args[4])))
    end
else  -- no init param
    pipes.exec(main)
end










