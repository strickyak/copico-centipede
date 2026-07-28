#ifndef FIRMWARE_PIO_TCL_COMMANDS_H_
#define FIRMWARE_PIO_TCL_COMMANDS_H_

#include <string.h>

#include "../tcl6.7c/tcl.h"
#include "flash-label.h"
#include "littlefs.h"
#include "heuristic_file.h"
#include "cobs_tx.h"
#include "console.h"
#include "../miniz/miniz.h"
extern "C" {
#include "../tcl6.7c/regexp.h"
}
#include "editor.h"


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
  bool opt_i = false;
  int arg_idx = 1;
  while (arg_idx < argc && argv[arg_idx][0] == '-') {
    for (int j = 1; argv[arg_idx][j] != '\0'; j++) {
      if (argv[arg_idx][j] == 'n') opt_n = true;
      else if (argv[arg_idx][j] == 'v') opt_v = true;
      else if (argv[arg_idx][j] == 'l') opt_l = true;
      else if (argv[arg_idx][j] == 'i') opt_i = true;
      else {
        Tcl_AppendResult(interp, "grep: invalid option -- '", argv[arg_idx]+j, "'\n", NULL);
        return TCL_ERROR;
      }
    }
    arg_idx++;
  }
  
  if (argc - arg_idx < 2) {
    Tcl_SetResult(interp, (char*)"Usage: grep [-nvli] pattern file...", TCL_STATIC);
    return TCL_ERROR;
  }
  
  const char* raw_pattern = argv[arg_idx++];
  char* pattern = (char*)ckalloc(strlen(raw_pattern) + 1);
  strcpy(pattern, raw_pattern);
  if (opt_i) {
    for (char* p = pattern; *p; p++) {
      if (isupper((unsigned char)*p)) *p = tolower((unsigned char)*p);
    }
  }
  
  regexp* prog = regcomp(pattern);
  ckfree(pattern);
  
  if (!prog) {
    Tcl_SetResult(interp, (char*)"grep: invalid regular expression", TCL_STATIC);
    return TCL_ERROR;
  }
  
  auto check_match = [&](char* l, int len) {
    if (!opt_i) {
      return regexec(prog, l) != 0;
    } else {
      std::string lower_line(l, len);
      for (int k = 0; k < len; k++) {
        lower_line[k] = tolower((unsigned char)lower_line[k]);
      }
      return regexec(prog, const_cast<char*>(lower_line.c_str())) != 0;
    }
  };
  
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
    
    std::string line;
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
        if (line.length() > 0) {
          bool matched = check_match(const_cast<char*>(line.c_str()), line.length());
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
            Tcl_AppendResult(interp, line.c_str(), "\n", NULL);
          }
        }
        break;
      }
      bool skip_file = false;
      for (lfs_ssize_t i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\n') {
          bool matched = check_match(const_cast<char*>(line.c_str()), line.length());
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
            Tcl_AppendResult(interp, line.c_str(), "\n", NULL);
          }
          line.clear();
          line_num++;
        } else if (c != '\r') {
          line += c;
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

int comb_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
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
        continue;
      } else if (result == TCL_BREAK) {
        result = TCL_OK;
        break;
      } else if (result == TCL_ERROR) {
        char msg[100];
        sprintf(msg, "\n    (\"comb\" body line %d)", interp->errorLine);
        Tcl_AddErrorInfo(interp, msg);
        break;
      } else {
        break;
      }
    }
    
    int is_true = 0;
    if (Tcl_GetBoolean(interp, interp->result, &is_true) != TCL_OK) {
      result = TCL_ERROR;
      break;
    }
    if (is_true) {
      results.push_back(listArgv[i]);
    }
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

int lzip_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc < 2) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
      " list1 ?list2...?\"", (char *) NULL);
    return TCL_ERROR;
  }
  
  int num_lists = argc - 1;
  std::vector<int> list_lengths(num_lists);
  std::vector<char**> list_elements(num_lists);
  
  int max_length = 0;
  for (int i = 0; i < num_lists; i++) {
    int list_argc;
    char **list_argv;
    int result = Tcl_SplitList(interp, argv[i + 1], &list_argc, &list_argv);
    if (result != TCL_OK) {
      for (int k = 0; k < i; k++) {
        ckfree((char*)list_elements[k]);
      }
      return result;
    }
    list_lengths[i] = list_argc;
    list_elements[i] = list_argv;
    if (list_argc > max_length) {
      max_length = list_argc;
    }
  }
  
  Tcl_ResetResult(interp);
  
  std::vector<char*> sub_argv(num_lists);
  char empty_str[] = "";
  
  for (int i = 0; i < max_length; i++) {
    for (int j = 0; j < num_lists; j++) {
      if (i < list_lengths[j]) {
        sub_argv[j] = list_elements[j][i];
      } else {
        sub_argv[j] = empty_str;
      }
    }
    char *merged = Tcl_Merge(num_lists, sub_argv.data());
    Tcl_AppendElement(interp, merged, 0);
    ckfree(merged);
  }
  
  for (int i = 0; i < num_lists; i++) {
    ckfree((char*)list_elements[i]);
  }
  
  return TCL_OK;
}

static size_t littlefs_zip_read_func(void *pOpaque, mz_uint64 file_ofs, void *pBuf, size_t n) {
  vfs_file_t* file = (vfs_file_t*)pOpaque;
  if (vfs_file_seek(file, file_ofs, LFS_SEEK_SET) < 0) {
    return 0;
  }
  lfs_ssize_t bytes_read = vfs_file_read(file, pBuf, n);
  if (bytes_read < 0) {
    return 0;
  }
  return bytes_read;
}

