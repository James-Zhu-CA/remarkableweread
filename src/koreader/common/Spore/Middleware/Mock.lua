--
-- lua-Spore : https://fperrad.frama.io/lua-Spore/>
--

local type = type
local match = string.match

local _ENV = nil
local m = {}

function m.call (args, req)
    req:finalize()
    for i = 1, #args, 2 do
        local r
        local cond, func = args[i], args[i+1]
        if type(cond) == 'string' then
            r = match(req.url, cond)
        else
            r = cond(req)
        end
        if r then
            return func(req)
        end
    end
end

return m
--
-- Copyright (c) 2010-2025 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
