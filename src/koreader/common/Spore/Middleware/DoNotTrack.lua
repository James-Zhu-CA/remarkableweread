--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore/>
--


local _ENV = nil
local m = {}

function m.call (_args, req)
    req.headers['x-do-not-track'] = '1'
end

return m
--
-- Copyright (c) 2011-2025 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
