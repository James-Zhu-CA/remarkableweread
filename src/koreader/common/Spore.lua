
--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore>
--
local decode = require'dkjson'.decode
local parse = require'socket.url'.parse
local assert = assert
local error = error
local pairs = pairs
local require = require
local setmetatable = setmetatable
local tonumber = tonumber
local tostring = tostring
local type = type
local unpack = table.unpack or unpack
local resume = coroutine.resume
local yield = coroutine.yield
local stderr = io.stderr
local gsub = string.gsub
local match = string.match
local request = require'Spore.Protocols'.request
local slurp = require'Spore.Protocols'.slurp
local Request = require'Spore.Request'.new

local _ENV = nil
local m = {}

local function raises (response, reason)
    local ex = { response = response, reason = reason }
    local mt = { __tostring = function (self) return self.reason end }
    error(setmetatable(ex, mt))
end
m.raises = raises

local function checktype (caller, narg, arg, tname)
    assert(type(arg) == tname, "bad argument #" .. tostring(narg) .. " to "
          .. caller .. " (" .. tname .. " expected, got " .. type(arg) .. ")")
end
m.checktype = checktype

local mt = {}

local function _enable_if (self, cond, mw, args)
    if not match(mw, '^Spore%.Middleware%.') then
        mw = 'Spore.Middleware.' .. mw
    end
    local _m = require(mw)
    assert(type(_m.call) == 'function', mw .. " without a function call")
    local t = self._middlewares
    t[#t+1] = {
        cond = cond,
        code = function (req)
            local res = _m.call(args, req)
            if type(res) == 'thread' then
                yield()
                res = select(2, resume(res))
            end
            return res
        end,
    }
    if _m.early_validate == false then
        self._early_validate = false
    end
end

function mt:enable_if (cond, mw, args)
    checktype('enable_if', 2, cond, 'function')
    checktype('enable_if', 3, mw, 'string')
    args = args or {}
    checktype('enable_if', 4, args, 'table')
    _enable_if(self, cond, mw, args)
end

function mt:enable (mw, args)
    checktype('enable', 2, mw, 'string')
    args = args or {}
    checktype('enable', 3, args, 'table')
    _enable_if(self, function () return true end, mw, args)
end

function mt:reset_middlewares ()
    self._middlewares = {}
    self._early_validate = true
end

