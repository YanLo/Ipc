#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/select.h>
#include <unistd.h>
#include <math.h>
#include <assert.h>
#include <error.h>
#include <string.h>

#define MULTPIPE
#include "defines.h"
#define MULTPIPE

enum
{ 
    CHLD_BUF_SIZE   = 1 << 17, //128kB
    PIPE_SIZE       = 1 << 12,
    MAX_CHLD_QT     = 128,
    MAX_BUF_SIZE    = 1 << 20
};

int chld_qt = 0;
int file_fd = -1;
int max_fd   = 0;
fd_set readfds, writefds;
struct timeval tv;

struct node
{
    char*   buf;
    char*   buf_write_from;
    size_t  buf_size;
    ssize_t read_bytes;
    ssize_t written_bytes;
    ssize_t remain_bytes;
    int     read_finished;
    int     write_finished;
    int     read_avail;
    int     write_avail;
    int     new_data;
    int     pipefd_rd[2];
    int     pipefd_wr[2];
};

size_t getBufSize(int chld_num)
{
    size_t buf_size = 512 * pow(3, chld_num); 
    return ((buf_size < MAX_BUF_SIZE) && buf_size) ? buf_size : MAX_BUF_SIZE;
}

void child(int id, int fd_rd, int fd_wr, struct node* nodes);
void parent(struct node* nodes);
void useReadyFds(struct node* nodes);
void readNode(struct node* nodes, int i);
void writeNode(struct node* nodes, int i);

int main(int argc, char* argv[])
{
    CHECK_COM_L_ARG_AND_CHLD_QT(argc, argv)

    file_fd = open(argv[2], O_RDONLY);
    CHECK_RET_VAL(open, file_fd)

    int i = 0;
    struct node nodes[chld_qt + 1];
    for(i = 1; i < chld_qt; i++)
    {
        CHECK_RET_VAL(pipe, pipe(nodes[i].pipefd_rd))
        CHECK_RET_VAL(pipe, pipe(nodes[i].pipefd_wr))
    }
    nodes[0].pipefd_wr[0] = file_fd;
    nodes[chld_qt].pipefd_rd[1] = STDOUT_FILENO;

    int pid = -1;
    int chld_num = chld_qt;
    int fd_rd, fd_wr;
    while(chld_num > 0)
    {
        pid = fork();
        CHECK_RET_VAL(fork, pid)
        if(pid == 0)
        {
            fd_rd = nodes[chld_num - 1].pipefd_wr[0];
            fd_wr = nodes[chld_num].pipefd_rd[1];
            child(chld_num, fd_rd, fd_wr, nodes);
            return 0;
        }
        if(pid > 0)
            chld_num--;
    }
    
    parent(nodes);
    return 0;
}

void parent(struct node* nodes)
{
    int i = 0, flags;
    for(i = 1; i < chld_qt; i++)
    {
        nodes[i].buf_size = getBufSize(chld_qt - i + 1);
        nodes[i].buf = (char*) calloc(nodes[i].buf_size, sizeof(char));
        nodes[i].buf_write_from = nodes[i].buf;
        nodes[i].read_bytes     = 0;
        nodes[i].written_bytes  = 0;
        nodes[i].remain_bytes   = 0;
        nodes[i].read_finished  = 0;
        nodes[i].write_finished = 0;
        nodes[i].read_avail     = 0;
        nodes[i].write_avail    = 0;
        nodes[i].new_data       = 0;
        close(nodes[i].pipefd_rd[1]);
        close(nodes[i].pipefd_wr[0]);
        flags = fcntl(nodes[i].pipefd_wr[1], F_GETFL);
        CHECK_RET_VAL(fcntl, fcntl(nodes[i].pipefd_wr[1], F_SETFL, flags | O_NONBLOCK))
        flags = fcntl(nodes[i].pipefd_rd[0], F_GETFL);
        CHECK_RET_VAL(fcntl, fcntl(nodes[i].pipefd_rd[0], F_SETFL, flags | O_NONBLOCK))
    }
    close(file_fd);

    for(i = 1; i < chld_qt; i++)
    {
        if(nodes[i].pipefd_rd[0] > max_fd)
            max_fd = nodes[i].pipefd_rd[0];
        if(nodes[i].pipefd_wr[1] > max_fd)
            max_fd = nodes[i].pipefd_wr[1];
    } 

    int remain_fds = 0;
    while(1)
    {
        remain_fds = 0;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        for(i = 1; i < chld_qt; i++)
        {
            if(!nodes[i].read_finished)
                remain_fds++;
            if(!nodes[i].write_finished)
                remain_fds++;
            if((!nodes[i].read_avail) && (!nodes[i].read_finished))
                FD_SET(nodes[i].pipefd_rd[0], &readfds);
            if((!nodes[i].write_avail) && (!nodes[i].write_finished))
                FD_SET(nodes[i].pipefd_wr[1], &writefds);
        }
        if(remain_fds == 0)
        {
            for(i = 1; i < chld_qt; i++)
                free(nodes[i].buf);
            exit(EXIT_SUCCESS);
        }
        useReadyFds(nodes);
    }
}

