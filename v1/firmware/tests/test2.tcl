send "ini names pc/test2.ini\n"
must "{} Section1 {Section 2}"
must "TCL>"

send "ini get pc/test2.ini {}\n"
must "global_key1=abc"
must "global_key2=def"
must "TCL>"

send "ini get pc/test2.ini Section1\n"
must "key1=val1"
must "TCL>"

send "ini get pc/test2.ini {Section 2}\n"
must "key2=val2"
must "TCL>"
