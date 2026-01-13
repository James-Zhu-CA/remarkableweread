--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore/>
--


local _ENV = nil
local m = {}

function m.call (args, req)
    if args.useragent then
        req.headers['user-agent'] = args.useragent
    end
end

return m
--
-- Copyright (c) 2010-2025 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