void useReadyFds(struct node* nodes)
{   
    int i = 0;
    int ready_qt = -1;
    ready_qt = select(max_fd + 1, &readfds, &writefds, NULL, &tv);
    CHECK_RET_VAL(select, ready_qt)
    if(ready_qt > 0)
    {
        for(i = 1; i < chld_qt; i++)
        {
            nodes[i].read_avail = nodes[i].write_avail = 0;
            if(FD_ISSET(nodes[i].pipefd_rd[0], &readfds))
                nodes[i].read_avail = 1;
            if(FD_ISSET(nodes[i].pipefd_wr[1], &writefds))
                nodes[i].write_avail = 1;
            if(FD_ISSET(nodes[i].pipefd_rd[0], &readfds) && (nodes[i].new_data == 0))
            {
                nodes[i].read_bytes = read(nodes[i].pipefd_rd[0], nodes[i].buf, nodes[i].buf_size);
                if(nodes[i].read_bytes == 0)
                {
                    nodes[i].read_finished = 1;
                    close(nodes[i].pipefd_rd[0]);
                    nodes[i].new_data = 0;
                }
                else
                    nodes[i].new_data = 1;
            }
            if(FD_ISSET(nodes[i].pipefd_wr[1], &writefds) && (nodes[i].new_data || nodes[i].remain_bytes))
                writeNode(nodes, i);
            else 
                if(FD_ISSET(nodes[i].pipefd_wr[1], &writefds) && nodes[i].read_finished)
                {
                    nodes[i].write_finished = 1;
                    close(nodes[i].pipefd_wr[1]);
                }
        }
    }
}

void writeNode(struct node* nodes, int i)
{
    if(nodes[i].remain_bytes == 0)
    {
        nodes[i].written_bytes  = write(nodes[i].pipefd_wr[1], nodes[i].buf, nodes[i].read_bytes);
        CHECK_RET_VAL(write, nodes[i].written_bytes)
        nodes[i].remain_bytes   = nodes[i].read_bytes - nodes[i].written_bytes;
        nodes[i].buf_write_from = nodes[i].buf + nodes[i].written_bytes;
        if(nodes[i].remain_bytes == 0)
            nodes[i].new_data = 0;
    }
    else
    {
        nodes[i].written_bytes  = write(nodes[i].pipefd_wr[1], nodes[i].buf_write_from, nodes[i].remain_bytes);
        CHECK_RET_VAL(write, nodes[i].written_bytes)
        nodes[i].remain_bytes = nodes[i].remain_bytes - nodes[i].written_bytes;
        nodes[i].buf_write_from = nodes[i].buf_write_from + nodes[i].written_bytes;
        if(nodes[i].remain_bytes == 0)
            nodes[i].new_data = 0;
    }
}

void child(int chld_num, int fd_rd, int fd_wr, struct node* nodes)
{
    int i = 0;
    for(i = 1; i < chld_qt; i++)
    {
        close(nodes[i].pipefd_rd[0]);
        close(nodes[i].pipefd_wr[1]);
        if(nodes[i].pipefd_rd[1] != fd_wr)
            close(nodes[i].pipefd_rd[1]);
        if(nodes[i].pipefd_wr[0] != fd_rd)
            close(nodes[i].pipefd_wr[0]);
    }

    char  buf[CHLD_BUF_SIZE] = {};
    ssize_t read_bytes = read(fd_rd, buf, CHLD_BUF_SIZE);
    while(read_bytes > 0)
    {
        if (chld_num == 2)
            sleep(1);
        write(fd_wr, buf, read_bytes);
        read_bytes = read(fd_rd, buf, CHLD_BUF_SIZE);
    }
    close(fd_rd);
    close(fd_wr);
}
