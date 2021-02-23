

local cj = {}

if not PIPES_JSON_LIB then
    PIPES_JSON_LIB = OPEN_PIPES_JSON_LIB()
end
local _cj = PIPES_JSON_LIB


function cj.decode(str)
    return _cj.deseri(str)
end

function cj.encode(tb)
    return _cj.seri(tb)
end

return cj