#ifndef FIRMWARE_PIO_VFS_OOP_H_
#define FIRMWARE_PIO_VFS_OOP_H_

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>
#include <set>
#include <memory>

#include "cobs_tx.h"

extern "C" {
#include "../littlefs/lfs.h"
}

#include "../miniz/miniz.h"
#include "vfs_rpc.h"

extern lfs_t lfs_volume;
extern std::string vfs_cwd;

// ----------------------------------------------------------------------------
// OOP Nodes
// ----------------------------------------------------------------------------

struct vfs_info {
  int type;  // Matches LFS_TYPE_REG or LFS_TYPE_DIR
  char name[LFS_NAME_MAX + 1];
  lfs_size_t size;
};

class VfsNode : public std::enable_shared_from_this<VfsNode> {
public:
  virtual ~VfsNode() = default;

  virtual std::shared_ptr<VfsNode> lookup(const std::string& token) = 0;

  virtual int open_file(int flags) { return -1; }
  virtual lfs_ssize_t read(void* buffer, lfs_size_t size) { return -1; }
  virtual lfs_ssize_t write(const void* buffer, lfs_size_t size) { return -1; }
  virtual lfs_soff_t seek(lfs_soff_t offset, int whence, Coro* self) { return -1; }
  virtual int close_file(Coro* self) { return 0; }

  virtual int open_dir() { return -1; }
  virtual int read_dir(struct vfs_info* info) { return -1; }
  virtual int close_dir() { return 0; }

  virtual int stat(struct vfs_info* info) = 0;
  virtual std::string get_name() const = 0;
  
  virtual int remove() { return -1; }
  virtual int mkdir() { return -1; }
  
  // For ZipFS miniz callback (optional)
  virtual lfs_ssize_t _zip_read_internal(void* buffer, lfs_size_t size) { return read(buffer, size); }
  virtual lfs_soff_t _zip_seek_internal(lfs_soff_t offset, int whence) { return seek(offset, whence, nullptr); }
};

inline size_t littlefs_zip_read_func(void *pOpaque, mz_uint64 file_ofs, void *pBuf, size_t n) {
  VfsNode* node = (VfsNode*)pOpaque;
  if (node->_zip_seek_internal(file_ofs, LFS_SEEK_SET) < 0) {
    return 0;
  }
  lfs_ssize_t bytes_read = node->_zip_read_internal(pBuf, n);
  if (bytes_read < 0) {
    return 0;
  }
  return bytes_read;
}

struct ZipEntryInfo {
  std::string name;
  lfs_size_t size;
  int type;
};

class ZipArchiveNode : public VfsNode {
  std::shared_ptr<VfsNode> parent;
  std::string sub_path;
  mz_zip_archive zip;
  std::vector<ZipEntryInfo> entries;
  int dir_index = 0;
  
  mz_uint64 file_offset = 0;
  mz_uint64 uncomp_size = 0;
  void* file_data = nullptr;

public:
  ZipArchiveNode(std::shared_ptr<VfsNode> p, const std::string& sp) : parent(p), sub_path(sp) {}
  ~ZipArchiveNode() {
    if (file_data) free(file_data);
  }

  std::shared_ptr<VfsNode> lookup(const std::string& token) override {
    if (token.empty()) return shared_from_this();
    std::string new_path = sub_path.empty() ? token : sub_path + "/" + token;
    return std::make_shared<ZipArchiveNode>(parent, new_path);
  }

