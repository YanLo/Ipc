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
    PID_MAX_LEN = 10
};

enum sem_index
{
    MUT_EX,
    FULL,
    EMPTY,
    WRITER_SEM_IND,
    READER_SEM_IND,
    SEM_QT
};

void    setKeyAndReadFromSharedMem(pid_t pid);
void    getKeyAndWriteInSHaredMem(const char* name_of_data_file);

void    P(int semid, int semnum, int semnum_of_another_pr);
void    P_EMPTY(int semid, ssize_t read_bytes, int it_num);
void    P_FULL(int semid, int it_num);
void    V(int semid, int semnum);
void    setLiveSem(int semid);
ssize_t produceItem(int data_file_fd, char* buf);
void    putItem(const char* buf, char* shmem);
void    getItem(char* buf, const char* shmem, int semid); 
void    consumeItem(const char* buf);

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
    setLiveSem(semid);
    int shmid = shmget(key, BUF_SIZE, IPC_CREAT | 0666);
    char* shmem = (char*) shmat(shmid, NULL, 0);
    char buf[BUF_SIZE];

 //   exit(1);
    int it_num = 0;
    while(1)
    {
        P_FULL(semid, it_num);
        P(semid, MUT_EX, WRITER_SEM_IND);
        getItem(buf, shmem, semid);
        V(semid, MUT_EX);
        V(semid, EMPTY);
        consumeItem(buf);
        it_num++;
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
    setLiveSem(semid);
    semctl(semid, MUT_EX, SETVAL, 1);
    semctl(semid, FULL, SETVAL, 0);
    semctl(semid, EMPTY, SETVAL, 1);

    int shmid = shmget(key, BUF_SIZE, IPC_CREAT | 0666);
    char* shmem = (char*) shmat(shmid, NULL, 0);
    int data_file_fd = open(name_of_data_file, O_RDONLY);
    if(data_file_fd < 0)
        ERROR_OPENING(name_of_data_file)
    char buf[BUF_SIZE];
    
 //   exit(1);
    ssize_t read_bytes = 0;
    int it_num = 0;
    while(1)
    {
        read_bytes = produceItem(data_file_fd, buf);
        P_EMPTY(semid, read_bytes, it_num);
        P(semid, MUT_EX, READER_SEM_IND);
        putItem(buf, shmem);
        V(semid, MUT_EX);
        V(semid, FULL);
        it_num++;
    }
}
    
ssize_t produceItem(int data_file_fd, char* buf)
{
    bzero(buf, BUF_SIZE);
    ssize_t read_bytes = read(data_file_fd, buf, BUF_SIZE);
    if(read_bytes < 0)
    {
        perror("read_from_data_file");
        exit(EXIT_FAILURE);
    }
    return read_bytes;
}

void putItem(const char* buf, char* shmem)
{
    memcpy(shmem, buf, BUF_SIZE);
}

void getItem(char* buf, const char* shmem, int semid)
{
    memcpy(buf, shmem, BUF_SIZE);
}

void consumeItem(const char* buf)
{
    write(STDOUT_FILENO, buf, BUF_SIZE);
}

void P(int semid, int semnum, int semnum_of_another_pr)
{
    struct sembuf sops[3];
    sops[0].sem_num = semnum_of_another_pr;
    sops[0].sem_op  = -1;
    sops[0].sem_flg = IPC_NOWAIT;

    sops[1].sem_num = semnum;
    sops[1].sem_op  = -1;
    sops[1].sem_flg = 0;

    sops[2].sem_num = semnum_of_another_pr;
    sops[2].sem_op  = 1;
    sops[2].sem_flg = 0;

    int ret_semop = semop(semid, sops, 3);
    if(ret_semop == -1)
    {
        semctl(semid, 0, IPC_RMID);
        exit(EXIT_FAILURE);
    }
}

void P_EMPTY(int semid, ssize_t read_bytes, int it_num)
{
    int ret_semop = -1;
    struct sembuf sops[3];
    sops[0].sem_num = READER_SEM_IND;
    sops[0].sem_op  = -1;
    sops[0].sem_flg = IPC_NOWAIT;

    sops[1].sem_num = EMPTY;
    sops[1].sem_op  = -1;
    sops[1].sem_flg = 0;

    if(it_num != 1)
    {
        sops[2].sem_num = READER_SEM_IND;
        sops[2].sem_op  = 1;
        sops[2].sem_flg = 0;
        ret_semop = semop(semid, sops, 3);
    }
    else
        ret_semop = semop(semid, sops, 2);

    if(ret_semop == -1)
    {
        semctl(semid, 0, IPC_RMID);
        exit(EXIT_FAILURE);
    }
    if(read_bytes == 0)
        exit(EXIT_SUCCESS);
}

void P_FULL(int semid, int it_num)
{
    int ret_semop = -1;
    struct sembuf sops[3];
    sops[0].sem_num = WRITER_SEM_IND;
    sops[0].sem_op  = -1;
    sops[0].sem_flg = IPC_NOWAIT;

    sops[1].sem_num = FULL;
    sops[1].sem_op  = -1;
    sops[1].sem_flg = 0;

    if(it_num != 0)
    {
        sops[2].sem_num = WRITER_SEM_IND;
        sops[2].sem_op  = 1;
        sops[2].sem_flg = 0;
        ret_semop = semop(semid, sops, 3);
    }
    else
        ret_semop = semop(semid, sops, 2);

    if(ret_semop == -1)
    {
        semctl(semid, 0, IPC_RMID);
        exit(EXIT_SUCCESS);
    }
}

void V(int semid, int semnum)
{
    struct sembuf sop;
    sop.sem_num = semnum;
    sop.sem_op  = 1;
    sop.sem_flg = 0;

    semop(semid, &sop, 1);
}

void setLiveSem(int semid)
{
    struct sembuf sops[2];
    sops[0].sem_num = READER_SEM_IND;
    sops[0].sem_op  = 1;
    sops[0].sem_flg = SEM_UNDO;

    sops[1].sem_num = WRITER_SEM_IND;
    sops[1].sem_op  = 1;
    sops[1].sem_flg = SEM_UNDO;

    semop(semid, sops, 2);
}
