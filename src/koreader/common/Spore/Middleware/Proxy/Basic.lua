--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore/>
--

local base64 = require'mime'.b64
local parse = require'socket.url'.parse
local url = require'socket.url'.build
local assert = assert
local getenv = os.getenv
local upper = string.upper

local _ENV = nil
local m = {}

local function _env_proxy (scheme)
    local name = upper(scheme) .. '_PROXY'
    local v = getenv(name)
    assert(v, "no " .. name)
    local proxy = parse(v)
    return {
        proxy = url{
            scheme  = proxy.scheme,
            host    = proxy.host,
            port    = proxy.port,
        },
        userinfo    = proxy.userinfo,
    }
end

local cache = {}
local function env_proxy (scheme)
    local r = cache[scheme]
    if not r then
        r = _env_proxy(scheme)
        cache[scheme] = r
    end
    return r
end

function m.call (args, req)
    local env = req.env
    if not args.proxy then
        args = env_proxy(env.spore.url_scheme)
    end
    req.headers['host'] = env.SERVER_NAME

    local proxy = parse(args.proxy)
    env.SERVER_NAME = proxy.host
    env.SERVER_PORT = proxy.port

    local userinfo
    if args.userinfo then
        userinfo = args.userinfo
    elseif args.username and args.password then
        userinfo = args.username .. ':' .. args.password
    end
    if userinfo then
        req.headers['proxy-authorization'] = 'Basic ' .. base64(userinfo)
    end
end

return m
--
-- Copyright (c) 2010-2025 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