  int stat(struct vfs_info* info) override {
    if (sub_path.empty()) {
      info->type = LFS_TYPE_DIR;
      info->size = 0;
      snprintf(info->name, sizeof(info->name), "%s", parent->get_name().c_str());
      return 0;
    }
    
    if (parent->open_file(LFS_O_RDONLY) < 0) return -1;
    struct vfs_info pinfo;
    if (parent->stat(&pinfo) < 0) {
      parent->close_file(nullptr);
      return -1;
    }
    
    mz_zip_zero_struct(&zip);
    zip.m_pRead = littlefs_zip_read_func;
    zip.m_pIO_opaque = parent.get();
    
    if (!mz_zip_reader_init(&zip, pinfo.size, 0)) {
      parent->close_file(nullptr);
      return -1;
    }
    
    // 1. Try to locate as an exact file
    int mz_loc = mz_zip_reader_locate_file(&zip, sub_path.c_str(), nullptr, 0);
    if (mz_loc >= 0) {
      mz_zip_archive_file_stat file_stat;
      mz_zip_reader_file_stat(&zip, mz_loc, &file_stat);
      
      if (mz_zip_reader_is_file_a_directory(&zip, mz_loc) ||
          (file_stat.m_filename[0] != '\0' && file_stat.m_filename[strlen(file_stat.m_filename)-1] == '/')) {
        info->type = LFS_TYPE_DIR;
        info->size = 0;
      } else {
        info->type = LFS_TYPE_REG;
        info->size = file_stat.m_uncomp_size;
      }
      snprintf(info->name, sizeof(info->name), "%s", get_name().c_str());
      mz_zip_reader_end(&zip);
      parent->close_file(nullptr);
      return 0;
    }
    
    // 2. Try to locate as an exact directory with trailing slash
    mz_loc = mz_zip_reader_locate_file(&zip, (sub_path + "/").c_str(), nullptr, 0);
    if (mz_loc >= 0) {
      info->type = LFS_TYPE_DIR;
      info->size = 0;
      snprintf(info->name, sizeof(info->name), "%s", get_name().c_str());
      mz_zip_reader_end(&zip);
      parent->close_file(nullptr);
      return 0;
    }
    
    // 3. Infer virtual directory if any file starts with sub_path + "/"
    std::string prefix = sub_path + "/";
    mz_uint num_files = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < num_files; i++) {
      mz_zip_archive_file_stat file_stat;
      if (!mz_zip_reader_file_stat(&zip, i, &file_stat)) continue;
      std::string fname = file_stat.m_filename;
      if (fname.length() >= prefix.length() && fname.substr(0, prefix.length()) == prefix) {
        info->type = LFS_TYPE_DIR;
        info->size = 0;
        snprintf(info->name, sizeof(info->name), "%s", get_name().c_str());
        mz_zip_reader_end(&zip);
        parent->close_file(nullptr);
        return 0;
      }
    }
    