int minizip_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc < 3) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                     " subcommand zipname ?args...?\"", (char *) NULL);
    return TCL_ERROR;
  }
  
  const char* subcmd = argv[1];
  const char* zipname = argv[2];
  
  vfs_file_t file;
  int err = vfs_file_open(&file, zipname, LFS_O_RDONLY);
  if (err < 0) {
    Tcl_AppendResult(interp, "zip: cannot open ", zipname, (char *) NULL);
    return TCL_ERROR;
  }
  
  struct vfs_info info;
  if (vfs_stat(zipname, &info) < 0) {
    vfs_file_close(&file);
    Tcl_AppendResult(interp, "zip: cannot stat ", zipname, (char *) NULL);
    return TCL_ERROR;
  }
  
  mz_zip_archive zip;
  mz_zip_zero_struct(&zip);
  zip.m_pRead = littlefs_zip_read_func;
  zip.m_pIO_opaque = &file;
  
  if (!mz_zip_reader_init(&zip, info.size, 0)) {
    vfs_file_close(&file);
    Tcl_AppendResult(interp, "zip: failed to read zip archive", (char *) NULL);
    return TCL_ERROR;
  }
  
  int return_code = TCL_OK;
  
  if (strcmp(subcmd, "names") == 0) {
    Tcl_ResetResult(interp);
    mz_uint num_files = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < num_files; i++) {
      mz_zip_archive_file_stat file_stat;
      if (!mz_zip_reader_file_stat(&zip, i, &file_stat)) continue;
      Tcl_AppendElement(interp, file_stat.m_filename, 0);
    }
  } else if (strcmp(subcmd, "get") == 0) {
    if (argc != 4) {
      Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                       " get zipname membername\"", (char *) NULL);
      return_code = TCL_ERROR;
    } else {
      const char* membername = argv[3];
      size_t uncomp_size = 0;
      void* pBuf = mz_zip_reader_extract_file_to_heap(&zip, membername, &uncomp_size, 0);
      if (!pBuf) {
        Tcl_AppendResult(interp, "zip: failed to extract member ", membername, (char *) NULL);
        return_code = TCL_ERROR;
      } else {
        char* tcl_buf = (char*)ckalloc(uncomp_size + 1);
        memcpy(tcl_buf, pBuf, uncomp_size);
        tcl_buf[uncomp_size] = '\0';
        Tcl_SetResult(interp, tcl_buf, TCL_VOLATILE);
        ckfree(tcl_buf);
        mz_free(pBuf);
      }
    }
  } else {
    Tcl_AppendResult(interp, "bad option \"", subcmd, "\": must be names or get", (char *) NULL);
    return_code = TCL_ERROR;
  }
  
  mz_zip_reader_end(&zip);
  vfs_file_close(&file);
  
  return return_code;
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

static int k_cmd(ClientData clientData, Tcl_Interp* interp, int argc,
                 char** argv) {
  if (argc > 1) {
    Tcl_SetResult(interp, (char*)argv[1], TCL_VOLATILE);
  } else {
    Tcl_SetResult(interp, (char*)"", TCL_STATIC);
  }
  return TCL_OK;
}

static int iota_cmd(ClientData clientData, Tcl_Interp* interp, int argc,
                    char** argv) {
  if (argc != 2) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0], " n\"", (char*)NULL);
    return TCL_ERROR;
  }
  
  int n = atoi(argv[1]);
  if (n < 1) {
    return TCL_OK;
  }
  
  for (int i = 0; i < n; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", i);
    Tcl_AppendElement(interp, buf, 0);
  }
  
  return TCL_OK;
}

static int source_cmd(ClientData clientData, Tcl_Interp* interp, int argc,
                      char** argv) {
  if (argc != 2) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0], " fileName\"", (char*)NULL);
    return TCL_ERROR;
  }
  
  const char* filename = argv[1];
  
  struct vfs_info info;
  if (vfs_stat(filename, &info) < 0) {
    Tcl_AppendResult(interp, "couldn't read file \"", filename, "\": no such file or directory", (char*)NULL);
    return TCL_ERROR;
  }
  
  vfs_file_t file;
  int err = vfs_file_open(&file, filename, LFS_O_RDONLY);
  if (err < 0) {
    Tcl_AppendResult(interp, "couldn't read file \"", filename, "\": no such file or directory", (char*)NULL);
    return TCL_ERROR;
  }
  
  size_t size = info.size;
  char* buf = (char*)ckalloc(size + 1);
  
  lfs_ssize_t n = vfs_file_read(&file, buf, size);
  vfs_file_close(&file);
  
  if (n < 0) {
    ckfree(buf);
    Tcl_AppendResult(interp, "error reading file \"", filename, "\"", (char*)NULL);
    return TCL_ERROR;
  }
  
  buf[n] = '\0';
  
  int result = Tcl_Eval(interp, buf, 0, (char**)NULL);
  ckfree(buf);
  
  if (result == TCL_OK) {
    Tcl_SetResult(interp, (char*)"", TCL_STATIC);
  }
  
  return result;
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
  Tcl_CreateCommand(interp, (char*)"comb", comb_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"lzip", lzip_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"zip", minizip_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"k", k_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"iota", iota_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"source", source_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"edit", editor_cmd, NULL, NULL);

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
