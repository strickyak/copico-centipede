#ifndef FIRMWARE_PIO_TCL_COMMANDS_H_
#define FIRMWARE_PIO_TCL_COMMANDS_H_

#include <string.h>

#include "../tcl6.7c/tcl.h"
#include "flash-label.h"
#include "littlefs.h"
#include "heuristic_file.h"
#include "cobs_tx.h"
#include "console.h"
extern "C" {
#include "../tcl6.7c/regexp.h"
}

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

int grep_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
  bool opt_n = false;
  bool opt_v = false;
  bool opt_l = false;
  int arg_idx = 1;
  while (arg_idx < argc && argv[arg_idx][0] == '-') {
    for (int j = 1; argv[arg_idx][j] != '\0'; j++) {
      if (argv[arg_idx][j] == 'n') opt_n = true;
      else if (argv[arg_idx][j] == 'v') opt_v = true;
      else if (argv[arg_idx][j] == 'l') opt_l = true;
      else {
        Tcl_AppendResult(interp, "grep: invalid option -- '", argv[arg_idx]+j, "'\n", NULL);
        return TCL_ERROR;
      }
    }
    arg_idx++;
  }
  
  if (argc - arg_idx < 2) {
    Tcl_SetResult(interp, (char*)"Usage: grep [-nvl] pattern file...", TCL_STATIC);
    return TCL_ERROR;
  }
  
  const char* pattern = argv[arg_idx++];
  
  regexp* prog = regcomp(const_cast<char*>(pattern));
  if (!prog) {
    Tcl_SetResult(interp, (char*)"grep: invalid regular expression", TCL_STATIC);
    return TCL_ERROR;
  }
  
  bool multiple_files = (argc - arg_idx > 1);
  bool any_error = false;
  
  for (; arg_idx < argc; arg_idx++) {
    const char* filename = argv[arg_idx];
    vfs_file_t file;
    int err = vfs_file_open(&file, filename, LFS_O_RDONLY);
    if (err < 0) {
      Tcl_AppendResult(interp, "grep: ", filename, ": No such file or directory\n", NULL);
      any_error = true;
      continue;
    }
    
    char line[512];
    int line_len = 0;
    int line_num = 1;
    char buf[64];
    
    while (true) {
      lfs_ssize_t n = vfs_file_read(&file, buf, sizeof(buf));
      if (n < 0) {
        Tcl_AppendResult(interp, "grep: error reading ", filename, "\n", NULL);
        any_error = true;
        break;
      }
      if (n == 0) {
        if (line_len > 0) {
          line[line_len] = '\0';
          int match = regexec(prog, line);
          bool matched = (match != 0);
          if (opt_v) matched = !matched;
          if (matched) {
            if (opt_l) {
              Tcl_AppendResult(interp, filename, "\n", NULL);
              break;
            }
            if (multiple_files) {
              Tcl_AppendResult(interp, filename, ":", NULL);
            }
            if (opt_n) {
              char numbuf[32];
              snprintf(numbuf, sizeof(numbuf), "%d:", line_num);
              Tcl_AppendResult(interp, numbuf, NULL);
            }
            Tcl_AppendResult(interp, line, "\n", NULL);
          }
        }
        break;
      }
      bool skip_file = false;
      for (lfs_ssize_t i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\n') {
          line[line_len] = '\0';
          int match = regexec(prog, line);
          bool matched = (match != 0);
          if (opt_v) matched = !matched;
          if (matched) {
            if (opt_l) {
              Tcl_AppendResult(interp, filename, "\n", NULL);
              skip_file = true;
              break;
            }
            if (multiple_files) {
              Tcl_AppendResult(interp, filename, ":", NULL);
            }
            if (opt_n) {
              char numbuf[32];
              snprintf(numbuf, sizeof(numbuf), "%d:", line_num);
              Tcl_AppendResult(interp, numbuf, NULL);
            }
            Tcl_AppendResult(interp, line, "\n", NULL);
          }
          line_len = 0;
          line_num++;
        } else if (c != '\r') {
          if (line_len < (int)sizeof(line) - 1) {
            line[line_len++] = c;
          }
        }
      }
      if (skip_file) break;
    }
    vfs_file_close(&file);
  }
  
  ckfree((char*)prog);
  
  return any_error ? TCL_ERROR : TCL_OK;
}

int glob_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc < 2) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                     " pattern ?pattern...?\"", NULL);
    return TCL_ERROR;
  }
  
  Tcl_ResetResult(interp);
  
  for (int i = 1; i < argc; i++) {
    std::vector<std::string> matches = glob(argv[i]);
    for (size_t j = 0; j < matches.size(); j++) {
      Tcl_AppendElement(interp, const_cast<char*>(matches[j].c_str()), 0);
    }
  }
  
  return TCL_OK;
}

int map_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
  int listArgc, i, result;
  char **listArgv;
  
  if (argc != 4) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
      " varName list command\"", (char *) NULL);
    return TCL_ERROR;
  }
  
  result = Tcl_SplitList(interp, argv[2], &listArgc, &listArgv);
  if (result != TCL_OK) {
    return result;
  }
  
  std::vector<std::string> results;
  
  for (i = 0; i < listArgc; i++) {
    if (Tcl_SetVar(interp, argv[1], listArgv[i], 0) == NULL) {
      Tcl_SetResult(interp, (char*)"couldn't set loop variable", TCL_STATIC);
      result = TCL_ERROR;
      break;
    }
    
    result = Tcl_Eval(interp, argv[3], 0, (char **) NULL);
    if (result != TCL_OK) {
      if (result == TCL_CONTINUE) {
        result = TCL_OK;
        results.push_back(interp->result ? interp->result : "");
        continue;
      } else if (result == TCL_BREAK) {
        result = TCL_OK;
        break;
      } else if (result == TCL_ERROR) {
        char msg[100];
        sprintf(msg, "\n    (\"map\" body line %d)", interp->errorLine);
        Tcl_AddErrorInfo(interp, msg);
        break;
      } else {
        break;
      }
    }
    results.push_back(interp->result ? interp->result : "");
  }
  
  ckfree((char *) listArgv);
  
  if (result == TCL_OK) {
    Tcl_ResetResult(interp);
    for (size_t j = 0; j < results.size(); j++) {
      Tcl_AppendElement(interp, const_cast<char*>(results[j].c_str()), 0);
    }
  }
  return result;
}

int puts_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc != 2 && argc != 3) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                     " s ?fd?\"", NULL);
    return TCL_ERROR;
  }
  
  const char* s = argv[1];
  const char* fd = (argc == 3) ? argv[2] : "tty";
  
  bool out_tether = false;
  bool out_coco = false;
  
  if (strcmp(fd, "tether") == 0) {
    out_tether = true;
  } else if (strcmp(fd, "coco") == 0) {
    out_coco = true;
  } else if (strcmp(fd, "tty") == 0) {
    out_tether = true;
    out_coco = true;
  } else {
    Tcl_AppendResult(interp, "bad fd: must be tether, coco, or tty", NULL);
    return TCL_ERROR;
  }
  
  for (const char* p = s; *p; p++) {
    if (out_tether) cobs_putchar(*p);
    if (out_coco) console::emit_char(*p);
  }
  if (out_tether) cobs_putchar('\n');
  if (out_coco) console::emit_char('\n');
  
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
  Tcl_CreateCommand(interp, (char*)"grep", grep_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"puts", puts_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"glob", glob_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"map", map_cmd, NULL, NULL);

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