    mz_zip_reader_end(&zip);
    parent->close_file(nullptr);
    return -1;
  }

  int open_dir() override {
    if (parent->open_file(LFS_O_RDONLY) < 0) return -1;
    struct vfs_info pinfo;
    if (parent->stat(&pinfo) < 0) {
        parent->close_file(nullptr);
        return -1;
    }
    
    mz_zip_zero_struct(&zip);
    zip.m_pRead = littlefs_zip_read_func;
    zip.m_pIO_opaque = parent.get();
    
    if (!mz_zip_reader_init(&zip, pinfo.size, 0)) {
        parent->close_file(nullptr);
        return -1;
    }
    
    std::string prefix = sub_path.empty() ? "" : sub_path + "/";
    size_t prefix_len = prefix.length();
    std::set<std::string> seen;
    
    mz_uint num_files = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < num_files; i++) {
      mz_zip_archive_file_stat file_stat;
      if (!mz_zip_reader_file_stat(&zip, i, &file_stat)) continue;
      std::string fname = file_stat.m_filename;
      
      if (fname.length() >= prefix_len && fname.substr(0, prefix_len) == prefix) {
        std::string rel_path = fname.substr(prefix_len);
        if (rel_path.empty()) continue; // The directory itself
        
        size_t first_slash = rel_path.find('/');
        if (first_slash != std::string::npos) {
          // Subdirectory
          std::string dir_name = rel_path.substr(0, first_slash);
          if (seen.find(dir_name) == seen.end()) {
            seen.insert(dir_name);
            ZipEntryInfo e;
            e.name = dir_name;
            e.size = 0;
            e.type = LFS_TYPE_DIR;
            entries.push_back(e);
          }
        } else {
          // File (or empty-named directory if zip allows it)
          if (seen.find(rel_path) == seen.end()) {
            seen.insert(rel_path);
            ZipEntryInfo e;
            e.name = rel_path;
            e.size = file_stat.m_uncomp_size;
            e.type = LFS_TYPE_REG;
            entries.push_back(e);
          }
        }
      }
    }
    
    mz_zip_reader_end(&zip);
    parent->close_file(nullptr);
    dir_index = 0;
    return 0;
  }
  
  int read_dir(struct vfs_info* info) override {
    if (dir_index < (int)entries.size()) {
      info->type = entries[dir_index].type;
      info->size = entries[dir_index].size;
      snprintf(info->name, sizeof(info->name), "%s", entries[dir_index].name.c_str());
      dir_index++;
      return 1;
    }
    return 0;
  }
  
  int open_file(int flags) override {
    if (sub_path.empty()) return -1;
    if (parent->open_file(LFS_O_RDONLY) < 0) return -1;
    struct vfs_info pinfo;
    if (parent->stat(&pinfo) < 0) {
        parent->close_file(nullptr);
        return -1;
    }
    
    mz_zip_zero_struct(&zip);
    zip.m_pRead = littlefs_zip_read_func;
    zip.m_pIO_opaque = parent.get();
    
    if (!mz_zip_reader_init(&zip, pinfo.size, 0)) {
        parent->close_file(nullptr);
        return -1;
    }
    
    int mz_loc = mz_zip_reader_locate_file(&zip, sub_path.c_str(), nullptr, 0);
    if (mz_loc < 0) {
      mz_zip_reader_end(&zip);
      parent->close_file(nullptr);
      return -1;
    }
    
    mz_zip_archive_file_stat file_stat;
    mz_zip_reader_file_stat(&zip, mz_loc, &file_stat);
    uncomp_size = file_stat.m_uncomp_size;
    file_data = mz_zip_reader_extract_to_heap(&zip, mz_loc, (size_t*)&uncomp_size, 0);
    
    mz_zip_reader_end(&zip);
    parent->close_file(nullptr);
    
    if (!file_data) return -1;
    file_offset = 0;
    return 0;
  }
  
  lfs_ssize_t read(void* buffer, lfs_size_t size) override {
    if (!file_data) return -1;
    if (file_offset >= uncomp_size) return 0;
    lfs_size_t read_size = size;
    if (file_offset + read_size > uncomp_size) {
      read_size = uncomp_size - file_offset;
    }
    memcpy(buffer, (uint8_t*)file_data + file_offset, read_size);
    file_offset += read_size;
    return read_size;
  }
  
  lfs_soff_t seek(lfs_soff_t offset, int whence, Coro* self) override {
    if (!file_data) return -1;
    if (whence == LFS_SEEK_SET) file_offset = offset;
    else if (whence == LFS_SEEK_CUR) file_offset += offset;
    else if (whence == LFS_SEEK_END) file_offset = uncomp_size + offset;
    if (file_offset > uncomp_size) file_offset = uncomp_size;
    return file_offset;
  }
  
  int close_file(Coro* self) override {
    if (file_data) {
      free(file_data);
      file_data = nullptr;
    }
    return 0;
  }
  
  std::string get_name() const override {
    if (sub_path.empty()) return parent->get_name();
    size_t last_slash = sub_path.find_last_of('/');
    if (last_slash == std::string::npos) return sub_path;
    return sub_path.substr(last_slash + 1);
  }
};

class TetherFsNode : public VfsNode {
  std::string path;
  int fd = -1;
  int dir_fd = -1;
public:
  TetherFsNode(const std::string& p) : path(p) {}
  
  std::shared_ptr<VfsNode> lookup(const std::string& token) override {
    if (token.empty()) {
      return shared_from_this();
    }
    std::string new_path = path.empty() ? token : path + "/" + token;
    return std::make_shared<TetherFsNode>(new_path);
  }

