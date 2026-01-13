
--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore>
--

local ltn12 = require'ltn12'            -- luasocket
local parse = require'socket.url'.parse -- luasocket
local assert = assert
local pairs = pairs
local pcall = pcall
local tonumber = tonumber
local tostring = tostring
local create = coroutine.create
local yield = coroutine.yield
local open = io.open
local match = string.match
local tconcat = table.concat

local protocol, http_request, http_tls, openssl_context
if pcall(require, 'http.version') then                  -- lua-http
    http_request = require'http.request'
    http_tls = require'http.tls'
    openssl_context = require'openssl.ssl.context'
else
    local r, https = pcall(require, 'ssl.https')        -- luasec
    protocol = {
        http  = require'socket.http',                   -- luasocket
        https = r and https or nil,
    }
end

local _ENV = nil
local m = {}

local function slurp (name, debug_)
    local uri = parse(name)
    if not uri.scheme or uri.scheme == 'file' then
        local f, msg = open(uri.path)
        assert(f, msg)
        local content = f:read '*a'
        f:close()
        return content
    else
        local res = m.request{
            env = {
                spore = {
                    url_scheme = uri.scheme,
                    debug = debug_,
                },
            },
            method = 'GET',
            url = name,
        }
        assert(res.status == 200, tostring(res.status) .. " not expected")
        return res.body
    end
end
m.slurp = slurp

if protocol then
    local function request (req)
        local spore = req.env.spore
        local prot = protocol[spore.url_scheme]
        assert(prot, "not protocol " .. spore.url_scheme)

        local body = spore.body
        if body then
            req.source = ltn12.source.string(body)
        end

        if req.method == 'POST' and not req.headers['content-length'] then
            req.headers['content-length'] = '0'
        end

        local t = {}
        req.sink = ltn12.sink.table(t)

        if spore.debug then
            spore.debug:write(req.method, " ", req.url, "\n")
            for k, v in pairs(req.headers or {}) do
                spore.debug:write(k, ": ", v, "\n")
            end
        end
        local _, status, headers, line = prot.request(req)
        assert(_, status)
        if spore.debug then
            spore.debug:write(line, "\n")
        end
        return {
            request = req,
            status = status,
            headers = headers,
            body = tconcat(t),
        }
    end
    m.request = request
else
    local function request (sreq)
        local env = sreq.env
        local spore = env.spore
        local req = http_request.new_from_uri(sreq.url)
        req.env = env
        req.follow_redirects = sreq.redirect
        req.headers:upsert(':method', sreq.method)
        for k, v in pairs(sreq.headers) do
            if k == 'host' then
                req.headers:upsert(':authority', v)
            elseif k == 'user-agent' then
                req.headers:upsert(k, v)
            else
                req.headers:append(k, v)
            end
        end

        local body = spore.body
        if body then
            req:set_body(body)
        end

        if req.tls then
            local ctx = http_tls.new_client_context()
            ctx:setVerify(openssl_context.VERIFY_NONE)
            req.ctx = ctx
        end

        if spore.debug then
            spore.debug:write(req.headers:get(':method'), " ", req:to_uri(), "\n")
            for k, v in req.headers:each() do
                spore.debug:write(k, ": ", v, "\n")
            end
        end
        local headers, stream = req:go()
        assert(headers, stream)
        if spore.debug then
            for k, v in headers:each() do
                spore.debug:write(k, ": ", v, "\n")
            end
        end

        local ct = headers:get('content-type')
        if ct and match(ct, '^text/event%-stream') then
            return create(function ()
                while true do
                    local line = stream:get_body_until('\n\n', true)
                    if line == '' then
                        break
                    end
                    yield(line)
                end
            end)
        end

        local status = tonumber(headers:get(':status'))
        local h = {}
        for k in headers:each() do
            if k ~= ':status' and not h[k] then
                h[k] = headers:get_comma_separated(k)
            end
        end
        body = stream:get_body_as_string()
        stream:shutdown()
        return {
            request = sreq,
            status = status,
            headers = h,
            body = body,
        }
    end
    m.request = request
end

return m
--
-- Copyright (c) 2010-2025 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
