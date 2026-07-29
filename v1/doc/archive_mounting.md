# Transparent Archive Mounting Design Spec

## 1. Goal of Transparent Archive Mounting
The goal of Transparent Archive Mounting is to allow the Copico Centipede firmware to seamlessly explore, list, and read the contents of ZIP archives as if they were native directories in the Virtual File System (VFS). This capability allows resources bundled in ZIP files (such as `.rom` files, text files, or other assets) on the host PC or local flash to be read using standard file operations without requiring the entire archive to be loaded into memory or extracted manually.

## 2. Key Decisions
* **The `!` Suffix Convention:** To prevent confusion between treating an archive as a raw file versus treating it as a directory, we have adopted the Java-style `!` suffix convention (similar to the `jar:file:.../foo.jar!/` URI syntax).
  * `cal.zip` refers to the raw archive file itself. It can be read, copied, or stat'ed just like any other file.
  * `cal.zip!` refers to the root directory of the extracted archive. When the VFS encounters the `!` suffix, it mounts the archive and treats it as a directory for subsequent path traversal.
* **Heuristic File Type Detection:** Instead of relying strictly on file extensions (like `.zip`), we use a heuristic approach to sniff the file contents (e.g., checking for the `PK\3\4` magic header) to determine if a file is genuinely a zip archive.
* **Miniz Integration:** We use `miniz` to parse and extract the zip archive metadata on the fly, streaming file data efficiently.

## 3. Important Classes and Functions
* **`VfsNode` (Base Class):** The abstract interface representing any file or directory in the VFS hierarchy. All file systems must implement this interface.
* **`TetherFsNode` (Class):** Represents a file or directory hosted on the PC via the USB RPC tether. It forwards VFS requests to the PC.
* **`LittleFsNode` (Class):** Represents a file or directory stored locally on the Pico's flash memory using the LittleFS filesystem. Just like `TetherFsNode`, it implements the `VfsNode` interface and can be transparently wrapped by a `ZipArchiveNode`.
* **`ZipArchiveNode` (Class):** Represents a mounted archive (or a subdirectory inside one). It wraps an underlying `VfsNode` (like a `TetherFsNode` or `LittleFsNode`), managing the `mz_zip_archive` state and servicing read requests by interacting with the underlying file.
* **`vfs_resolve(path)` (Function):** The core path parsing function. It traverses the path string token by token. If it encounters a token ending in `!`, it strips the `!`, looks up the prefix file, verifies its type using `HeuristicFileType`, and dynamically wraps it in a `ZipArchiveNode`.
* **`HeuristicFileType(node)` (Function):** Opens a given file node, reads the first 4 bytes, and returns a string indicating its detected type (e.g., `"zip-archive"`).

## 4. OOP VFS Methods
The object-oriented `VfsNode` interface abstracts away the underlying storage backend (LittleFS, PC Tether, or Zip Archive). Key methods include:
* **Lookup:**
  * `lookup(const std::string& token)`: Resolves a single path token and returns a new `std::shared_ptr<VfsNode>` representing the child.
* **File Operations:**
  * `open_file(int flags)`: Opens a file for reading/writing.
  * `read(void* buffer, lfs_size_t size)`: Reads chunks of data from the open file.
  * `write(const void* buffer, lfs_size_t size)`: Writes chunks of data to the open file.
  * `seek(lfs_soff_t offset, int whence, Coro* self)`: Moves the read/write pointer.
  * `close_file(Coro* self)`: Closes the file descriptor.
* **Directory Operations:**
  * `open_dir()`: Prepares the node for directory enumeration.
  * `read_dir(struct vfs_info* info)`: Yields the next child entry in the directory.
  * `close_dir()`: Ends directory enumeration.
* **Metadata Operations:**
  * `stat(struct vfs_info* info)`: Fetches file size and type (`LFS_TYPE_REG` or `LFS_TYPE_DIR`).
  * `mkdir()` / `remove()`: Modifies the file system (if supported).

## 5. New RPCs to Tether and Changes to `pcb.h`
To support remote VFS interactions over the USB COBS tether, we added several new RPC methods and updated the `pcb::RpcRequest` / `pcb::RpcResponse` protobuf-like definitions:
* **File RPCs:** `"open"`, `"read"`, `"write"`, `"seek"`, `"close"`.
* **Directory RPCs:** `"dir_open"`, `"dir_read"`, `"dir_close"`.
* **Metadata RPCs:** `"stat"`, `"mkdir"`, `"remove"`.
* **Changes to `pcb.h`:** 
  * The `RpcRequest` struct was expanded to include `offset` and `whence` (for `"seek"`), `flags` (for `"open"`), `length` (for `"read"`), and `data` (for `"write"`).
  * The `RpcResponse` struct was expanded to return `handle` (for open file descriptors), `size` (for reads/stats), `is_dir` (for stats), and `data` (for payloads or directory names).

## 6. Recommended Tests
To ensure robustness, the following test cases should be performed in the Tcl console:
1. **Raw File Access:** `cat /pc/cal.zip` (Should dump binary zip data).
2. **Root Archive Listing:** `ls /pc/cal.zip!` (Should list the top-level contents of the zip file).
3. **Subdirectory Listing:** `ls /pc/cal.zip!/some_folder` (Should list the contents of a directory inside the archive).
4. **File Extraction / Reading:** `cat /pc/cal.zip!/hello.txt` (Should print the uncompressed text).
5. **Heuristic Failure Verification:** `ls /pc/regular_file.txt!` (Should fail gracefully if the target is not a valid zip archive).
6. **Concurrent VFS Usage:** Run a long command and verify that the `vfs_rpc_call` scheduling yields correctly to other coroutines.
7. **Nested Archives (Optional):** `ls /pc/cal.zip!/nested.zip!` (If supported, verify that an archive inside an archive can be mounted).

## 7. TODO

Based on the initial implementation, the following features remain incomplete and must be addressed:

1. **Implement Subdirectory Listing inside Archives (Test #3):**
   Currently, `ZipArchiveNode::open_dir()` fails if `sub_path` is not empty. It also strips the directory prefix (`last_slash`) from all files, flattening the entire zip archive into a single root directory. This needs to be refactored to support proper hierarchical tree traversal.
2. **Implement True Iterative Streaming for `open_file`/`read` (Test #4):**
   `ZipArchiveNode::open_file()` extracts the entire uncompressed file directly to the Pico's heap using `mz_zip_reader_extract_to_heap`. For files larger than the available free heap (~200KB), this causes an instant Out-Of-Memory (OOM) crash. We need to replace this with chunk-based streaming (e.g., using `mz_zip_reader_extract_iter_...`).
3. **Safe Support for Nested Archives (Test #7):**
   Because nested archives depend on `open_file`, trying to mount an archive inside another archive currently extracts the entire nested archive to the heap. Once iterative streaming is implemented for regular files, we need to ensure that nested archive reading correctly streams the parent's data on the fly.
