#ifndef _VFS_H
#define _VFS_H

#include "types.h"

#define VFS_FILE        0x01
#define VFS_DIRECTORY   0x02
#define VFS_CHARDEVICE  0x03
#define VFS_BLOCKDEVICE 0x04
#define VFS_PIPE        0x05
#define VFS_SYMLINK     0x06
#define VFS_MOUNTPOINT  0x08

#define O_RDONLY        0x0000
#define O_WRONLY        0x0001
#define O_RDWR          0x0002
#define O_CREAT         0x0040
#define O_EXCL          0x0080
#define O_TRUNC         0x0200
#define O_APPEND        0x0400

#define SEEK_SET        0
#define SEEK_CUR        1
#define SEEK_END        2

#define VFS_MAX_NAME    256
#define VFS_MAX_PATH    4096

struct VfsNode;
struct VfsDirent;
struct VfsStat;

typedef struct VfsNode *(*VfsOpenFn)(struct VfsNode *, const char *, int);
typedef void (*VfsCloseFn)(struct VfsNode *);
typedef ssize_t (*VfsReadFn)(struct VfsNode *, void *, size_t, off_t);
typedef ssize_t (*VfsWriteFn)(struct VfsNode *, const void *, size_t, off_t);
typedef int (*VfsReaddirFn)(struct VfsNode *, struct VfsDirent *, uint32_t);
typedef struct VfsNode *(*VfsFinddirFn)(struct VfsNode *, const char *);
typedef int (*VfsCreateFn)(struct VfsNode *, const char *, mode_t);
typedef int (*VfsMkdirFn)(struct VfsNode *, const char *, mode_t);
typedef int (*VfsUnlinkFn)(struct VfsNode *, const char *);
typedef int (*VfsStatFn)(struct VfsNode *, struct VfsStat *);

struct VfsNode {
    char name[VFS_MAX_NAME];
    uint32_t flags;
    mode_t mode;
    uid_t uid;
    gid_t gid;
    ino_t inode;
    size_t size;
    time_t atime, mtime, ctime;
    
    VfsOpenFn open;
    VfsCloseFn close;
    VfsReadFn read;
    VfsWriteFn write;
    VfsReaddirFn readdir;
    VfsFinddirFn finddir;
    VfsCreateFn create;
    VfsMkdirFn mkdir;
    VfsUnlinkFn unlink;
    VfsStatFn stat;
    
    void *fs_data;
    struct VfsNode *ptr;
    uint32_t ref_count;
};

struct VfsDirent {
    ino_t inode;
    char name[VFS_MAX_NAME];
};

struct VfsStat {
    dev_t st_dev;
    ino_t st_ino;
    mode_t st_mode;
    uint32_t st_nlink;
    uid_t st_uid;
    gid_t st_gid;
    dev_t st_rdev;
    off_t st_size;
    time_t st_atime;
    time_t st_mtime;
    time_t st_ctime;
    blksize_t st_blksize;
    blkcnt_t st_blocks;
};

struct Filesystem {
    const char *name;
    struct VfsNode *(*mount)(const char *device, void *options);
    int (*unmount)(struct VfsNode *root);
    struct Filesystem *next;
};

struct MountPoint {
    char path[VFS_MAX_PATH];
    struct VfsNode *root;
    struct Filesystem *fs;
    struct MountPoint *next;
};

void vfs_init(void);
int vfs_register_fs(struct Filesystem *fs);
int vfs_mount(const char *path, const char *fs_name, const char *device);
int vfs_unmount(const char *path);

struct VfsNode *vfs_open(const char *path, int flags);
void vfs_close(struct VfsNode *node);
ssize_t vfs_read(struct VfsNode *node, void *buf, size_t size, off_t offset);
ssize_t vfs_write(struct VfsNode *node, const void *buf, size_t size, off_t offset);

int vfs_readdir(struct VfsNode *node, struct VfsDirent *entry, uint32_t index);
struct VfsNode *vfs_finddir(struct VfsNode *node, const char *name);
int vfs_mkdir(const char *path, mode_t mode);
int vfs_create(const char *path, mode_t mode);
int vfs_unlink(const char *path);

struct VfsNode *vfs_resolve_path(const char *path);
char *vfs_dirname(const char *path);
char *vfs_basename(const char *path);

#endif
