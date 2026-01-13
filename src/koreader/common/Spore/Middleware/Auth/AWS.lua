--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore/>
--

--[[
    See http://docs.amazonwebservices.com/AmazonS3/latest/dev/index.html?RESTAuthentication.html
]]

local base64 = require'mime'.b64
local openssl_digest = require'openssl.digest'.new
local openssl_hmac = require'openssl.hmac'.new
local pairs = pairs
local tostring = tostring
local byte = string.byte
local format = string.format
local gsub = string.gsub
local lower = string.lower
local match = string.match
local tconcat = table.concat
local tsort = table.sort
local date = os.date
local request = require 'Spore.Protocols'.request

local _ENV = nil
local m = {}

m.early_validate = false

local function base16 (s)
    return (gsub(s, '.', function (c) return format('%02x', byte(c)) end))
end

local function digest (algo, s)
    return openssl_digest(algo):final(s)
end

local function hmac (algo, s, key)
    return openssl_hmac(key, algo):final(s)
end

function m.call (args, req)
    local env = req.env
    local query = env.QUERY_STRING or ''
    local spore = env.spore
    local bucket = tostring(spore.params.bucket or '')

    local function get_canonical_headers ()
        local headers_amz = {}
        for k in pairs(req.headers) do
            if match(k, '^x%-amz%-') then
                headers_amz[#headers_amz+1] = k
            end
        end
        if #headers_amz == 0 then
            return ''
        else
            tsort(headers_amz)
            local lines = {}
            for i = 1, #headers_amz do
                local k = headers_amz[i]
                lines[#lines+1] = k .. ':' .. req.headers[k]
            end
            return tconcat(lines, "\n") .. "\n"
        end
    end -- get_canonical_headers

    local function get_string_to_sign ()
        if bucket ~= '' then
            bucket = '/' .. bucket
        end
        local object = '/' .. tostring(spore.params.object or '')
        if query ~= '' then
            query = '?' .. query
        end

        return req.method .. "\n"
            .. (req.headers['content-md5'] or '') .. "\n"
            .. (req.headers['content-type'] or '') .. "\n"
            .. (req.headers['date'] or '') .. "\n"
            .. get_canonical_headers()
            .. bucket .. object .. query
    end -- get_string_to_sign

    if spore.authentication and args.aws_access_key and args.aws_secret_key then
        if spore.params.bucket then
            env.SERVER_NAME = bucket .. '.' .. env.SERVER_NAME
            spore.params.bucket = nil
        end

        for k, v in pairs(spore.params) do
            k = tostring(k)
            if match(k, '^x%-amz%-') then
                req.headers[lower(k)] = tostring(v)
                spore.params[k] = nil
            end
        end

        req:finalize()

        if spore.headers and spore.headers['Date'] == 'AWS' then
            req.headers['date'] = date("!%a, %d %b %Y %H:%M:%S GMT")
        end

        local payload = spore.payload
        if payload then
            req.headers['content-length'] = tostring(#payload)
            req.headers['content-type'] = req.headers['content-type'] or 'application/x-www-form-urlencoded'
            if spore.headers and spore.headers['Content-MD5'] == 'AWS' then
                req.headers['content-md5'] = base16(digest('md5', payload))
            end
        end

        req.headers['authorization'] = 'AWS '
          .. args.aws_access_key .. ':'
          .. base64(hmac('sha1', get_string_to_sign(), args.aws_secret_key))

        return request(req)
    end
end

return m

--
-- Copyright (c) 2011-2025 Francois Perrad
-- Copyright (c) 2011 LogicEditor.com: Alexander Gladysh, Vladimir Fedin
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
