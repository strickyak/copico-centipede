send "expr 39+3\n"

expect {
    43      {puts ...OKAY\n ; exp_continue }
    timeout {puts ...TIMEOUT\n ; exit 13 }
}

expect TCL>
