#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>

#include "defines.h"

enum general_const
{
    BUF_SIZE    = 16384,
    SEM_QT      = 3,
    PID_MAX_LEN = 10
};

enum sem_index
{
    mut_ex,
    full,
    empty
};

void setKeyAndReadFromSharedMem(pid_t pid);
void getKeyAndWriteInSHaredMem(const char* name_of_data_file);

void P(int end_of_sending, int semid, int semnum);
void V(int semid, int semnum);
int produceItem(int data_file_fd, char* buf);
void putItem(const char* buf, char* shmem);
void getItem(char* buf, const char* shmem); 
void consumeItem(const char* buf);


int main(int argc, char* argv[])
{
    CHECK_COM_LINE_ARG(argc)

    if(argc == 1)
    {
        pid_t pid = getpid();
        setKeyAndReadFromSharedMem(pid);
    }

    if(argc == 2)
        getKeyAndWriteInSHaredMem(argv[1]);

    return 0;
}

void setKeyAndReadFromSharedMem(pid_t pid)
{
    int ret_mkfifo = mkfifo("fifo_key", 0666);
    if((ret_mkfifo != 0) && (errno != EEXIST))
        ERROR_CREATING("fifo_key");
    char pid_str[PID_MAX_LEN] = {};
    sprintf(pid_str, "%d", pid);
    int fifo_key_fd = open("fifo_key", O_WRONLY);
    int wr_bytes = write(fifo_key_fd, pid_str, PID_MAX_LEN);   
    if(wr_bytes < 0)
        ERROR_WRITING("fifo_key");
    close(fifo_key_fd);

    key_t key = ftok("shmem", pid);
    int semid = semget(key, SEM_QT, IPC_CREAT | 0666);
    int shmid = shmget(key, BUF_SIZE, IPC_CREAT | 0666);
    char* shmem = (char*) shmat(shmid, NULL, 0);

    char buf[BUF_SIZE];
    while(1)
    {
        P(0, semid, full);
        P(0, semid, mut_ex);
        getItem(buf, shmem);
        V(semid, mut_ex);
        V(semid, empty);
        consumeItem(buf);
    }
}

void getKeyAndWriteInSHaredMem(const char* name_of_data_file)
{
    int ret_mkfifo = mkfifo("fifo_key", 0666);
    if((ret_mkfifo != 0) && (errno != EEXIST))
        ERROR_CREATING("fifo_key");
    int fifo_key_fd = open("fifo_key", O_RDONLY);
    int read_len = 0;
    char pid_str[PID_MAX_LEN] = {};
    if((read_len = read(fifo_key_fd, pid_str,PID_MAX_LEN)) == 0)
        exit(EXIT_FAILURE);
    close(fifo_key_fd);

    int pid = atoi(pid_str);
    key_t key = ftok("shmem", pid);
    int semid = semget(key, SEM_QT, IPC_CREAT | 0666);
    semctl(semid, mut_ex, SETVAL, 1);
    semctl(semid, full, SETVAL, 0);
    semctl(semid, empty, SETVAL, 1);
    int shmid = shmget(key, BUF_SIZE, IPC_CREAT | 0666);
    char* shmem = (char*) shmat(shmid, NULL, 0);
 
    int data_file_fd = open(name_of_data_file, O_RDONLY);
    if(data_file_fd < 0)
        ERROR_OPENING(name_of_data_file)
    
    char buf[BUF_SIZE];
    int end_of_sending = 0;
    while(1)
    {
        end_of_sending = produceItem(data_file_fd, buf);
        P(end_of_sending, semid, empty);
        P(end_of_sending, semid, mut_ex);
        putItem(buf, shmem);
        V(semid, mut_ex);
        V(semid, full);
    }
}
    
int produceItem(int data_file_fd, char* buf)
{
    bzero(buf, BUF_SIZE);
    ssize_t read_bytes = read(data_file_fd, buf, BUF_SIZE);
    if(read_bytes == 0)
        return 1;

    return 0;
}

void putItem(const char* buf, char* shmem)
{
    memcpy(shmem, buf, BUF_SIZE);
}

void getItem(char* buf, const char* shmem)
{
    memcpy(buf, shmem, BUF_SIZE);
}

void consumeItem(const char* buf)
{
    write(STDOUT_FILENO, buf, BUF_SIZE);
}

void P(int end_of_sending, int semid, int semnum)
{
    struct sembuf sembuf;
    sembuf.sem_num = semnum;
    sembuf.sem_op  = -1;
    sembuf.sem_flg = SEM_UNDO;

    int ret_val = semop(semid, &sembuf, 1);
    if(ret_val == -1)
        exit(EXIT_SUCCESS);

    if(end_of_sending == 1)
    {
        semctl(semid, 0, IPC_RMID);
        exit(EXIT_SUCCESS);
    }
}

void V(int semid, int semnum)
{
    struct sembuf sembuf;
    sembuf.sem_num = semnum;
    sembuf.sem_op  = 1;
    sembuf.sem_flg = SEM_UNDO;

    semop(semid, &sembuf, 1);
}
