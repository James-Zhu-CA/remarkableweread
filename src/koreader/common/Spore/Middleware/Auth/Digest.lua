--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore/>
--

local openssl_digest = require'openssl.digest'.new
local parse = require'socket.url'.parse
local url = require'socket.url'.build
local error = error
local time = os.time
local byte = string.byte
local format = string.format
local gmatch = string.gmatch
local gsub = string.gsub
local request = require'Spore.Protocols'.request

local _ENV = nil
local m = {}

--  see RFC-2617

local function base16 (s)
    return (gsub(s, '.', function (c) return format('%02x', byte(c)) end))
end

local function digest (algo, s)
    return openssl_digest(algo):final(s)
end

function m.generate_nonce ()
    return format('%08x', time())
end

local function path_query (uri)
    local t = parse(uri)
    return url{ path = t.path, query = t.query }
end

function m.call (args, req)
    local function add_header ()
        args.nc = args.nc + 1
        local nc = format('%08X', args.nc)
        local cnonce = m.generate_nonce()
        local uri = path_query(req.url)
        local ha1, ha2, response
        ha1 = base16(digest('md5', args.username .. ':'
                                .. args.realm .. ':'
                                .. args.password))
        ha2 = base16(digest('md5', req.method .. ':'
                                .. uri))
        if args.qop then
            response = base16(digest('md5', ha1 .. ':'
                                         .. args.nonce .. ':'
                                         .. nc .. ':'
                                         .. cnonce .. ':'
                                         .. args.qop .. ':'
                                         .. ha2))
        else
            response = base16(digest('md5', ha1 .. ':'
                                         .. args.nonce .. ':'
                                         .. ha2))
        end
        local auth = 'Digest username="' .. args.username
                  .. '", realm="' .. args.realm
                  .. '", nonce="' .. args.nonce
                  .. '", uri="' .. uri
                  .. '", algorithm="' .. args.algorithm
                  .. '", nc=' .. nc
                  .. ', cnonce="' .. cnonce
                  .. '", response="' .. response
                  .. '", opaque="' .. args.opaque .. '"'
        if args.qop then
            auth = auth .. ', qop=' .. args.qop
        end
        req.headers['authorization'] = auth
    end  -- add_header

    if req.env.spore.authentication and args.username and args.password then
        if args.nonce then
            req:finalize()
            add_header()
        end

        return  function (res)
            if res.status == 401 and res.headers['www-authenticate'] then
                for k, v in gmatch(res.headers['www-authenticate'], '(%w+)="([^"]*)"') do
                    args[k] = v
                end
                if args.qop then
                    for v in gmatch(args.qop, '([%w%-]+)[,;]?') do
                        args.qop = v
                        if v == 'auth' then
                            break
                        end
                    end
                    if args.qop ~= 'auth' then
                        error(args.qop .. " is not supported")
                    end
                end
                if not args.algorithm then
                    args.algorithm = 'MD5'
                end
                if args.algorithm ~= 'MD5' then
                    error(args.algorithm .. " is not supported")
                end
                args.nc = 0
                add_header()
                return request(req)
            end
            return res
        end
    end
end

return m
--
-- Copyright (c) 2011-2025 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
