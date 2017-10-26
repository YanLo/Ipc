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

#define INC_VAL  1
#define DEC_VAL -1

enum general_const
{
    BUF_SIZE    = 16384,
    SEM_QT      = 3,
    PID_MAX_LEN = 10
};

enum sem_index
{
    MUT_EX,
    FULL,
    EMPTY
};

enum sem_indicating_proccess_alive
{
    WRITER_SEM_IND = 0,
    READER_SEM_IND = 1,
    WRITER_SEM_VAL = 0xa,
    READER_SEM_VAL = 0xf
};

void setKeyAndReadFromSharedMem(pid_t pid);
void getKeyAndWriteInSHaredMem(const char* name_of_data_file);

void P(int semid, int semnum);
void V(int semid, int semnum);
void produceItem(int data_file_fd, char* buf);
void putItem(const char* buf, char* shmem);
void getItem(char* buf, const char* shmem); 
void consumeItem(const char* buf);

void setReaderLiveSem(int semid, int semnum);
void setWriterLiveSem(int semid, int semnum);
void checkWRITERalive(int semid);
void checkREADERalive(int semid);


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

/*READER*/
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

    int sem_alive_id = semget(key + 1, 2, IPC_CREAT | 0666);
    setReaderLiveSem(sem_alive_id, READER_SEM_IND);

 //   exit(1);
    char buf[BUF_SIZE];
    while(1)
    {
        checkWRITERalive(sem_alive_id);
        P(semid, FULL);
        P(semid, MUT_EX);
        getItem(buf, shmem);
        V(semid, MUT_EX);
        V(semid, EMPTY);
        consumeItem(buf);
    }
}

/*WRITER*/
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
    semctl(semid, MUT_EX, SETVAL, 1);
    semctl(semid, FULL, SETVAL, 0);
    semctl(semid, EMPTY, SETVAL, 1);

    int sem_alive_id = semget(key + 1, 2, IPC_CREAT | 0666);
    setWriterLiveSem(sem_alive_id, WRITER_SEM_IND);

    int shmid = shmget(key, BUF_SIZE, IPC_CREAT | 0666);
    char* shmem = (char*) shmat(shmid, NULL, 0);
 
    int data_file_fd = open(name_of_data_file, O_RDONLY);
    if(data_file_fd < 0)
        ERROR_OPENING(name_of_data_file)
 

 //   exit(1);
    char buf[BUF_SIZE];
    while(1)
    {
        checkREADERalive(sem_alive_id);
        produceItem(data_file_fd, buf);
        P(semid, EMPTY);
        P(semid, MUT_EX);
        putItem(buf, shmem);
        V(semid, MUT_EX);
        V(semid, FULL);
    }
}
    
void produceItem(int data_file_fd, char* buf)
{
    bzero(buf, BUF_SIZE);
    ssize_t read_bytes = read(data_file_fd, buf, BUF_SIZE);
    if(read_bytes == 0)
        exit(EXIT_SUCCESS);
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

#define SEM_FUNC(funcname, val_to_add)          \
void funcname(int semid, int semnum)            \
{                                               \
    struct sembuf sembuf;                       \
    sembuf.sem_num = semnum;                    \
    sembuf.sem_op  = val_to_add;                \
    sembuf.sem_flg = SEM_UNDO;                  \
    semop(semid, &sembuf, 1);                   \
}

SEM_FUNC(P, DEC_VAL)
SEM_FUNC(V, INC_VAL)
SEM_FUNC(setReaderLiveSem, READER_SEM_VAL)
SEM_FUNC(setWriterLiveSem, WRITER_SEM_VAL)

#define CHECK_PROCCESS_ALIVE(proccess)                          \
void check##proccess##alive(int semid)                          \
{                                                               \
    int semval = semctl(semid, proccess##_SEM_IND, GETVAL);     \
    if(semval != proccess##_SEM_VAL)                            \
        exit(EXIT_SUCCESS);                                     \
}

CHECK_PROCCESS_ALIVE(READER)
CHECK_PROCCESS_ALIVE(WRITER)
