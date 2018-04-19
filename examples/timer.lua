local hive = require "hive"
local timer = require "hive.timer"
local log = require("hive.log").log


local M = {}


function M:on_create()
    id0 = timer.register(0, function ()
            log("timer0", id0)
        end)

    id1 = timer.register(10, function ()
            log("timer1", id1)
        end)

    id2 = timer.register(77, function ()
            log("timer2", id2)
        end)

    id3 = timer.register(200, function ()
            log("timer3", id3)
            id4 = timer.register(0, function ()
                log("timer4", id4)
                id5 = timer.register(0x55, function ()
                        log("timer5", id5)
                        hive.abort()
                    end)
                end)

            timer.unregister(id4)
        end)
end


hive.start(M)