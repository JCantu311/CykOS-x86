local passed = 0

local function check(name, condition)
    if not condition then
        error("FAIL: " .. name)
    end
    passed = passed + 1
    print("PASS: " .. name)
end

check("script arguments", type(arg) == "table" and type(arg[0]) == "string")
check("integer arithmetic", 6 * 7 == 42 and 17 // 5 == 3 and 17 % 5 == 2)
check("floating point", 0.125 * 8.0 == 1.0 and 2.0 ^ 3.0 == 8.0 and 6.5 % 2.0 == 0.5)
check("string library", string.upper("cykos") == "CYKOS")
check("string patterns", string.match("abc123", "%a+%d+") == "abc123")
check("table sort and concat", (function()
    local values = { 4, 1, 3, 2 }
    table.sort(values)
    return table.concat(values, ",") == "1,2,3,4"
end)())
check("UTF-8 library", utf8.len("CykOS: \226\156\147") == 8)
check("coroutine resume", (function()
    local thread = coroutine.create(function(value)
        coroutine.yield(value + 1)
        return value + 2
    end)
    local ok, first = coroutine.resume(thread, 40)
    if not ok or first ~= 41 then return false end
    local finished, second = coroutine.resume(thread)
    return finished and second == 42
end)())
check("protected Lua error", (function()
    local ok, message = pcall(function() error("expected test error") end)
    return not ok and string.find(message, "expected test error") ~= nil
end)())
check("load and execute source", (function()
    local chunk, message = load("return 6 * 7")
    return chunk ~= nil and chunk() == 42
end)())

print("Lua smoke test passed: " .. passed .. " checks")
