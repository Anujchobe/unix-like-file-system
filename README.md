# Unix-Like File System (FUSE-based)

A Unix-like file system implemented in **C** using **FUSE (Filesystem in Userspace)**.  
The file system supports core POSIX-style file and directory operations, inode-based
metadata management, bitmap-based block allocation, and persistent on-disk state.
Once mounted, it can be interacted with using standard Unix tools such as `ls`, `cat`,
`mkdir`, `touch`, and `rm`.

This project was developed as part of a **Computer Systems (CS5600)** course and focuses
on understanding file system design, storage abstractions, and correctness in
systems-level software.

---

## Features

- Fully mountable user-space file system via **FUSE**
- POSIX-style file and directory operations:
  - `read`, `write`, `create`, `unlink`
  - `mkdir`, `rmdir`, `rename`
  - `chmod`, `utime`, `truncate`
- Inode-based metadata representation
- Bitmap-managed block allocation
- Hierarchical directory structure with path traversal
- Correct handling of permissions, timestamps, and error codes
- Validated using comprehensive system-level unit tests

---

## File System Design Overview

### Disk Layout
The disk is abstracted as an array of **4KB blocks**:


### Core Data Structures

- **Superblock**
  - Stores filesystem metadata such as disk size and magic number
- **Block Bitmap**
  - Tracks free and allocated disk blocks
  - Supports up to 32,768 blocks (128MB max filesystem size)
- **Inodes**
  - Represent files and directories
  - Store ownership, permissions, timestamps, size, and block pointers
- **Directories**
  - Implemented as special files containing fixed-size directory entries
  - Map filenames to inode numbers

---

## Implemented Operations

### Part A – Read-Only and Metadata Operations
- `fs_init`
- `fs_statfs`
- `fs_getattr`
- `fs_readdir`
- `fs_read`
- `fs_rename`
- `fs_chmod`

### Part B – Mutating Operations
- `fs_create`
- `fs_mkdir`
- `fs_unlink`
- `fs_rmdir`
- `fs_write`
- `fs_truncate`
- `fs_utime`

All operations follow correct **POSIX error semantics** (`ENOENT`, `ENOTDIR`, `EEXIST`,
`EINVAL`, etc.) and maintain on-disk consistency.

---

## Testing

The file system was validated using **system-level unit tests** written with the
**Check** testing framework.

- All tests passed successfully:
  - Part A: metadata and read-only operations
  - Part B: file and directory mutation operations
- Disk images are regenerated automatically for isolated test execution
- Additional debugging performed using `gdb`, logging, and disk inspection utilities

---

## Building and Running

### Prerequisites
- Linux / Unix environment
- FUSE installed
- GCC / Clang toolchain
- Python 2.7 (for disk image utilities, if needed)

# Technologies Used

-C
-FUSE (Filesystem in Userspace)
-POSIX APIs
-Block-based storage abstractions
-Unit testing with Check

# Author

Anuj K Chobe
M.S. Computer Science, Northeastern University

GitHub: https://github.com/Anujchobe