  int open_file(int flags) override {
    pcb::RpcRequest req;
    req.method = "open";
    req.path = path;
    req.flags = flags;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req);
    if (resp.status != 0) return -1;
    fd = resp.handle;
    return 0;
  }
  
  lfs_ssize_t read(void* buffer, lfs_size_t size) override {
    pcb::RpcRequest req;
    req.method = "read";
    req.handle = fd;
    req.length = size;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req);
    if (resp.status != 0) return -1;
    lfs_size_t read_size = resp.data.size();
    if (read_size > size) read_size = size;
    if (read_size > 0) memcpy(buffer, resp.data.data(), read_size);
    return read_size;
  }
  
  lfs_ssize_t write(const void* buffer, lfs_size_t size) override {
    pcb::RpcRequest req;
    req.method = "write";
    req.handle = fd;
    req.data.assign(static_cast<const char*>(buffer), size);
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req);
    if (resp.status != 0) return -1;
    return size;
  }
  
  lfs_soff_t seek(lfs_soff_t offset, int whence, Coro* self) override {
    pcb::RpcRequest req;
    req.method = "seek";
    req.handle = fd;
    req.offset = offset;
    req.whence = whence;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req, self);
    if (resp.status != 0) return -1;
    return resp.size;
  }
  
  int close_file(Coro* self) override {
    pcb::RpcRequest req;
    req.method = "close";
    req.handle = fd;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req, self);
    if (resp.status != 0) return -1;
    return 0;
  }
  
  int open_dir() override {
    pcb::RpcRequest req;
    req.method = "dir_open";
    req.path = path;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req);
    if (resp.status != 0) return -1;
    dir_fd = resp.handle;
    return 0;
  }
  
  int read_dir(struct vfs_info* info) override {
    pcb::RpcRequest req;
    req.method = "dir_read";
    req.handle = dir_fd;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req);
    if (resp.status != 0) return -1;
    if (resp.data.empty()) return 0;
    info->type = resp.is_dir ? LFS_TYPE_DIR : LFS_TYPE_REG;
    info->size = resp.size;
    snprintf(info->name, sizeof(info->name), "%s", resp.data.c_str());
    return 1;
  }
  
  int close_dir() override {
    pcb::RpcRequest req;
    req.method = "dir_close";
    req.handle = dir_fd;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req);
    if (resp.status != 0) return -1;
    return 0;
  }
  
  int stat(struct vfs_info* info) override {
    pcb::RpcRequest req;
    req.method = "stat";
    req.path = path;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req);
    if (resp.status == 0) {
      info->type = resp.is_dir ? LFS_TYPE_DIR : LFS_TYPE_REG;
      info->size = resp.size;
      snprintf(info->name, sizeof(info->name), "%s", resp.data.c_str());
      return 0;
    }
    return -1;
  }
  
  std::string get_name() const override {
    if (path.empty()) return "pc";
    size_t last_slash = path.find_last_of('/');
    if (last_slash == std::string::npos) return path;
    return path.substr(last_slash + 1);
  }
  
  int mkdir() override {
    pcb::RpcRequest req; req.method = "mkdir"; req.path = path;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req);
    return resp.status != 0 ? -1 : 0;
  }
  int remove() override {
    pcb::RpcRequest req; req.method = "remove"; req.path = path;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req);
    return resp.status != 0 ? -1 : 0;
  }
};

class LittleFsNode : public VfsNode {
  std::string path;
  lfs_file_t lfs_file;
  lfs_dir_t lfs_dir;
  bool virtual_pc_returned = false;
public:
  LittleFsNode(const std::string& p) : path(p) {}
  
  std::shared_ptr<VfsNode> lookup(const std::string& token) override {
    if (path.empty() && token == "pc") {
      return std::make_shared<TetherFsNode>("");
    }
    
    if (token.empty()) {
      return shared_from_this();
    }
    
    std::string new_path = path.empty() ? token : path + "/" + token;
    return std::make_shared<LittleFsNode>(new_path);
  }

  int open_file(int flags) override {
    std::string p = path.empty() ? "/" : path;
    return lfs_file_open(&lfs_volume, &lfs_file, p.c_str(), flags);
  }
  lfs_ssize_t read(void* buffer, lfs_size_t size) override {
    return lfs_file_read(&lfs_volume, &lfs_file, buffer, size);
  }
  lfs_ssize_t write(const void* buffer, lfs_size_t size) override {
    return lfs_file_write(&lfs_volume, &lfs_file, buffer, size);
  }
  lfs_soff_t seek(lfs_soff_t offset, int whence, Coro* self) override {
    return lfs_file_seek(&lfs_volume, &lfs_file, offset, whence);
  }
  int close_file(Coro* self) override {
    return lfs_file_close(&lfs_volume, &lfs_file);
  }
  
  int open_dir() override {
    virtual_pc_returned = false;
    std::string p = path.empty() ? "/" : path;
    return lfs_dir_open(&lfs_volume, &lfs_dir, p.c_str());
  }
  int read_dir(struct vfs_info* info) override {
    struct lfs_info lfs_i;
    int res = lfs_dir_read(&lfs_volume, &lfs_dir, &lfs_i);
    if (res > 0) {
      info->type = lfs_i.type;
      info->size = lfs_i.size;
      snprintf(info->name, sizeof(info->name), "%s", lfs_i.name);
      if (path.empty() && strcmp(info->name, "pc") == 0) {
        virtual_pc_returned = true;
      }
      return res;
    } else if (res == 0) {
      if (path.empty() && !virtual_pc_returned) {
        virtual_pc_returned = true;
        info->type = LFS_TYPE_DIR;
        info->size = 0;
        snprintf(info->name, sizeof(info->name), "pc");
        return 1;
      }
    }
    return res;
  }
  int close_dir() override {
    return lfs_dir_close(&lfs_volume, &lfs_dir);
  }
  
