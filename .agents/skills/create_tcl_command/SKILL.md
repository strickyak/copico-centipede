---
name: Create Tcl Command
description: How to create a Tcl command in the firmware.
---

When asked to create or add a new Tcl command or ensemble command to the firmware, follow these steps:

1. **Locate the command definitions**:
   Tcl commands are typically defined as C functions (e.g., `centipede_cmd`) in `v1/firmware/tcl_commands.h`. They follow the signature:
   `int command_name(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[])`. 
   The commands are registered at the bottom of the file in `register_tcl_commands(Tcl_Interp* interp)` using `Tcl_CreateCommand`.

2. **Implement the logic**:
   - Parse `argc` and `argv` to determine the subcommand and arguments. For ensemble commands, use `strcmp(argv[1], "subcommand")` for routing.
   - Return errors by placing a message in the interpreter result using `Tcl_SetResult(interp, "message", TCL_STATIC/TCL_VOLATILE)` or `Tcl_AppendResult(interp, ...)` and returning `TCL_ERROR`.
   - On success, set the result (if any) and return `TCL_OK`.

3. **Update the documentation**:
   Always document the new command or subcommand in the Tcl scripting manual located at `v1/doc/centipede_scripting_manual.md`. Place it in the appropriate section (Filesystem, Control, or Utilities).

4. **Recompile to verify**:
   Verify that the C++ code compiles successfully by running the "Compile Firmware" skill (`cd v1/firmware && ./build.sh`).
