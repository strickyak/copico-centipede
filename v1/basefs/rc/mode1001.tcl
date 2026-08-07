# Wait for the /pc filesystem to become available over USB RPC.
puts "(Waiting for /pc to mount...)"
while {1} {
    if {![catch {find /pc} err]} {
        break
    }
    sleep 1
}

# For testing.
puts "(Sourcing /pc/mode1001.tcl)"
puts "((("
if {[catch {
    source /pc/mode1001.tcl
    fs echo OKAY > /pc/result1001
} err ]} {
    fs echo "FAIL: $err" > /pc/result1001
    puts "FAIL: $err"
}
puts ")))"
