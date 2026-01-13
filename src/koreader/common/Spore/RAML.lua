
--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore>
--

local load = require'lyaml'.load
local pairs = pairs
local tonumber = tonumber
local gmatch = string.gmatch
local gsub = string.gsub
local sub = string.sub
local upper = string.upper
local Spore = require'Spore'
local checktype = require'Spore'.checktype
local new_from_lua = require'Spore'.new_from_lua
local slurp = require'Spore.Protocols'.slurp

local _ENV = nil
local m = {}

m.spore = '1.1'

local function convert_uri_template (uri)
    -- see RFC 6570
    return (gsub(uri, '{([%w_]+)}', ':%1'))
end

local ops = {
    get = true,
    put = true,
    post = true,
    delete = true,
    options = true,
    head = true,
    patch = true,
}

local convert = function(doc)
    local spore = {
        name = doc.title,
        description = doc.description,
        version = doc.version,
        base_url = doc.baseUri,
        methods = {},
        authentication = doc.securedBy and true or nil,
    }

    local function populate (resources, base, auth)
        for rpath, data in pairs(resources) do
            if sub(rpath, 1, 1) == '/' then
                local path = base .. rpath
                populate(data, path, data.securedBy and true or auth)
                for op, meth in pairs(data) do
                    if ops[op] then
                        local headers
                        if meth.headers then
                            headers = {}
                            for name in pairs(meth.headers) do
                                headers[name] = ':' .. name
                            end
                        end

                        local required_params, optional_params
                        if meth.queryParameters then
                            for name, v in pairs(meth.queryParameters) do
                                if v.required then
                                    if not required_params then
                                        required_params = {}
                                    end
                                    required_params[#required_params+1] = name
                                else
                                    if not optional_params then
                                        optional_params = {}
                                    end
                                    optional_params[#optional_params+1] = name
                                end
                            end
                        end
                        for name in gmatch(path, '{([%w_]+)}') do
                            if not required_params then
                                required_params = {}
                            end
                            required_params[#required_params+1] = name
                        end

                        local expected_status
                        if meth.responses then
                            expected_status = {}
                            for status in pairs(meth.responses) do
                                expected_status[#expected_status+1] = tonumber(status)
                            end
                        end

                        local required_payload
                        local form_data
                        if meth.body then
                            local form = meth.body['multipart/form-data']
                            if form then
                                form_data = {}
                                for name in pairs(form.properties) do
                                    form_data[name] = ':' .. name
                                end
                            else
                                required_payload = true
                            end
                        end

                        spore.methods[meth.displayName or (op .. path)] = {
                            method = upper(op),
                            path = (m.spore == '1.0') and convert_uri_template(path) or path,
                            headers = headers,
                            ['form-data'] = form_data,
                            required_params = required_params,
                            optional_params = optional_params,
                            required_payload = required_payload,
                            expected_status = expected_status,
                            authentication = meth.securedBy and true or auth,
                            description = meth.description,
                        }
                    end
                end
            end
        end
    end  -- populate

    populate(doc, '', spore.authentication)
    return spore
end
m.convert = convert

function m.new_from_raml (api, opts)
    opts = opts or {}
    checktype('new_from_raml', 1, api, 'string')
    checktype('new_from_raml', 2, opts, 'table')
    local doc = load(slurp(api, Spore.debug))
    return new_from_lua(convert(doc), opts, doc)
end

return m
--
-- Copyright (c) 2024-2025 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
