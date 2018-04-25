local hive = require "hive"
local socket = require "hive.socket"
local timer = require "hive.timer"
local thread = require "hive.thread"

local log = require("hive.log").log
local logf = require("hive.log").logf


local M = {}

local players_map = {}
local players_slice_data = {}

local function id2addr(client_id)
    local host, port = socket.addrinfo(client_id)
    return string.format("%s:%s", host, port)
end

local function player_server(client_id)
    socket.attach(client_id)
    players_map[client_id] = id2addr(client_id)
    while true do
        local data, err = socket.read(client_id)
        if not data then
            players_map[client_id] = nil
            error(err)
        elseif #data == 0 then
            local addr = players_map[client_id]
            logf("player: %s is exit", addr)
            players_map[client_id] = nil
            break
        else
            table.insert(players_slice_data, data)
        end
    end
end


local function timer_server()
    local s = table.concat(players_slice_data)

    if #s > 0 then
        for id, _ in pairs(players_map) do
            socket.send(id, s)
        end
        players_slice_data = {}
    end

    -- next trigger
    timer.register(100, timer_server)
end


function M:on_create()
    local host = "0.0.0.0"
    local port = 9987
    local id, err = socket.listen(host, port, 
        function (client_id)
            local host, port = socket.addrinfo(client_id)
            logf("[accept] %s:%s", host, port)
            thread.run(player_server, client_id)
        end)
    assert(id, err)
    logf("hub listen: %s:%s", host, port)

    -- 2s timer
    timer.register(100, timer_server)
end


hive.start(M)