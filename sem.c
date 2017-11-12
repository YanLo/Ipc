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
    PROJECT_ID  = 1341
};

enum sem_index
{
    MUT_USE_SEM,
    MUT_EX,
    FULL,
    EMPTY,
    WRITER_SEM_IND,
    READER_SEM_IND,
    WR_SET_MUT_SEM,
    SEM_QT
};

void    reader(int semid, char* shmem);
void    writer(int semid, char* shmem, const char* name_of_file);

void    startReaderActivity(int semid);
void    startWriterActivity(int semid);
void    setMutualSems(int semid);

void    P_EMPTY(int semid, ssize_t read_bytes, int it_num);
void    P_FULL(int semid);
void    P(int semid, int semnum, int live_semnum_of_another_pr);
void    V(int semid, int semnum, int live_semnum_of_another_pr);
ssize_t produceItem(int data_file_fd, char* buf);
void    putItem(const char* buf, char* shmem);
void    getItem(char* buf, const char* shmem, int semid); 
void    consumeItem(const char* buf);

int main(int argc, char* argv[])
{
    CHECK_COM_LINE_ARG(argc)

    key_t key = ftok(argv[0], PROJECT_ID);
    int semid = semget(key, SEM_QT, IPC_CREAT | 0666);
    if((semid == -1) && (errno != EEXIST))
        ERROR_CREATING("sems_set");
    int shmid = shmget(key, BUF_SIZE, IPC_CREAT | 0666);
    if((shmid == -1) && (errno != EEXIST))
        ERROR_CREATING("shmem");
    char* shmem = (char*) shmat(shmid, NULL, 0);

    if(argc == 1)
        reader(semid, shmem);
    else
        writer(semid, shmem, argv[1]);

    return 0;
}

void reader(int semid, char* shmem)
{
    startReaderActivity(semid);
    char buf[BUF_SIZE];

    while(1)
    {
        P_FULL(semid);
        P(semid, MUT_EX, WRITER_SEM_IND);
        getItem(buf, shmem, semid);
        V(semid, MUT_EX, WRITER_SEM_IND);
        V(semid, EMPTY,  WRITER_SEM_IND);
        consumeItem(buf);
    }
}

void writer(int semid, char* shmem, const char* name_of_file)
{
    startWriterActivity(semid);
    setMutualSems(semid);

    int data_file_fd = open(name_of_file, O_RDONLY);
    if(data_file_fd < 0)
        ERROR_OPENING(name_of_data_file)
    char buf[BUF_SIZE];
    ssize_t read_bytes = 0;
    int it_num = 1;
    while(1)
    {
        read_bytes = produceItem(data_file_fd, buf);
        P_EMPTY(semid, read_bytes, it_num);
        P(semid, MUT_EX, READER_SEM_IND);
        putItem(buf, shmem);
        V(semid, MUT_EX, READER_SEM_IND);
        V(semid, FULL,   READER_SEM_IND);
        it_num++;
    }
}
 
void startReaderActivity(int semid)
{
    struct sembuf sops[4];
    sops[0].sem_num = MUT_USE_SEM;
    sops[0].sem_op  = -1;
    sops[0].sem_flg = SEM_UNDO;

    sops[1].sem_num = MUT_USE_SEM;
    sops[1].sem_num = 0;
    sops[1].sem_flg = 0;

    sops[2].sem_num = MUT_USE_SEM;
    sops[2].sem_op  = 2;
    sops[2].sem_flg = SEM_UNDO;

    sops[3].sem_num = READER_SEM_IND;
    sops[3].sem_op  = 1;
    sops[3].sem_flg = SEM_UNDO;

    int ret_val = semop(semid, sops, 4);
}

void startWriterActivity(int semid)
{
    struct sembuf sops[4] = {};
    sops[0].sem_num = MUT_USE_SEM;
    sops[0].sem_op  = 0;
    sops[0].sem_flg = 0;

    sops[1].sem_num = MUT_USE_SEM;
    sops[1].sem_op  = 1;
    sops[1].sem_flg = SEM_UNDO;

    sops[2].sem_num = WRITER_SEM_IND;
    sops[2].sem_op  = 1;
    sops[2].sem_flg = SEM_UNDO;

    sops[3].sem_num = READER_SEM_IND;
    sops[3].sem_op  = 1;
    sops[3].sem_flg = SEM_UNDO;

    int ret_semop = semop(semid, sops, 4);
    if(ret_semop < 0)
        exit(EXIT_FAILURE);
}

void setMutualSems(int semid)
{
    semctl(semid, MUT_EX, SETVAL, 1);
    semctl(semid, FULL, SETVAL, 0);
    semctl(semid, EMPTY, SETVAL, 1);

    struct sembuf sop;
    sop.sem_num = WR_SET_MUT_SEM;
    sop.sem_op  = 1;
    sop.sem_flg = SEM_UNDO;

    int ret_semop = semop(semid, &sop, 1);
    if(ret_semop < 0)
        exit(EXIT_FAILURE);
}


void P_FULL(int semid)
{
    int ret_semop = -1;
    struct sembuf sops[5];
    sops[0].sem_num = WRITER_SEM_IND;
    sops[0].sem_op  = -1;
    sops[0].sem_flg = IPC_NOWAIT;

    sops[1].sem_num = FULL;
    sops[1].sem_op  = -1;
    sops[1].sem_flg = 0;

    sops[2].sem_num = WRITER_SEM_IND;
    sops[2].sem_op  = 1;
    sops[2].sem_flg = 0;

    sops[3].sem_num = WR_SET_MUT_SEM;
    sops[3].sem_op  = -1;
    sops[3].sem_flg  = 0;

    sops[4].sem_num = WR_SET_MUT_SEM;
    sops[4].sem_op  = 1;
    sops[4].sem_flg = 0;
    ret_semop = semop(semid, sops, 5);

    if(ret_semop < 0)
        exit(EXIT_SUCCESS);    
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

    sops[2].sem_num = READER_SEM_IND;
    sops[2].sem_op  = 1;
    sops[2].sem_flg = 0;

    if(it_num == 2)
        ret_semop = semop(semid, sops, 2);
    else
        ret_semop = semop(semid, sops, 3);

    if(ret_semop < 0)
        exit(EXIT_FAILURE);
    if(read_bytes == 0)
        exit(EXIT_SUCCESS);
}

#define P_V_FUNCIONS(name, op)                              \
void name(int semid, int semnum, int semnum_of_another_pr)  \
{                                                           \
    struct sembuf sops[3];                                  \
    sops[0].sem_num = semnum_of_another_pr;                 \
    sops[0].sem_op  = -1;                                   \
    sops[0].sem_flg = IPC_NOWAIT;                           \
                                                            \
    sops[1].sem_num = semnum;                               \
    sops[1].sem_op  = op;                                   \
    sops[1].sem_flg = 0;                                    \
                                                            \
    sops[2].sem_num = semnum_of_another_pr;                 \
    sops[2].sem_op  = 1;                                    \
    sops[2].sem_flg = 0;                                    \
                                                            \
    int ret_semop = semop(semid, sops, 3);                  \
    if(ret_semop < 0)                                       \
        exit(EXIT_FAILURE);                                 \
}

P_V_FUNCIONS(P, -1)
P_V_FUNCIONS(V, +1)

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

