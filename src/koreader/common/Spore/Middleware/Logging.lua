--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore/>
--


local _ENV = nil
local m = {}

function m.call (args, req)
    req.env.sporex.logger = args.logger
end

return m
--
-- Copyright (c) 2010-2025 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
