---
name: Test Tether with Expect
description: How to run tests against the tether and firmware using Expect scripts
---

When asked to "test tether with expect" or run an interactive test against the tether, you should use `expect` scripts.

The tether program uses `stty` to configure the terminal, which fails when run in standard headless background tasks. Using `expect` automatically allocates a pseudo-terminal (PTY) for the tether, allowing `stty` to succeed and the Tcl shell to be interacted with cleanly.

### Reference Example
There is a working example script in `v1/tether/demo_tether_with_expect.exp` that you can reference or adapt.

To execute an expect script, simply run it with the `expect` command:
```bash
cd v1/tether
expect demo_tether_with_expect.exp
```

### Writing new Expect Scripts
When writing new expect scripts to interact with the tether:
1. Always `spawn sh run2.sh` to launch the tether.
2. Wait for the `TCL>` prompt using `expect "TCL>"`.
3. Send commands using `send "your_command_here\r"`.
4. Conclude the script with `expect eof` or send an exit command if applicable.
5. Create new expect scripts inside the `v1/tether` directory.

### Quick Headless Commands
For simple, one-shot operations that do not require an interactive terminal, the tether provides several `--quick-*` flags. These bypass the interactive setup and can be run safely as standard background tasks.

Assuming you are in the `v1/tether` directory:

1. **Test Liveness (Ping)**: Sends a PicoRPC ping with a payload and verifies the echo.
   ```bash
   build/tether --quick-ping 42
   ```
2. **Restart Firmware**: Reboots the firmware on the Pico.
   ```bash
   build/tether --quick-restart
   ```
3. **Reflash Firmware**: Reboots the Pico into BOOTSEL mode and automatically copies the specified UF2 file to the mounted filesystem.
   ```bash
   build/tether --quick-reflash ../firmware/build/centipede.uf2
   ```
