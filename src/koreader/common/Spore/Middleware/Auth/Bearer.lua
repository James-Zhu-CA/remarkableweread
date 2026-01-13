--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore/>
--


local _ENV = nil
local m = {}

--[[
        The OAuth 2.0 Protocol: Bearer Tokens
--]]

function m.call (args, req)
    if req.env.spore.authentication and args.bearer_token then
        req.headers['authorization'] = 'Bearer ' .. args.bearer_token
    end
end

return m
--
-- Copyright (c) 2011-2025 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
