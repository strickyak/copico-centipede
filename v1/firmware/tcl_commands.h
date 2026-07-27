#ifndef FIRMWARE_PIO_TCL_COMMANDS_H_
#define FIRMWARE_PIO_TCL_COMMANDS_H_

#include <string.h>

#include "../tcl6.7c/tcl.h"
#include "flash-label.h"
#include "littlefs.h"
#include "heuristic_file.h"

int file_cmd(ClientData clientData, Tcl_Interp* interp, int argc,
               char* argv[]) {
  if (argc < 2) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                     " filename ?filename...?\"", NULL);
    return TCL_ERROR;
  }

  for (int i = 1; i < argc; i++) {
    const char* type = HeuristicFileType(argv[i]);
    if (argc == 2) {
      Tcl_AppendResult(interp, (char*)type, NULL);
    } else {
      Tcl_AppendResult(interp, argv[i], ": ", (char*)type, "\n", NULL);
    }
  }

  return TCL_OK;
}

int nice_cmd(ClientData clientData, Tcl_Interp* interp, int argc,
               char* argv[]) {
  if (argc != 2) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                     " filename\"", NULL);
    return TCL_ERROR;
  }

  if (IsNiceFilename(argv[1])) {
    Tcl_SetResult(interp, (char*)"1", TCL_STATIC);
  } else {
    Tcl_SetResult(interp, (char*)"0", TCL_STATIC);
  }
  return TCL_OK;
}

void register_tcl_commands(Tcl_Interp* interp) {
  // Register commands from littlefs.h
  Tcl_CreateCommand(interp, (char*)"ls", dir_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"mkdir", mkdir_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"rmdir", rmdir_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"echo", echo_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"cp", cp_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"mv", mv_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"rm", rm_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"cat", cat_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"wc", wc_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"cd", cd_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"pwd", pwd_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"df", df_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"du", du_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"fs", fs_cmd, NULL, NULL);

  // Register commands from tcl_commands.h
  Tcl_CreateCommand(interp, (char*)"file", file_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"nice", nice_cmd, NULL, NULL);

  // Populate global Tcl array 'Label'
  if (FlashLabel::Label[0] == 'p' && FlashLabel::Label[1] == '\0' &&
      FlashLabel::Label[2] == '1' && FlashLabel::Label[3] == '\0') {
    const char* p = FlashLabel::Label;
    while (*p) {
      const char* q = p + strlen(p) + 1;  // q is the value
      Tcl_SetVar2(interp, (char*)"Label", (char*)p, (char*)q, TCL_GLOBAL_ONLY);
      p = q + strlen(q) + 1;
    }
  }
}

#endif  // FIRMWARE_PIO_TCL_COMMANDS_H_