  int stat(struct vfs_info* info) override {
    if (path.empty()) {
        info->type = LFS_TYPE_DIR;
        info->size = 0;
        snprintf(info->name, sizeof(info->name), "/");
        return 0;
    }
    struct lfs_info lfs_i;
    int res = lfs_stat(&lfs_volume, path.c_str(), &lfs_i);
    if (res >= 0) {
      info->type = lfs_i.type;
      info->size = lfs_i.size;
      snprintf(info->name, sizeof(info->name), "%s", lfs_i.name);
    }
    return res;
  }
  
  std::string get_name() const override {
    size_t last_slash = path.find_last_of('/');
    if (last_slash == std::string::npos) return path;
    return path.substr(last_slash + 1);
  }
  
  int mkdir() override {
    if (path.empty()) return -1;
    return lfs_mkdir(&lfs_volume, path.c_str());
  }
  int remove() override {
    if (path.empty()) return -1;
    return lfs_remove(&lfs_volume, path.c_str());
  }
};

class HexFileNode : public VfsNode {
  std::shared_ptr<VfsNode> parent;
  lfs_soff_t hex_offset = 0;
public:
  HexFileNode(std::shared_ptr<VfsNode> p) : parent(p) {}

  std::shared_ptr<VfsNode> lookup(const std::string& token) override {
    if (!token.empty()) return nullptr;
    return shared_from_this();
  }

  int open_file(int flags) override {
    hex_offset = 0;
    return parent->open_file(flags);
  }

  int close_file(Coro* self) override {
    return parent->close_file(self);
  }

  lfs_ssize_t read(void* buffer, lfs_size_t size) override {
    if (size % 2 != 0 || hex_offset % 2 != 0) {
      cobs_printf("HexFileNode::read ERROR: odd size=%u or hex_offset=%u\n", (unsigned)size, (unsigned)hex_offset);
      return -1;
    }
    lfs_size_t half_size = size / 2;
    std::vector<uint8_t> bin_buf(half_size);
    lfs_ssize_t n = parent->read(bin_buf.data(), half_size);
    if (n < 0) return n;
    
    char* out = (char*)buffer;
    for (lfs_ssize_t i = 0; i < n; i++) {
      sprintf(out + (i * 2), "%02X", bin_buf[i]);
    }
    hex_offset += n * 2;
    return n * 2;
  }

  lfs_ssize_t write(const void* buffer, lfs_size_t size) override {
    const char* in = (const char*)buffer;
    lfs_size_t actual_size = size;
    while (actual_size > 0 && (in[actual_size - 1] == '\n' || in[actual_size - 1] == '\r')) {
      actual_size--;
    }
    
    if (actual_size % 2 != 0 || hex_offset % 2 != 0) {
      cobs_printf("HexFileNode::write ERROR: odd actual_size=%u or hex_offset=%u\n", (unsigned)actual_size, (unsigned)hex_offset);
      return -1;
    }
    lfs_size_t half_size = actual_size / 2;
    std::vector<uint8_t> bin_buf(half_size);
    
    for (lfs_size_t i = 0; i < half_size; i++) {
      char hex[3] = { in[i*2], in[i*2+1], 0 };
      char* end;
      long val = strtol(hex, &end, 16);
      if (end != hex + 2) {
        cobs_printf("HexFileNode::write ERROR: invalid hex '%s'\n", hex);
        return -1;
      }
      bin_buf[i] = (uint8_t)val;
    }
    
    lfs_ssize_t n = 0;
    if (half_size > 0) {
      n = parent->write(bin_buf.data(), half_size);
      if (n < 0) {
        cobs_printf("HexFileNode::write ERROR: parent write failed with %d\n", (int)n);
        return n;
      }
    }
    hex_offset += n * 2;
    return size; // Return original size so caller doesn't think it was a short write
  }

