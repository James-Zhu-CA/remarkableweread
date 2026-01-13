--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore/>
--

local base64 = require'mime'.b64        -- luasocket
local url = require'socket.url'.build
local assert = assert
local next = next
local pairs = pairs
local require = require
local setmetatable = setmetatable
local tostring = tostring
local unpack = table.unpack or unpack
local random = math.random
local byte = string.byte
local char = string.char
local format = string.format
local gsub = string.gsub
local lower = string.lower
local match = string.match
local sub = string.sub
local tconcat = table.concat
local slurp = require'Spore.Protocols'.slurp

if _VERSION < 'Lua 5.4' then
    math.randomseed(os.time())
end

local _ENV = nil
local m = {}
local mt = {}

m.redirect = false

local function validate (spore)
    local caller = spore.caller
    local method = spore.method
    local params = spore.params
    local payload = spore.payload
    if method.required_payload then
        assert(payload, "payload is required for method " .. caller)
    end
    if payload then
        assert(method.required_payload
            or method.optional_payload
            or method.payload, "payload is not expected for method " .. caller)
    end

    local required_params = method.required_params or {}
    for i = 1, #required_params do
        local v = required_params[i]
        assert(params[v], v .. " is required for method " .. caller)
    end

    if not method.unattended_params then
        local optional_params = method.optional_params or {}
        for param in pairs(params) do
            if not match(param, '^oauth_') then
                local found = false
                for i = 1, #required_params do
                    if param == required_params[i] then
                        found = true
                        break
                    end
                end
                if not found then
                    for i = 1, #optional_params do
                        if param == optional_params[i] then
                            found = true
                            break
                        end
                    end
                end
                assert(found, param .. " is not expected for method " .. caller)
            end
        end
    end
end

function m.new (env)
    local spore = env.spore
    if spore.early_validate then
        validate(spore)
    end
    local obj = {
        env = env,
        redirect = m.redirect,
        headers = {
            ['user-agent'] = env.HTTP_USER_AGENT,
        },
    }
    return setmetatable(obj, { __index = mt })
end

local function escape (s)
    -- see RFC 3986 & RFC 5849
    -- unreserved
    return (gsub(s, '[^-._~%w]', function(c)
        return format('%%%02X', byte(c))
    end))
end
m.escape = escape

local function escape_path (s)
    -- see RFC 3986
    -- unreserved + slash
    return (gsub(s, '[^-._~%w/]', function(c)
        return format('%%%02X', byte(c))
    end))
end

local function boundary (size)
    local t = {}
    for _ = 1, 3 * size do
        t[#t+1] = random(256) - 1
    end
    return (gsub(base64(char(unpack(t))), '%W', 'X'))
end

local function _form_data (data, debug_)
    local p = {}
    for k, v in pairs(data) do
        if sub(v, 1, 1) == '@' then
            local fullpath = sub(v, 2)
            local fname = match(fullpath, '([^/\\]+)$')
            local content = slurp(fullpath, debug_)
            p[#p+1] = 'content-disposition: form-data; name="' .. k .. '"; filename="' .. fname ..'"\r\n'
                   .. 'content-type: application/octet-stream\r\n\r\n'
                   .. content
        else
            p[#p+1] = 'content-disposition: form-data; name="' .. k .. '"\r\n\r\n' .. v
        end
    end

    local b = boundary(10)
    local t = {}
    for i = 1, #p do
        t[#t+1] = '--'
        t[#t+1] = b
        t[#t+1] = '\r\n'
        t[#t+1] = p[i]
        t[#t+1] = '\r\n'
    end
    t[#t+1] = '--'
    t[#t+1] = b
    t[#t+1] = '--'
    t[#t+1] = '\r\n'
    return tconcat(t), b
end

function mt:finalize ()
    local function gsub2 (s, patt1, patt2, repl)
        repl = gsub(repl, '%%', '%%%%')
        local r, n = gsub(s, patt1, repl)
        if n == 0 then
            r, n = gsub(s, patt2, repl)
        end
        return r, n
    end -- gsub2

    if self.url then
        return
    end
    local env = self.env
    local spore = env.spore
    local payload = spore.method.payload or {}
    if not spore.early_validate then
        validate(spore)
    end
    local path_info = env.PATH_INFO
    local query_string = env.QUERY_STRING
    local form_data = {}
    for k, v in pairs(spore.form_data or {}) do
        form_data[tostring(k)] = tostring(v)
    end
    local headers = {}
    for k, v in pairs(spore.headers or {}) do
        headers[lower(tostring(k))] = tostring(v)
    end
    local query = {}
    if query_string then
        query[1] = query_string
    end
    local form = {}
    for k, v in pairs(spore.params) do
        k = tostring(k)
        v = tostring(v)
        local patt = ':' .. k
        local patt6570 = '{' .. k .. '}'        -- see RFC 6570
        local n
        path_info, n = gsub2(path_info, patt, patt6570, escape_path(v))
        for kk, vv in pairs(form_data) do
            local nn
            vv, nn = gsub2(vv, patt, patt6570, v)
            if nn > 0 then
                form_data[kk] = vv
                form[kk] = vv
                n = n + 1
            end
        end
        for kk, vv in pairs(headers) do
            local nn
            vv, nn = gsub2(vv, patt, patt6570, v)
            if nn > 0 then
                headers[kk] = vv
                self.headers[kk] = vv
                n = n + 1
            end
        end
        for i = 1, #payload do
            if k == payload[i] then
                n = n + 1
            end
        end
        if n == 0 then
            query[#query+1] = escape(k) .. '=' .. escape(v)
        end
    end
    if #query > 0 then
        query_string = tconcat(query, '&')
    end
    env.PATH_INFO = path_info
    env.QUERY_STRING = query_string
    if spore.form_data then
        spore.form_data = form
    end
    self.method = env.REQUEST_METHOD

    payload = spore.payload
    if payload then
        self.headers['content-length'] = tostring(#payload)
        self.headers['content-type'] = self.headers['content-type'] or 'application/x-www-form-urlencoded'
        spore.body = payload
    end

    if next(form) then
        local content, _boundary = _form_data(form, spore.debug)
        self.headers['content-length'] = tostring(#content)
        self.headers['content-type'] = 'multipart/form-data; boundary=' .. _boundary
        spore.body = content
    end

    self.url = url{
        scheme  = spore.url_scheme,
        host    = env.SERVER_NAME,
        port    = env.SERVER_PORT,
        path    = path_info,
        query   = query_string,
    }
end

return m
--
-- Copyright (c) 2010-2025 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
