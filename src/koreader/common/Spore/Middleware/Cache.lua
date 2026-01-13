--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore/>
--

local collectgarbage = collectgarbage
local setmetatable = setmetatable


local _ENV = nil
local m = {}

local cache = setmetatable({}, {__mode = 'v'})

function m.reset ()
    collectgarbage 'collect'
end

function m.call (_args, req)
    req:finalize()
    local key = req.url
    local res = cache[key]
    if res then
        return res
    else
        return  function (res_)
                    cache[key] = res_
                    return res_
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