  lfs_soff_t seek(lfs_soff_t offset, int whence, Coro* self) override {
    lfs_soff_t new_offset = hex_offset;
    if (whence == LFS_SEEK_SET) new_offset = offset;
    else if (whence == LFS_SEEK_CUR) new_offset += offset;
    else if (whence == LFS_SEEK_END) {
      struct vfs_info info;
      if (parent->stat(&info) == 0) {
        new_offset = (info.size * 2) + offset;
      } else {
        cobs_printf("HexFileNode::seek ERROR: parent stat failed\n");
        return -1;
      }
    }
    if (new_offset % 2 != 0) {
      cobs_printf("HexFileNode::seek ERROR: odd new_offset=%d\n", (int)new_offset);
      return -1;
    }
    
    lfs_soff_t p_offset = parent->seek(new_offset / 2, LFS_SEEK_SET, self);
    if (p_offset < 0) {
      cobs_printf("HexFileNode::seek ERROR: parent seek failed with %d\n", (int)p_offset);
      return p_offset;
    }
    hex_offset = p_offset * 2;
    return hex_offset;
  }

  int stat(struct vfs_info* info) override {
    int res = parent->stat(info);
    if (res >= 0 && info->type == LFS_TYPE_REG) {
      info->size *= 2;
      std::string new_name = parent->get_name() + "!hex";
      snprintf(info->name, sizeof(info->name), "%s", new_name.c_str());
    } else if (res >= 0 && info->type == LFS_TYPE_DIR) {
      return -1;
    }
    return res;
  }

  std::string get_name() const override {
    return parent->get_name() + "!hex";
  }

  int remove() override { return parent->remove(); }
};

class CrFileNode : public VfsNode {
  std::shared_ptr<VfsNode> parent;
public:
  CrFileNode(std::shared_ptr<VfsNode> p) : parent(p) {}

  std::shared_ptr<VfsNode> lookup(const std::string& token) override {
    if (!token.empty()) return nullptr;
    return shared_from_this();
  }

  int open_file(int flags) override {
    return parent->open_file(flags);
  }

  int close_file(Coro* self) override {
    return parent->close_file(self);
  }

  lfs_ssize_t read(void* buffer, lfs_size_t size) override {
    lfs_ssize_t n = parent->read(buffer, size);
    if (n > 0) {
      char* buf = (char*)buffer;
      for (lfs_ssize_t i = 0; i < n; i++) {
        if (buf[i] == '\n') buf[i] = '\r';
      }
    }
    return n;
  }

  lfs_ssize_t write(const void* buffer, lfs_size_t size) override {
    std::vector<char> tmp(size);
    const char* in = (const char*)buffer;
    for (lfs_size_t i = 0; i < size; i++) {
      tmp[i] = (in[i] == '\r') ? '\n' : in[i];
    }
    return parent->write(tmp.data(), size);
  }

  lfs_soff_t seek(lfs_soff_t offset, int whence, Coro* self) override {
    return parent->seek(offset, whence, self);
  }

  int stat(struct vfs_info* info) override {
    int res = parent->stat(info);
    if (res >= 0 && info->type == LFS_TYPE_REG) {
      std::string new_name = parent->get_name() + "!cr";
      snprintf(info->name, sizeof(info->name), "%s", new_name.c_str());
    } else if (res >= 0 && info->type == LFS_TYPE_DIR) {
      return -1;
    }
    return res;
  }

  std::string get_name() const override {
    return parent->get_name() + "!cr";
  }

  int remove() override { return parent->remove(); }
};

// ----------------------------------------------------------------------------
// Path Resolution
// ----------------------------------------------------------------------------

inline std::string vfs_normalize_path(const std::string& path) {
  std::string full_path = path;
  if (full_path.empty()) full_path = ".";
  if (full_path[0] != '/') {
    full_path = vfs_cwd + "/" + full_path;
  }

  std::vector<std::string> parts;
  size_t i = 0;
  while (i < full_path.length()) {
    size_t next = full_path.find('/', i);
    std::string part;
    if (next == std::string::npos) {
      part = full_path.substr(i);
      i = full_path.length();
    } else {
      part = full_path.substr(i, next - i);
      i = next + 1;
    }

    if (part == "" || part == ".") {
      continue;
    } else if (part == "..") {
      if (!parts.empty()) parts.pop_back();
    } else {
      parts.push_back(part);
    }
  }

  std::string resolved = "";
  for (const auto& p : parts) {
    resolved += "/" + p;
  }
  if (resolved.empty()) resolved = "/";
  return resolved;
}

const char* HeuristicFileType(std::shared_ptr<VfsNode> node);

