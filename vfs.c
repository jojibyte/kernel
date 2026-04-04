#include "vfs.h"
#include "heap.h"
#include "console.h"

static struct VfsNode *vfs_root = NULL;
static struct Filesystem *filesystems = NULL;
static struct MountPoint *mount_points = NULL;

static int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

static void strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n - 1 && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

struct RamfsEntry {
    struct VfsNode node;
    uint8_t *data;
    size_t capacity;
    struct RamfsEntry *children;
    struct RamfsEntry *next;
};

static ino_t ramfs_next_inode = 1;

static struct VfsNode *ramfs_open(struct VfsNode *dir, const char *name, int flags);
static void ramfs_close(struct VfsNode *node);
static ssize_t ramfs_read(struct VfsNode *node, void *buf, size_t size, off_t offset);
static ssize_t ramfs_write(struct VfsNode *node, const void *buf, size_t size, off_t offset);
static int ramfs_readdir(struct VfsNode *node, struct VfsDirent *entry, uint32_t index);
static struct VfsNode *ramfs_finddir(struct VfsNode *node, const char *name);
static int ramfs_create(struct VfsNode *dir, const char *name, mode_t mode);
static int ramfs_mkdir(struct VfsNode *dir, const char *name, mode_t mode);

static struct RamfsEntry *ramfs_create_entry(const char *name, uint32_t flags, mode_t mode) {
    struct RamfsEntry *entry = kzalloc(sizeof(struct RamfsEntry));
    if (!entry) return NULL;
    
    strncpy(entry->node.name, name, VFS_MAX_NAME);
    entry->node.flags = flags;
    entry->node.mode = mode;
    entry->node.inode = ramfs_next_inode++;
    entry->node.uid = 0;
    entry->node.gid = 0;
    entry->node.ref_count = 1;
    
    entry->node.open = ramfs_open;
    entry->node.close = ramfs_close;
    entry->node.read = ramfs_read;
    entry->node.write = ramfs_write;
    entry->node.readdir = ramfs_readdir;
    entry->node.finddir = ramfs_finddir;
    entry->node.create = ramfs_create;
    entry->node.mkdir = ramfs_mkdir;
    
    entry->node.fs_data = entry;
    
    return entry;
}

static struct VfsNode *ramfs_mount(const char *device, void *options) {
    (void)device;
    (void)options;
    
    struct RamfsEntry *root = ramfs_create_entry("", VFS_DIRECTORY, 0755);
    if (!root) return NULL;
    
    return &root->node;
}

static int ramfs_unmount(struct VfsNode *root) {
    (void)root;
    return 0;
}

static struct VfsNode *ramfs_open(struct VfsNode *dir, const char *name, int flags) {
    struct VfsNode *node = ramfs_finddir(dir, name);
    
    if (!node && (flags & O_CREAT)) {
        if (ramfs_create(dir, name, 0644) == 0) {
            node = ramfs_finddir(dir, name);
        }
    }
    
    if (node) {
        node->ref_count++;
    }
    
    return node;
}

static void ramfs_close(struct VfsNode *node) {
    if (node && node->ref_count > 0) {
        node->ref_count--;
    }
}

static ssize_t ramfs_read(struct VfsNode *node, void *buf, size_t size, off_t offset) {
    if (!node || !(node->flags & VFS_FILE)) return -EINVAL;
    
    struct RamfsEntry *entry = (struct RamfsEntry *)node->fs_data;
    if (!entry->data) return 0;
    
    if ((size_t)offset >= node->size) return 0;
    
    size_t to_read = size;
    if (offset + to_read > node->size) {
        to_read = node->size - offset;
    }
    
    uint8_t *src = entry->data + offset;
    uint8_t *dst = buf;
    for (size_t i = 0; i < to_read; i++) {
        dst[i] = src[i];
    }
    
    return to_read;
}

