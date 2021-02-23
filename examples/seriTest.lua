

print("seri test")

math.randomseed(os.time())
--math.randomseed(10086)

local tbChar = {
        "0","1","2","3","4","5","6","7","8","9",
        "a","b","c","d","e","f","g","h","i","j","k","l","m","n","o","p","q","r","s","t","u","v","w","x","y","z",
        "A","B","C","D","E","F","G","H","I","J","K","L","M","N","O","P","Q","R","S","T","U","V","W","X","Y","Z",
    }

local function getRandomStr(n)     
    local s = ""
    for i =1, n do
        s = s .. tbChar[math.random(#tbChar)]        
    end
    return s
end

local function randKey()
    local t = math.random(1, 4)
    --
    if t == 1 then  -- integer
        --return math.random(1, 500)
        return math.random(0, math.maxinteger) - math.random(0, math.maxinteger)
    elseif t == 2 then  -- number
        local num1 = math.random(0, math.maxinteger) - math.random(0, math.maxinteger)
        local num2 = num1 * 0.97
        return num2
    elseif t == 3 then  -- string
        local sz = math.random(1, 400)
        local str = getRandomStr(sz)
        return str, sz
    elseif t == 4 then  -- user-data
        return USER_DATA
    end
end

local function randValue(noTable)
    local t = 0
    if noTable then
        t = math.random(0, 4)
    else
        t = math.random(0, 5)
    end
    --
    if t == 0 then  -- bool
        if math.random(0, 1) == 0 then
            return true
        else
            return false
        end
    elseif t == 1 then  -- integer
        return math.random(0, math.maxinteger) - math.random(0, math.maxinteger)
    elseif t == 2 then  -- number
        return (math.random(0, math.maxinteger) - math.random(0, math.maxinteger)) * 0.97
    elseif t == 3 then  -- string
        local sz = math.random(1, 600)
        local str = getRandomStr(sz)
        return str, sz
    elseif t == 4 then    -- userdata
        return USER_DATA
    elseif t == 5 then  -- table
        local num = math.random(0, 30)
        return {}, num
    end
end

local genItem
genItem = function(tbWrap, cur, total, depth, onlyArr)
    -- print('genItem', cur, total, depth)
    if cur > total then 
        print('table is full: ', cur, total)
        return 
    end
    local noTable = false
    if depth >= 7 then
        noTable = true
    end
    local pair = true
    if math.random(0, 1) == 0 then
        pair = false
    end
    if onlyArr then
        pair = false
    end
    -- gen val
    local val, valArg = randValue(noTable)
    if type(val) == 'table' then
        for i=1, valArg do
            genItem(val, i, valArg, depth + 1)
        end
    end
    --
    if pair then -- item is key-value
        local key, keyArg = randKey(noTable)
        if type(key) == 'table' then
            for i=1, keyArg do
                genItem(key, i, keyArg, depth + 1)
            end
        end
        tbWrap[key] = val
    else  -- item is arr-item
        table.insert(tbWrap, val)
    end
end


local fnCompare
fnCompare = function(p1, p2)
    local t1 = type(p1)
    local t2 = type(p2)
    if t1 ~= t2 then
        error('type not match: '.. t1..', '..t2)
    end
    if t1 ~= 'table' and p1 ~= p2 then
        error('value not eq: '..t1..', '..p1..', '..p2)
    end
    if t1 == 'table' then
        for k,v in pairs(p1) do
            if p2[k] == nil then
                error('table key not match: '..tostring(k)..', '..type(k))
            end
            fnCompare(v, p2[k])
        end
    end
end


local tryTimes = 100
for i=1, tryTimes do
    local paramNum = math.random(1, 50)
    --print('paramNum: ', paramNum)
    -- gen params
    --print('genItems begin')
    local tbOri = {}
    for i=1, paramNum do
        genItem(tbOri, i, paramNum, 1, true)
    end
    --print('genItems done')
    -- pack & unpack
    --for i,v in pairs(tbOri) do
      --  print('itemType: ', i, type(v))
    --end
    --

    local longStr = getRandomStr(70000)
    table.insert(tbOri, {str=longStr})
    --
    local tbRet = table.pack(seriTest(table.unpack(tbOri)))
    -- check
    fnCompare(tbOri, tbRet)

    print('task done-'..i..', paramNum='..paramNum)
end



