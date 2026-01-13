--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore/>
--

--[[
    See http://www.data-publica.com/content/api/
]]

local openssl_digest = require'openssl.digest'.new
local parse = require'socket.url'.parse
local url = require'socket.url'.build
local pairs = pairs
local tostring = tostring
local byte = string.byte
local format = string.format
local gsub = string.gsub
local tconcat = table.concat
local tsort = table.sort
local request = require'Spore.Protocols'.request

local _ENV = nil
local m = {}

m.early_validate = false

local function base16 (s)
    return (gsub(s, '.', function (c) return format('%02x', byte(c)) end))
end

local function digest (algo, s)
    return openssl_digest(algo):final(s)
end

function m.call (args, req)
    local env = req.env
    local spore = env.spore
    local params = spore.params

    local function get_string_to_sign ()
        local u = parse(req.url)
        u.query = nil
        local t = { url(u) }            -- url without query

        local names = {}
        for k in pairs(params) do
            if k ~= 'reference' and k ~= 'tablename' then
                names[#names+1] = k
            end
        end
        tsort(names)
        for i = 1, #names do
            local name = names[i]
            t[#t+1] = name .. '=' .. tostring(spore.params[name])
        end
        t[#t+1] = args.password
        return tconcat(t, ',')
    end -- get_string_to_sign

    if spore.authentication and args.key and args.password then
        params.format = params.format or 'json'
        params.limit = params.limit or 50
        params.offset = params.offset or 0
        params.key = args.key

        req:finalize()
        req.url = req.url .. '&signature=' .. base16(digest('sha1', get_string_to_sign()))

        return request(req)
    end
end

return m

--
-- Copyright (c) 2012-2025 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