inline std::shared_ptr<VfsNode> vfs_resolve(const std::string& path_str) {
  std::string path = path_str;
  if (!path.empty() && path.back() == '/') {
    path.pop_back();
  }
  
  std::string full_path = path;
  if (full_path.empty()) full_path = ".";
  if (full_path[0] != '/') {
    full_path = vfs_cwd + "/" + full_path;
  }

  std::vector<std::string> parts;
  size_t i = 0;
  while (i < full_path.length()) {
    size_t next = full_path.find('/', i);
    std::string part;
    if (next == std::string::npos) {
      part = full_path.substr(i);
      i = full_path.length();
    } else {
      part = full_path.substr(i, next - i);
      i = next + 1;
    }

    if (part == "" || part == ".") {
      continue;
    } else if (part == "..") {
      if (!parts.empty() && parts.back() != "") parts.pop_back();
    } else {
      parts.push_back(part);
    }
  }

  if (full_path.length() > 1 && full_path.back() == '/') {
    parts.push_back("");
  } else if (full_path == "/") {
    parts.push_back("");
  }

  std::shared_ptr<VfsNode> curr = std::make_shared<LittleFsNode>("");
  for (const auto& token : parts) {
    if (token.empty() && token != parts.back()) continue;
    
    if (!token.empty() && token.length() > 4 && token.substr(token.length() - 4) == "!hex") {
      std::string real_name = token.substr(0, token.length() - 4);
      auto file_node = curr->lookup(real_name);
      if (file_node) {
        curr = std::make_shared<HexFileNode>(file_node);
        continue;
      }
    } else if (!token.empty() && token.length() > 3 && token.substr(token.length() - 3) == "!cr") {
      std::string real_name = token.substr(0, token.length() - 3);
      auto file_node = curr->lookup(real_name);
      if (file_node) {
        curr = std::make_shared<CrFileNode>(file_node);
        continue;
      }
    } else if (!token.empty() && token.back() == '!') {
      std::string real_name = token.substr(0, token.length() - 1);
      auto file_node = curr->lookup(real_name);
      if (file_node) {
        std::string type = HeuristicFileType(file_node);
        if (type == "zip archive") {
          curr = std::make_shared<ZipArchiveNode>(file_node, "");
          continue;
        }
      }
    }
    
    curr = curr->lookup(token);
    if (!curr) break;
  }
  return curr;
}

// ----------------------------------------------------------------------------
// C API Adapters
// ----------------------------------------------------------------------------

struct vfs_file_t {
  std::shared_ptr<VfsNode> node;
};

struct vfs_dir_t {
  std::shared_ptr<VfsNode> node;
};

inline int vfs_file_open(vfs_file_t* file, const std::string& path, int flags) {
  file->node = vfs_resolve(path);
  if (!file->node) return -1;
  return file->node->open_file(flags);
}

inline lfs_ssize_t vfs_file_read(vfs_file_t* file, void* buffer, lfs_size_t size) {
  if (!file->node) return -1;
  return file->node->read(buffer, size);
}

inline lfs_ssize_t vfs_file_write(vfs_file_t* file, const void* buffer, lfs_size_t size) {
  if (!file->node) return -1;
  return file->node->write(buffer, size);
}

inline lfs_soff_t vfs_file_seek(vfs_file_t* file, lfs_soff_t offset, int whence, Coro* self = nullptr) {
  if (!file->node) return -1;
  return file->node->seek(offset, whence, self);
}

inline int vfs_file_close(vfs_file_t* file, Coro* self = nullptr) {
  if (!file->node) return -1;
  int err = file->node->close_file(self);
  file->node.reset();
  return err;
}

inline int vfs_dir_open(vfs_dir_t* dir, const std::string& path) {
  std::string dir_path = path;
  if (!dir_path.empty() && dir_path.back() != '/') {
    dir_path += "/";
  }
  dir->node = vfs_resolve(dir_path);
  if (!dir->node) return -1;
  return dir->node->open_dir();
}

inline int vfs_dir_read(vfs_dir_t* dir, struct vfs_info* info) {
  if (!dir->node) return -1;
  return dir->node->read_dir(info);
}

inline int vfs_dir_close(vfs_dir_t* dir) {
  if (!dir->node) return -1;
  int err = dir->node->close_dir();
  dir->node.reset();
  return err;
}

inline int vfs_stat(const std::string& path, struct vfs_info* info) {
  std::shared_ptr<VfsNode> node = vfs_resolve(path);
  if (!node) return -1;
  return node->stat(info);
}

