--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore/>
--

local pairs = pairs

local _ENV = nil
local m = {}

m.early_validate = false

function m.call (args, req)
    local params = req.env.spore.params
    for k, v in pairs(args) do
        params[k] = v
    end
end

return m
--
-- Copyright (c) 2010-2025 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
