
local pps = {}


local DEF_MSG_HANDLER_NAME = '.PIPES_DEF_MSG_HANDLER'

local LOGGER_NAME = '.lpps_logger'

function pps.defMsgHandlerName()
    return DEF_MSG_HANDLER_NAME
end

function pps.loggerName()
    return LOGGER_NAME
end


return pps