static ssize_t ramfs_write(struct VfsNode *node, const void *buf, size_t size, off_t offset) {
    if (!node || !(node->flags & VFS_FILE)) return -EINVAL;
    
    struct RamfsEntry *entry = (struct RamfsEntry *)node->fs_data;
    
    size_t needed = offset + size;
    if (needed > entry->capacity) {
        size_t new_cap = needed * 2;
        if (new_cap < 4096) new_cap = 4096;
        
        uint8_t *new_data = kmalloc(new_cap);
        if (!new_data) return -ENOMEM;
        
        if (entry->data) {
            uint8_t *src = entry->data;
            for (size_t i = 0; i < node->size; i++) {
                new_data[i] = src[i];
            }
            kfree(entry->data);
        }
        
        entry->data = new_data;
        entry->capacity = new_cap;
    }
    
    const uint8_t *src = buf;
    uint8_t *dst = entry->data + offset;
    for (size_t i = 0; i < size; i++) {
        dst[i] = src[i];
    }
    
    if (offset + size > node->size) {
        node->size = offset + size;
    }
    
    return size;
}

static int ramfs_readdir(struct VfsNode *node, struct VfsDirent *entry, uint32_t index) {
    if (!node || !(node->flags & VFS_DIRECTORY)) return -ENOTDIR;
    
    struct RamfsEntry *dir = (struct RamfsEntry *)node->fs_data;
    struct RamfsEntry *child = dir->children;
    
    uint32_t i = 0;
    while (child && i < index) {
        child = child->next;
        i++;
    }
    
    if (!child) return -1;
    
    entry->inode = child->node.inode;
    strncpy(entry->name, child->node.name, VFS_MAX_NAME);
    
    return 0;
}

static struct VfsNode *ramfs_finddir(struct VfsNode *node, const char *name) {
    if (!node || !(node->flags & VFS_DIRECTORY)) return NULL;
    
    struct RamfsEntry *dir = (struct RamfsEntry *)node->fs_data;
    struct RamfsEntry *child = dir->children;
    
    while (child) {
        if (strcmp(child->node.name, name) == 0) {
            return &child->node;
        }
        child = child->next;
    }
    
    return NULL;
}

static int ramfs_create(struct VfsNode *dir, const char *name, mode_t mode) {
    if (!dir || !(dir->flags & VFS_DIRECTORY)) return -ENOTDIR;
    
    if (ramfs_finddir(dir, name)) return -EEXIST;
    
    struct RamfsEntry *entry = ramfs_create_entry(name, VFS_FILE, mode);
    if (!entry) return -ENOMEM;
    
    struct RamfsEntry *parent = (struct RamfsEntry *)dir->fs_data;
    entry->next = parent->children;
    parent->children = entry;
    
    return 0;
}

static int ramfs_mkdir(struct VfsNode *dir, const char *name, mode_t mode) {
    if (!dir || !(dir->flags & VFS_DIRECTORY)) return -ENOTDIR;
    
    if (ramfs_finddir(dir, name)) return -EEXIST;
    
    struct RamfsEntry *entry = ramfs_create_entry(name, VFS_DIRECTORY, mode);
    if (!entry) return -ENOMEM;
    
    struct RamfsEntry *parent = (struct RamfsEntry *)dir->fs_data;
    entry->next = parent->children;
    parent->children = entry;
    
    return 0;
}

static struct Filesystem ramfs = {
    .name = "ramfs",
    .mount = ramfs_mount,
    .unmount = ramfs_unmount,
    .next = NULL
};

void vfs_init(void) {
    vfs_register_fs(&ramfs);
}

int vfs_register_fs(struct Filesystem *fs) {
    if (!fs) return -EINVAL;
    
    fs->next = filesystems;
    filesystems = fs;
    
    return 0;
}

int vfs_mount(const char *path, const char *fs_name, const char *device) {
    struct Filesystem *fs = filesystems;
    while (fs) {
        if (strcmp(fs->name, fs_name) == 0) break;
        fs = fs->next;
    }
    
    if (!fs) return -ENODEV;
    
    struct VfsNode *root = fs->mount(device, NULL);
    if (!root) return -EIO;
    
    struct MountPoint *mp = kzalloc(sizeof(struct MountPoint));
    if (!mp) {
        fs->unmount(root);
        return -ENOMEM;
    }
    
    strncpy(mp->path, path, VFS_MAX_PATH);
    mp->root = root;
    mp->fs = fs;
    
    mp->next = mount_points;
    mount_points = mp;
    
    if (strcmp(path, "/") == 0) {
        vfs_root = root;
    }
    
    return 0;
}

