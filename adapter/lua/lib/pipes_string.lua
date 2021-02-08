

local s = {}

function s.trim(str)
    str = string.gsub(str, '^%s+', '')
    str = string.gsub(str, '%s+$', '')
    return str
end

function s.split(str, sep)
    local ret = {}
    local cnt = 0
    for wd in string.gmatch(str, '%S+') do
        table.insert(ret, wd)
        cnt = cnt + 1
    end
    return ret, cnt
end

function s.lower(str)
    return string.lower(str)
end
function s.upper(str)
    return string.upper(str)
end
function s.len(str)
    return string.len(str)
end

return s
