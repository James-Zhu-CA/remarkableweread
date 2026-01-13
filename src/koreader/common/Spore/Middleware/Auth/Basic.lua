--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore/>
--

local base64 = require'mime'.b64

local _ENV = nil
local m = {}

function m.call (args, req)
    if req.env.spore.authentication and args.username and args.password then
        req.headers['authorization'] =
            'Basic ' .. base64(args.username .. ':' .. args.password)
    end
end

return m
--
-- Copyright (c) 2010-2025 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