function mt:_http_request (env)
    local req = Request(env)
    local callbacks = {}
    local response
    local middlewares = self._middlewares
    for i = 1, #middlewares do
        local mw = middlewares[i]
        if mw.cond(req) then
            local res = mw.code(req)
            if type(res) == 'function' then
                callbacks[#callbacks+1] = res
            elseif type(res) == 'table' then
                if res.status == 599 then
                    return res
                end
                response = res
                break
            end
        end
    end

    if response == nil then
        req:finalize()
        response = request(req)
    end

    for i = #callbacks, 1, -1 do
        if type(response) == 'table' then
            local cb = callbacks[i]
            response = cb(response)
        end
    end

    return response
end

function mt:_wrap (name, method, prms)
    prms = prms or {}
    checktype(name, 2, prms, 'table')
    local params = {}
    for k, v in pairs(prms) do
        if type(k) == 'number' then
            v = tostring(v)
            params[v] = v
        else
            params[tostring(k)] = v
        end
    end
    local payload = params.spore_payload or params.payload
    params.spore_payload = nil
    params.payload = nil
    if method.payload then
        payload = {}
        for i = 1, #method.payload do
            local v = method.payload[i]
            payload[v] = params[v]
        end
    end

    local base_url = parse(method.base_url)
    local path_url = parse(method.path) or {}
    local path_info = (base_url.path or '') .. (path_url.path or '')
    path_info = gsub(path_info, '//', '/')

    local env = {
        REQUEST_METHOD  = method.method,
        SERVER_NAME     = base_url.host,
        SERVER_PORT     = base_url.port,
        PATH_INFO       = path_info,
        REQUEST_URI     = '',
        QUERY_STRING    = path_url.query,
        HTTP_USER_AGENT = 'lua-Spore',
        spore = {
            caller          = name,
            method          = method,
            expected        = method.expected_status,
            authentication  = method.authentication,
            params          = params,
            form_data       = method['form-data'],
            headers         = method.headers,
            payload         = payload,
            errors          = m.errors or stderr,
            debug           = m.debug,
            url_scheme      = base_url.scheme,
            format          = method.formats,
            early_validate  = self._early_validate,
        },
        sporex = {},
    }
    if method.deprecated and m.debug then
        m.debug:write(name, " is deprecated\n")
    end
    local response = self:_http_request(env)

    local expected_status = method.expected_status
    if expected_status and type(response) == 'table' then
        local status = response.status
        local found = false
        for i = 1, #expected_status do
            if status == tonumber(expected_status[i]) then
                found = true
                break
            end
        end
        if not found then
            if m.errors then
                local req = response.request
                m.errors:write(req.method, " ", req.url, "\n")
                m.errors:write(status, "\n")
            end
            raises(response, tostring(status) .. ' not expected')
        end
    end
    return response
end

local function new ()
    local obj = {
        _early_validate = true,
        _middlewares = {}
    }
    return setmetatable(obj, { __index = mt })
end

local function populate (obj, spec, opts)
    assert(spec.methods, "no method in spec")
    local methname_modifier = m.methname_modifier
    for k, v in pairs(spec.methods) do
        if type(methname_modifier) == 'function' then
            k = methname_modifier(k)
        end
        v.authentication = opts.authentication or v.authentication or spec.authentication
        v.base_url = opts.base_url or v.base_url or spec.base_url
        v.expected_status = opts.expected_status or v.expected_status or spec.expected_status
        v.formats = opts.formats or v.formats or spec.formats
        v.unattended_params = opts.unattended_params or v.unattended_params or spec.unattended_params
        assert(obj[k] == nil, k .. " duplicated")
        assert(v.method, k .. " without field method")
        assert(v.path, k .. " without field path")
        assert(type(v.expected_status or {}) == 'table', "expected_status of " .. k .. " is not an array")
        assert(type(v.required_params or {}) == 'table', "required_params of " .. k .. " is not an array")
        assert(type(v.optional_params or {}) == 'table', "optional_params of " .. k .. " is not an array")
        assert(type(v.payload or {}) == 'table', "payload of " .. k .. " is not an array")
        assert(type(v['form-data'] or {}) == 'table', "form-data of " .. k .. " is not an hash")
        assert(type(v.headers or {}) == 'table', "headers of " .. k .. " is not an hash")
        assert(v.base_url, k .. ": base_url is missing")
        local uri = parse(v.base_url)
        assert(uri, k .. ": base_url is invalid")
        assert(uri.host, k .. ": base_url without host")
        assert(uri.scheme, k .. ": base_url without scheme")
        if v.required_payload or v.optional_payload then
            assert(not v['form-data'], "payload and form-data are exclusive")
            assert(not v.payload, "payload and required_payload|optional_payload are exclusive")
        end
        obj[k] =  function (self, prms)
                      return self:_wrap(k, v, prms)
                  end
    end
end

local function new_from_lua (spec, opts, src)
    opts = opts or {}
    checktype('new_from_lua', 1, spec, 'table')
    checktype('new_from_lua', 2, opts, 'table')
    local obj = new()
    populate(obj, spec, opts)
    obj._src = src
    return obj
end
m.new_from_lua = new_from_lua

local function new_from_string (...)
    local args = {...}
    local opts = {}
    local nb
    for i = 1, #args do
        local arg = args[i]
        if i > 1 and type(arg) == 'table' then
            opts = arg
            break
        end
        checktype('new_from_string', i, arg, 'string')
        nb = i
    end

    local obj = new()
    for i = 1, nb do
        local spec, _, msg = decode(args[i])
        assert(spec, msg)
        populate(obj, spec, opts)
    end
    return obj
end
m.new_from_string = new_from_string

local function new_from_spec (...)
    local args = {...}
    local opts = {}
    local t = {}
    for i = 1, #args do
        local arg = args[i]
        if i > 1 and type(arg) == 'table' then
            opts = arg
            break
        end
        checktype('new_from_spec', i, arg, 'string')
        t[#t+1] = slurp(arg, m.debug)
    end
    t[#t+1] = opts
    return new_from_string(unpack(t))
end
m.new_from_spec = new_from_spec

m._NAME = ...
m._VERSION = '0.4.1'
m._DESCRIPTION = "lua-Spore : a generic ReST client"
m._COPYRIGHT = "Copyright (c) 2010-2025 Francois Perrad"
return m
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
