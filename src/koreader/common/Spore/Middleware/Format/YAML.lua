--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore/>
--

local dump = require'lyaml'.dump
local load = require'lyaml'.load
local pcall = pcall
local type = type
local find = string.find
local raises = require'Spore'.raises

local _ENV = nil
local m = {}

m['content-type'] = 'text/x-yaml'

function m.call (_args, req)
    local spore = req.env.spore
    local payload = spore.payload
    if payload and type(payload) == 'table' then
        spore.payload = dump({ payload })
        req.headers['content-type'] = m['content-type']
    end
    req.headers['accept'] = m['content-type']

    return  function (res)
                local header = res.headers and res.headers['content-type']
                local body = res.body
                if header and find(header, m['content-type'], 1, true) and type(body) == 'string' then
                    local r, msg = pcall(function ()
                        res.body = load(body)
                    end)
                    if not r then
                        if spore.errors then
                            spore.errors:write(msg)
                            spore.errors:write(body, "\n")
                        end
                        if res.status == 200 then
                            raises(res, msg)
                        end
                    end
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
