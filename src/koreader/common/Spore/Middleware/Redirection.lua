--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore/>
--

local absolute = require'socket.url'.absolute
local parse = require'socket.url'.parse
local url = require'socket.url'.build
local request = require'Spore.Protocols'.request

local _ENV = nil
local m = {}

m.max_redirect = 5

function m.call (_args, req)
    local redirect_status = {
        [301] = true,  -- Moved Permanently
        [302] = true,  -- Found (Moved Temporarily)
        [303] = true,  -- See Other
        [307] = true,  -- Temporary Redirect
    }
    local nredirect = 0

    return  function (res)
                while nredirect < m.max_redirect do
                    local location = res.headers and res.headers['location']
                    local status = res.status
                    if location and redirect_status[status] then
                        if req.headers['host'] then
                            local uri = parse(location)
                            req.headers['host'] = uri.host
                            local proxy = parse(req.url)
                            uri.host = proxy.host
                            uri.port = proxy.port
                            req.url = url(uri)
                            req.env.spore.url_scheme = uri.scheme
                        else
                            req.url = absolute(req.url, location)
                            req.env.spore.url_scheme = parse(location).scheme
                        end
                        req.headers['cookie'] = nil
                        res = request(req)
                        nredirect = nredirect + 1
                    else
                        break
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
