#ifndef _PIPE_H
#define _PIPE_H

#include "types.h"

#define PIPE_BUF_SIZE   4096

struct Pipe {
    uint8_t buffer[PIPE_BUF_SIZE];
    size_t read_pos;
    size_t write_pos;
    size_t count;
    uint32_t readers;
    uint32_t writers;
    bool closed_read;
    bool closed_write;
};

struct Pipe *pipe_create(void);
void pipe_destroy(struct Pipe *pipe);

ssize_t pipe_read(struct Pipe *pipe, void *buf, size_t count);
ssize_t pipe_write(struct Pipe *pipe, const void *buf, size_t count);

void pipe_close_read(struct Pipe *pipe);
void pipe_close_write(struct Pipe *pipe);

int sys_pipe(int pipefd[2]);
int sys_dup(int oldfd);
int sys_dup2(int oldfd, int newfd);

#endif
