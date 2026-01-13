--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore/>
--

local pairs = pairs
local find = string.find
local match = string.match
local sub = string.sub
local tconcat = table.concat

local _ENV = nil
local m = {}

m.store = {}

local function set (header)
    local name, value = match(header, '^([^=]+)=([^;]*)')
    if name and value then
        if value == '' then
            m.store[name] = nil
        else
            m.store[name] = value
        end
    end
end

function m.call (_args, req)
    local t = {}
    for k, v in pairs(m.store) do
        t[#t+1] = k .. '=' .. v
    end
    if #t > 0 then
        req.headers['cookie'] = tconcat(t, '; ')
    end

    return  function (res)
                local header = res.headers and res.headers['set-cookie']
                if header then
                    local pos = 1
                    while true do
                        local start, end_ = find(header, ', ', pos, true)
                        if not start then
                            set(sub(header, pos))
                            break
                        end
                        set(sub(header, pos, start - 1))
                        pos = end_ + 1
                    end
                end
                return res
            end
end

return m
--
-- Copyright (c) 2024-2025 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
