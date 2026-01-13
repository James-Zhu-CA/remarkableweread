
--
-- lua-Spore : <https://fperrad.frama.io/lua-Spore>
--

local load = require'lyaml'.load
local Spore = require'Spore'
local checktype = require'Spore'.checktype
local new_from_lua = require'Spore'.new_from_lua
local slurp = require'Spore.Protocols'.slurp
local convert = require'Spore.Swagger'.convert

local _ENV = nil
local m = {}

m.convert = convert

function m.new_from_openapi (api, opts, tag)
    opts = opts or {}
    checktype('new_from_openapi', 1, api, 'string')
    checktype('new_from_openapi', 2, opts, 'table')
    checktype('new_from_openapi', 3, tag or '', 'string')
    local doc = load(slurp(api, Spore.debug))
    return new_from_lua(convert(doc, tag), opts, doc)
end

return m
--
-- Copyright (c) 2024-2025 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
