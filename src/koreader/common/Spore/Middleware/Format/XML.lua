--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore/>
--

local type = type
local find = string.find
local raises = require'Spore'.raises
local dump = require'Spore.XML'.dump
local parse = require'Spore.XML'.parse

local _ENV = nil
local m = {}

m['content-type'] = 'text/xml'

function m.call (args, req)
    local spore = req.env.spore
    local payload = spore.payload
    if payload and type(payload) == 'table' then
        spore.payload = dump(payload, args)
        req.headers['content-type'] = m['content-type']
    end
    req.headers['accept'] = m['content-type']

    return  function (res)
                local header = res.headers and res.headers['content-type']
                local body = res.body
                if header and find(header, m['content-type'], 1, true) and type(body) == 'string' then
                    local r, msg = parse(body, args)
                    if r then
                        res.body = r
                    else
                        if spore.errors then
                            spore.errors:write(msg, "\n")
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
