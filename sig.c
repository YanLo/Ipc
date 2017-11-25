#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <strings.h>

#define SIGNAL_DEF
#include "defines.h"
#undef SIGNAL_DEF

enum
{
    BUF_SIZE = 16384
};

int mutex = 1;

void setMutex(int signo)
{
    mutex = 1;
}

int success = 0;

void reashSuccess(int signo)
{
    success = 1;
}

int bit_ext = 0;

void convertSignalToBit(int signo)
{
    if(signo == SIGSEGV)
        bit_ext = 1;
    if(signo == SIGPIPE)
        bit_ext = 0;
}


void tellParent(pid_t parent_id);
void tellChild(pid_t child_id);
void waitParent(sigset_t* emptymask);
void waitChild(sigset_t* waitmask);

void sendFile(const char* name_of_file, int parent_id);
void receiveFile(pid_t child_id);

int main(int argc, char* argv[])
{
    CHECK_COM_LINE_ARG(argc)

    struct sigaction act;
    act.sa_handler = &setMutex;
    if(sigaction(SIGUSR1, &act, NULL) < 0)
        ERROR_SIGACTION
    if(sigaction(SIGUSR2, &act, NULL) < 0)
        ERROR_SIGACTION
    
    act.sa_handler = &convertSignalToBit;
    if(sigaction(SIGSEGV, &act, NULL) < 0)
        ERROR_SIGACTION
    if(sigaction(SIGPIPE, &act, NULL) < 0)
        ERROR_SIGACTION

    act.sa_handler = &reashSuccess;
    if(sigaction(SIGCONT, &act, NULL) < 0)
        ERROR_SIGACTION

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    if(sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
        ERROR_SIGPROCMASK

    int parent_id = getpid();
    int pid = fork();
    if(pid == 0)
        sendFile(argv[1], parent_id);
    else
        receiveFile(pid);
    return 0;
}

void sendFile(const char* name_of_file, pid_t parent_id)
{
    int file_fd = open(name_of_file, O_RDONLY);
    if(file_fd == -1)
        ERROR_OPENING(name_of_file)
    char buf[BUF_SIZE];
    int bit_mask = 1;
    int bit = 0;
    int slider = 0;
    int read_bytes = read(file_fd, buf, BUF_SIZE);
    sigset_t emptymask;
    sigemptyset(&emptymask);

    while(read_bytes > 0)
    {
        for(slider = 0; slider < read_bytes; slider++)
        {
            bit_mask = 1;
            char byte = buf[slider];
            while(bit_mask <= 128)
            {
                bit = byte & bit_mask;
                waitParent(&emptymask);
                if(bit)
                    kill(parent_id, SIGSEGV);
                else
                    kill(parent_id, SIGPIPE);
                tellParent(parent_id);
                bit_mask = bit_mask << 1;
            }
        }
        read_bytes = read(file_fd, buf, BUF_SIZE);
    }

    waitParent(&emptymask);
    kill(parent_id, SIGCONT);
    tellParent(parent_id);
    close(file_fd);
}

void receiveFile(pid_t child_id)
{
    char buf[BUF_SIZE];
    int slider  = 0;
    int shift   = 0;
    int child_alive = 0;
    sigset_t waitmask;
    sigemptyset(&waitmask);
    sigaddset(&waitmask, SIGSEGV);
    sigaddset(&waitmask, SIGPIPE);

    while(1)
    {
        bzero(buf, BUF_SIZE);
        for(slider = 0; slider < BUF_SIZE; slider++)
        {
            shift = 0;
            while(shift < 8)
            {
                waitChild(&waitmask);
                if(success)
                {
                    write(STDOUT_FILENO, buf, slider);
                    exit(EXIT_SUCCESS);
                }
                buf[slider] = buf[slider] | bit_ext << shift;
                shift++;
                tellChild(child_id);
            }
        }
        write(STDOUT_FILENO, buf, BUF_SIZE);
    }
}

void tellParent(pid_t parent_id)
{
    kill(parent_id, SIGUSR2);   
}

void tellChild(pid_t child_id)
{
    kill(child_id, SIGUSR1);
}

void waitParent(sigset_t* emptymask)
{
    while(mutex == 0)
        sigsuspend(emptymask);
    mutex = 0;
}

void waitChild(sigset_t* waitmask)
{
    while(mutex == 0)
        sigsuspend(waitmask);
    mutex = 0;
}