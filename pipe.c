#include "pipe.h"
#include "heap.h"
#include "process.h"
#include "kstring.h"

struct Pipe *pipe_create(void) {
    struct Pipe *pipe = kzalloc(sizeof(struct Pipe));
    if (!pipe) return NULL;

    pipe->read_pos = 0;
    pipe->write_pos = 0;
    pipe->count = 0;
    pipe->readers = 1;
    pipe->writers = 1;
    pipe->closed_read = false;
    pipe->closed_write = false;

    return pipe;
}

void pipe_destroy(struct Pipe *pipe) {
    if (!pipe) return;
    kfree(pipe);
}

ssize_t pipe_read(struct Pipe *pipe, void *buf, size_t count) {
    if (!pipe || !buf) return -EFAULT;

    if (pipe->count == 0) {
        if (pipe->closed_write)
            return 0;
        return -EAGAIN;
    }

    size_t to_read = count < pipe->count ? count : pipe->count;
    uint8_t *dst = buf;

    for (size_t i = 0; i < to_read; i++) {
        dst[i] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) % PIPE_BUF_SIZE;
    }

    pipe->count -= to_read;
    return (ssize_t)to_read;
}

ssize_t pipe_write(struct Pipe *pipe, const void *buf, size_t count) {
    if (!pipe || !buf) return -EFAULT;

    if (pipe->closed_read)
        return -EPIPE;

    size_t available = PIPE_BUF_SIZE - pipe->count;
    size_t to_write = count < available ? count : available;

    if (to_write == 0)
        return -EAGAIN;

    const uint8_t *src = buf;

    for (size_t i = 0; i < to_write; i++) {
        pipe->buffer[pipe->write_pos] = src[i];
        pipe->write_pos = (pipe->write_pos + 1) % PIPE_BUF_SIZE;
    }

    pipe->count += to_write;
    return (ssize_t)to_write;
}

void pipe_close_read(struct Pipe *pipe) {
    if (!pipe) return;
    pipe->readers--;
    if (pipe->readers == 0)
        pipe->closed_read = true;
    if (pipe->closed_read && pipe->closed_write)
        pipe_destroy(pipe);
}

void pipe_close_write(struct Pipe *pipe) {
    if (!pipe) return;
    pipe->writers--;
    if (pipe->writers == 0)
        pipe->closed_write = true;
    if (pipe->closed_read && pipe->closed_write)
        pipe_destroy(pipe);
}

#define FD_TYPE_NONE    0
#define FD_TYPE_CONSOLE 1
#define FD_TYPE_FILE    2
#define FD_TYPE_PIPE_R  3
#define FD_TYPE_PIPE_W  4

#define MAX_GLOBAL_FDS  512

static struct FileDescriptor fd_table[MAX_GLOBAL_FDS];
static bool fd_table_init = false;

static void ensure_fd_table(void) {
    if (fd_table_init) return;
    kmemset(fd_table, 0, sizeof(fd_table));

    fd_table[0].type = FD_TYPE_CONSOLE;
    fd_table[1].type = FD_TYPE_CONSOLE;
    fd_table[2].type = FD_TYPE_CONSOLE;

    fd_table_init = true;
}

static int fd_alloc(void) {
    ensure_fd_table();
    for (int i = 3; i < MAX_GLOBAL_FDS; i++) {
        if (fd_table[i].type == FD_TYPE_NONE)
            return i;
    }
    return -EMFILE;
}

int sys_pipe(int pipefd[2]) {
    ensure_fd_table();

    struct Pipe *pipe = pipe_create();
    if (!pipe) return -ENOMEM;

    int read_fd = fd_alloc();
    if (read_fd < 0) {
        pipe_destroy(pipe);
        return read_fd;
    }

    fd_table[read_fd].type = FD_TYPE_PIPE_R;
    fd_table[read_fd].data = pipe;

    int write_fd = fd_alloc();
    if (write_fd < 0) {
        fd_table[read_fd].type = FD_TYPE_NONE;
        pipe_destroy(pipe);
        return write_fd;
    }

    fd_table[write_fd].type = FD_TYPE_PIPE_W;
    fd_table[write_fd].data = pipe;

    pipefd[0] = read_fd;
    pipefd[1] = write_fd;

    return 0;
}

int sys_dup(int oldfd) {
    ensure_fd_table();

    if (oldfd < 0 || oldfd >= MAX_GLOBAL_FDS) return -EBADF;
    if (fd_table[oldfd].type == FD_TYPE_NONE) return -EBADF;

    int newfd = fd_alloc();
    if (newfd < 0) return newfd;

    fd_table[newfd] = fd_table[oldfd];

    if (fd_table[oldfd].type == FD_TYPE_PIPE_R) {
        struct Pipe *p = fd_table[oldfd].data;
        if (p) p->readers++;
    } else if (fd_table[oldfd].type == FD_TYPE_PIPE_W) {
        struct Pipe *p = fd_table[oldfd].data;
        if (p) p->writers++;
    }

    return newfd;
}

int sys_dup2(int oldfd, int newfd) {
    ensure_fd_table();

    if (oldfd < 0 || oldfd >= MAX_GLOBAL_FDS) return -EBADF;
    if (newfd < 0 || newfd >= MAX_GLOBAL_FDS) return -EBADF;
    if (fd_table[oldfd].type == FD_TYPE_NONE) return -EBADF;
    if (oldfd == newfd) return newfd;

    if (fd_table[newfd].type == FD_TYPE_PIPE_R)
        pipe_close_read(fd_table[newfd].data);
    else if (fd_table[newfd].type == FD_TYPE_PIPE_W)
        pipe_close_write(fd_table[newfd].data);

    fd_table[newfd] = fd_table[oldfd];

    if (fd_table[oldfd].type == FD_TYPE_PIPE_R) {
        struct Pipe *p = fd_table[oldfd].data;
        if (p) p->readers++;
    } else if (fd_table[oldfd].type == FD_TYPE_PIPE_W) {
        struct Pipe *p = fd_table[oldfd].data;
        if (p) p->writers++;
    }

    return newfd;
}

ssize_t fd_read(int fd, void *buf, size_t count) {
    ensure_fd_table();
    if (fd < 0 || fd >= MAX_GLOBAL_FDS) return -EBADF;

    struct FileDescriptor *desc = &fd_table[fd];

    switch (desc->type) {
    case FD_TYPE_PIPE_R:
        return pipe_read(desc->data, buf, count);
    default:
        return -EBADF;
    }
}

ssize_t fd_write(int fd, const void *buf, size_t count) {
    ensure_fd_table();
    if (fd < 0 || fd >= MAX_GLOBAL_FDS) return -EBADF;

    struct FileDescriptor *desc = &fd_table[fd];

    switch (desc->type) {
    case FD_TYPE_PIPE_W:
        return pipe_write(desc->data, buf, count);
    default:
        return -EBADF;
    }
}

void fd_close(int fd) {
    ensure_fd_table();
    if (fd < 0 || fd >= MAX_GLOBAL_FDS) return;

    struct FileDescriptor *desc = &fd_table[fd];

    switch (desc->type) {
    case FD_TYPE_PIPE_R:
        pipe_close_read(desc->data);
        break;
    case FD_TYPE_PIPE_W:
        pipe_close_write(desc->data);
        break;
    }

    desc->type = FD_TYPE_NONE;
    desc->data = NULL;
}