int vfs_unmount(const char *path) {
    struct MountPoint *prev = NULL;
    struct MountPoint *mp = mount_points;
    
    while (mp) {
        if (strcmp(mp->path, path) == 0) {
            if (prev) {
                prev->next = mp->next;
            } else {
                mount_points = mp->next;
            }
            
            mp->fs->unmount(mp->root);
            kfree(mp);
            return 0;
        }
        prev = mp;
        mp = mp->next;
    }
    
    return -EINVAL;
}

struct VfsNode *vfs_resolve_path(const char *path) {
    if (!path || path[0] != '/') return NULL;
    if (!vfs_root) return NULL;
    
    if (strcmp(path, "/") == 0) return vfs_root;
    
    struct VfsNode *node = vfs_root;
    char component[VFS_MAX_NAME];
    const char *p = path + 1;
    
    while (*p) {
        size_t len = 0;
        while (*p && *p != '/' && len < VFS_MAX_NAME - 1) {
            component[len++] = *p++;
        }
        component[len] = '\0';
        
        if (len == 0) {
            if (*p == '/') p++;
            continue;
        }
        
        node = vfs_finddir(node, component);
        if (!node) return NULL;
        
        if (*p == '/') p++;
    }
    
    return node;
}

struct VfsNode *vfs_open(const char *path, int flags) {
    struct VfsNode *parent = NULL;
    struct VfsNode *node = vfs_resolve_path(path);
    
    if (!node && (flags & O_CREAT)) {
        char parent_path[VFS_MAX_PATH];
        strncpy(parent_path, path, VFS_MAX_PATH);
        
        char *last_slash = parent_path;
        for (char *p = parent_path; *p; p++) {
            if (*p == '/') last_slash = p;
        }
        
        if (last_slash == parent_path) {
            parent = vfs_root;
        } else {
            *last_slash = '\0';
            parent = vfs_resolve_path(parent_path);
        }
        
        if (parent && parent->create) {
            const char *name = last_slash + 1;
            if (parent->create(parent, name, 0644) == 0) {
                node = vfs_resolve_path(path);
            }
        }
    }
    
    if (node && node->open) {
        node = node->open(node, "", flags);
    }
    
    return node;
}

void vfs_close(struct VfsNode *node) {
    if (node && node->close) {
        node->close(node);
    }
}

ssize_t vfs_read(struct VfsNode *node, void *buf, size_t size, off_t offset) {
    if (!node || !node->read) return -ENODEV;
    return node->read(node, buf, size, offset);
}

ssize_t vfs_write(struct VfsNode *node, const void *buf, size_t size, off_t offset) {
    if (!node || !node->write) return -ENODEV;
    return node->write(node, buf, size, offset);
}

int vfs_readdir(struct VfsNode *node, struct VfsDirent *entry, uint32_t index) {
    if (!node || !node->readdir) return -ENODEV;
    return node->readdir(node, entry, index);
}

struct VfsNode *vfs_finddir(struct VfsNode *node, const char *name) {
    if (!node || !node->finddir) return NULL;
    return node->finddir(node, name);
}

int vfs_mkdir(const char *path, mode_t mode) {
    char parent_path[VFS_MAX_PATH];
    strncpy(parent_path, path, VFS_MAX_PATH);
    
    char *last_slash = parent_path;
    for (char *p = parent_path; *p; p++) {
        if (*p == '/') last_slash = p;
    }
    
    const char *name;
    struct VfsNode *parent;
    
    if (last_slash == parent_path) {
        parent = vfs_root;
        name = path + 1;
    } else {
        *last_slash = '\0';
        parent = vfs_resolve_path(parent_path);
        name = last_slash + 1;
    }
    
    if (!parent || !parent->mkdir) return -ENODEV;
    return parent->mkdir(parent, name, mode);
}

int vfs_create(const char *path, mode_t mode) {
    struct VfsNode *node = vfs_open(path, O_CREAT | O_EXCL);
    if (node) {
        node->mode = mode;
        vfs_close(node);
        return 0;
    }
    return -EEXIST;
}

int vfs_unlink(const char *path) {
    (void)path;
    return -ENOSYS;
}