inline int vfs_mkdir(const std::string& path) {
  std::shared_ptr<VfsNode> node = vfs_resolve(path);
  if (!node) return -1;
  return node->mkdir();
}

inline int vfs_remove(const std::string& path) {
  std::shared_ptr<VfsNode> node = vfs_resolve(path);
  if (!node) return -1;
  return node->remove();
}

// ----------------------------------------------------------------------------
// Globbing implementation (unchanged logic)
// ----------------------------------------------------------------------------

inline bool match_pattern(const char* pattern, const char* str) {
  while (*pattern && *str) {
    if (*pattern == '*') {
      while (*pattern == '*') pattern++;
      if (!*pattern) return true;
      while (*str) {
        if (match_pattern(pattern, str)) return true;
        str++;
      }
      return false;
    } else if (*pattern == '?') {
      pattern++;
      str++;
    } else if (*pattern == '[') {
      pattern++;
      bool invert = false;
      if (*pattern == '!' || *pattern == '^') {
        invert = true;
        pattern++;
      }
      bool matched = false;
      while (*pattern && *pattern != ']') {
        if (pattern[1] == '-' && pattern[2] && pattern[2] != ']') {
          if (*str >= pattern[0] && *str <= pattern[2]) matched = true;
          pattern += 3;
        } else {
          if (*str == *pattern) matched = true;
          pattern++;
        }
      }
      if (*pattern == ']') pattern++;
      if (matched == invert) return false;
      str++;
    } else {
      if (*pattern != *str) return false;
      pattern++;
      str++;
    }
  }
  while (*pattern == '*') pattern++;
  return *pattern == '\0' && *str == '\0';
}

inline void glob_recursive(const std::string& current_path,
                           const std::string& remaining_pattern,
                           std::vector<std::string>& results) {
  if (remaining_pattern.empty()) {
    struct vfs_info info;
    std::string scan_path = current_path.empty() ? "." : current_path;
    if (vfs_stat(scan_path, &info) >= 0) {
      results.push_back(scan_path);
    }
    return;
  }

  size_t slash = remaining_pattern.find('/');
  std::string comp = remaining_pattern.substr(0, slash);
  std::string next_rem =
      (slash == std::string::npos) ? "" : remaining_pattern.substr(slash + 1);
  if (next_rem.empty() && slash != std::string::npos && !comp.empty()) {
    next_rem = "/";
  }

  bool has_wildcard = (comp.find('*') != std::string::npos ||
                       comp.find('?') != std::string::npos ||
                       comp.find('[') != std::string::npos);

  if (!has_wildcard) {
    std::string next_path;
    if (current_path.empty()) {
      next_path = comp;
      if (slash == 0) next_path = "/";
    } else {
      if (current_path.back() == '/') {
        next_path = current_path + comp;
      } else {
        next_path = current_path + "/" + comp;
      }
    }

    if (comp == "" && slash != std::string::npos) {
      glob_recursive(next_path, next_rem, results);
      return;
    }

    glob_recursive(next_path, next_rem, results);
    return;
  }

  std::string scan_path = current_path.empty() ? "." : current_path;

  vfs_dir_t dir;
  if (vfs_dir_open(&dir, scan_path) < 0) {
    return;
  }

  struct vfs_info info;
  while (vfs_dir_read(&dir, &info) > 0) {
    if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) continue;

    if (match_pattern(comp.c_str(), info.name)) {
      std::string next_path;
      if (current_path.empty()) {
        next_path = info.name;
      } else {
        if (current_path.back() == '/') {
          next_path = current_path + info.name;
        } else {
          next_path = current_path + "/" + info.name;
        }
      }

      if (!next_rem.empty()) {
        if (info.type != LFS_TYPE_DIR) continue;
      }

      glob_recursive(next_path, next_rem, results);
    }
  }
  vfs_dir_close(&dir);
}

inline std::vector<std::string> glob(const std::string& pattern) {
  std::vector<std::string> results;

  if (pattern.empty()) {
    results.push_back("");
    return results;
  }

  std::string current_path = "";
  std::string remaining_pattern = pattern;

  if (pattern[0] == '/') {
    current_path = "/";
    remaining_pattern = pattern.substr(1);
  }

  glob_recursive(current_path, remaining_pattern, results);
  return results;
}

#endif  // FIRMWARE_PIO_VFS_OOP_H_
