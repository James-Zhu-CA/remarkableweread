--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore/>
--

local base64 = require'mime'.b64
local openssl_hmac = require'openssl.hmac'.new
local url = require'socket.url'.build
local error = error
local pairs = pairs
local tostring = tostring
local random = math.random
local time = os.time
local byte = string.byte
local format = string.format
local gmatch = string.gmatch
local gsub = string.gsub
local upper = string.upper
local tconcat = table.concat
local tsort = table.sort
local escape = require 'Spore.Request'.escape
local request = require 'Spore.Protocols'.request

local _ENV = nil
local m = {}

--[[
        Homepage: https://oauth.net/

        RFC 5849 : The OAuth 1.0 Protocol
--]]

local function base16 (s)
    return (gsub(s, '.', function (c) return format('%02x', byte(c)) end))
end

local function hmac (algo, s, key)
    return openssl_hmac(key, algo):final(s)
end

function m.generate_timestamp ()
    return tostring(time())
end

function m.generate_nonce ()
    return base16(hmac('sha1', tostring(random()) .. 'random' .. tostring(time()), 'keyyyy'))
end

function m.call (args, req)
    local env = req.env
    local spore = env.spore
    local oparams

    local function base_string ()
        local query_keys, query_vals = {}, {}
        local query_string = env.QUERY_STRING
        if query_string then
            for k, v in gmatch(query_string, '([^=]+)=([^&]*)&?') do
                query_keys[#query_keys+1] = k
                query_vals[k] = v
            end
        end
        local payload = spore.payload
        if payload then
            local ct = req.headers['content-type']
            if not ct or ct == 'application/x-www-form-urlencoded' then
                for k, v in gmatch(payload, '([^=&]+)=?([^&]*)&?') do
                    query_keys[#query_keys+1] = k
                    query_vals[k] = v:gsub('+', '%%20')
                end
            end
        end

        local scheme = spore.url_scheme
        local port = env.SERVER_PORT
        if port == '80' and scheme == 'http' then
            port = nil
        end
        if port == '443' and scheme == 'https' then
            port = nil
        end
        local base_url = url{
            scheme  = scheme,
            host    = env.SERVER_NAME,
            port    = port,
            path    = env.PATH_INFO,
            -- no query
        }
        for k, v in pairs(oparams) do
            query_keys[#query_keys+1] = k
            query_vals[k] = v
        end
        tsort(query_keys)
        local params = {}
        for i = 1, #query_keys do
            local k = query_keys[i]
            local v = query_vals[k]
            params[#params+1] = k .. '=' .. v
        end
        local normalized = tconcat(params, '&')

        return upper(req.method) .. '&' .. escape(base_url) .. '&' .. escape(normalized)
    end  -- base_string

    if spore.authentication
    and args.oauth_consumer_key and args.oauth_consumer_secret then
        oparams = {
            oauth_signature_method  = args.oauth_signature_method or 'HMAC-SHA1',
            oauth_consumer_key      = args.oauth_consumer_key,
            oauth_token             = args.oauth_token,
            oauth_verifier          = args.oauth_verifier,
        }
        if not oparams.oauth_token then
            oparams.oauth_callback  = args.oauth_callback or 'oob'      -- out-of-band
        end
        for k, v in pairs(oparams) do
            oparams[k] = escape(v)
        end

        req:finalize()

        local signature_key = escape(args.oauth_consumer_secret) .. '&' .. escape(args.oauth_token_secret or '')
        local oauth_signature
        if args.oauth_signature_method == 'PLAINTEXT' then
            oauth_signature = escape(signature_key)
        else
            oparams.oauth_timestamp = m.generate_timestamp()
            oparams.oauth_nonce = m.generate_nonce()
            local oauth_signature_base_string = base_string()
            if oparams.oauth_signature_method == 'HMAC-SHA1' then
                local hmac_binary = hmac('sha1', oauth_signature_base_string, signature_key)
                local hmac_b64 = base64(hmac_binary)
                oauth_signature = escape(hmac_b64)
            else
                error(oparams.oauth_signature_method .. " is not supported")
            end
            spore.oauth_signature_base_string = oauth_signature_base_string
        end

        local auth = 'OAuth'
        if args.realm then
            auth = auth .. ' realm="' .. tostring(args.realm) .. '",'
        end
        auth = auth ..  ' oauth_consumer_key="' .. oparams.oauth_consumer_key .. '"'
                    .. ', oauth_signature_method="' .. oparams.oauth_signature_method .. '"'
                    .. ', oauth_signature="' .. oauth_signature ..'"'
        if oparams.oauth_signature_method ~= 'PLAINTEXT' then
            auth = auth .. ', oauth_timestamp="' .. oparams.oauth_timestamp .. '"'
                        .. ', oauth_nonce="' .. oparams.oauth_nonce .. '"'
        end
        if not oparams.oauth_token then      -- 1) request token
            auth = auth .. ', oauth_callback="' .. oparams.oauth_callback .. '"'
        else
            if oparams.oauth_verifier then   -- 2) access token
                auth = auth .. ', oauth_token="' .. oparams.oauth_token .. '"'
                            .. ', oauth_verifier="' .. oparams.oauth_verifier .. '"'
            else                            -- 3) client requests
                auth = auth .. ', oauth_token="' .. oparams.oauth_token .. '"'
            end
        end
        req.headers['authorization'] = auth

        return request(req)
    end
end

return m
--
-- Copyright (c) 2010-2025 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
