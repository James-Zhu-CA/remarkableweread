--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore/>
--

local gettime = require'socket'.gettime  -- See http://lua-users.org/wiki/HiResTimers
local format = string.format

local _ENV = nil
local m = {}

function m.call (_args, _req)
    local start_time = gettime()

    return  function (res)
                local diff = gettime() - start_time
                local str = format('%.4f', diff)
                local header = res.headers['x-spore-runtime']
                if header then
                    res.headers['x-spore-runtime'] = header .. ',' .. str
                else
                    res.headers['x-spore-runtime'] = str
                end
                return res
            end
end

return m
--
-- Copyright (c) 2010-2025 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
