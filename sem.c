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

#define SEMBUF_OP(ind, semnum, semop, semflg)             \
sops[ind].sem_num = semnum;                               \
sops[ind].sem_op  = semop;                                \
sops[ind].sem_flg = semflg;                               \

enum general_const
{
    BUF_SIZE    = 65536, 
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

void    P_EMPTY(int semid, ssize_t read_bytes, int it_num);
void    P_FULL(int semid);
void    P(int semid, int semnum, int live_semnum_of_another_pr);
void    V(int semid, int semnum, int live_semnum_of_another_pr);
ssize_t produceItem(int data_file_fd, char* buf);
void    putItem(const char* buf, char* shmem, ssize_t read_bytes);
int     getItem(char* buf, const char* shmem, int semid); 
void    consumeItem(const char* buf, int read_bytes);

int main(int argc, char* argv[])
{
    CHECK_COM_LINE_ARG(argc)

    key_t key = ftok(argv[0], PROJECT_ID);
    int semid = semget(key, SEM_QT, IPC_CREAT | 0666);
    if((semid == -1) && (errno != EEXIST))
        ERROR_CREATING("sems_set");
    int shmid = shmget(key, BUF_SIZE + 4, IPC_CREAT | 0666);
    /*4 bytes for store how many bytes read*/
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
    struct sembuf sops[4];
    SEMBUF_OP(0, MUT_USE_SEM, -1, SEM_UNDO)
    SEMBUF_OP(1, MUT_USE_SEM, 0, 0)
    SEMBUF_OP(2, MUT_USE_SEM, 2, SEM_UNDO)
    SEMBUF_OP(3, READER_SEM_IND, 1, SEM_UNDO)

    int ret_semop = semop(semid, sops, 4);
    if(ret_semop < 0)
        exit(EXIT_FAILURE);

    char buf[BUF_SIZE];
    int read_bytes = 0;

    while(1)
    {
        P_FULL(semid);
        P(semid, MUT_EX, WRITER_SEM_IND);
        read_bytes = getItem(buf, shmem, semid);        
        V(semid, MUT_EX, WRITER_SEM_IND);
        V(semid, EMPTY,  WRITER_SEM_IND);
        consumeItem(buf, read_bytes);
    }
}

void writer(int semid, char* shmem, const char* name_of_file)
{
    struct sembuf sops[4] = {};
    SEMBUF_OP(0, MUT_USE_SEM, 0, 0)
    SEMBUF_OP(1, MUT_USE_SEM, 1, SEM_UNDO)
    SEMBUF_OP(2, WRITER_SEM_IND, 1, SEM_UNDO)
    SEMBUF_OP(3, READER_SEM_IND, 1, SEM_UNDO)
    int ret_semop = semop(semid, sops, 4);
    if(ret_semop < 0)
        exit(EXIT_FAILURE);
        
    semctl(semid, MUT_EX, SETVAL, 1);
    semctl(semid, FULL, SETVAL, 0);
    semctl(semid, EMPTY, SETVAL, 1);

    struct sembuf sop;
    sop.sem_num = WR_SET_MUT_SEM;
    sop.sem_op  = 1;
    sop.sem_flg = SEM_UNDO;
    ret_semop = semop(semid, &sop, 1);
    if(ret_semop < 0)
        exit(EXIT_FAILURE);

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
        putItem(buf, shmem, read_bytes);
        V(semid, MUT_EX, READER_SEM_IND);
        V(semid, FULL,   READER_SEM_IND);
        it_num++;
    }
}

void P_FULL(int semid)
{
    struct sembuf sops[5];
    SEMBUF_OP(0, WRITER_SEM_IND, -1, IPC_NOWAIT)
    SEMBUF_OP(1, FULL, -1, 0)
    SEMBUF_OP(2, WRITER_SEM_IND, 1, 0)
    SEMBUF_OP(3, WR_ST_MUT_SEM, -1, 0)
    SEMBUF_OP(4, WR_SET_MUT_SEM, 1, 0)

    int ret_semop = semop(semid, sops, 5);
    if(ret_semop < 0)
        exit(EXIT_FAILURE);    
}

void P_EMPTY(int semid, ssize_t read_bytes, int it_num)
{
    int ret_semop = -1;
    struct sembuf sops[3];
    SEMBUF_OP(0, READER_SEM_IND, -1, IPC_NOWAIT)
    SEMBUF_OP(1, EMPTY, -1, 0)
    SEMBUF_OP(2, READER_SEM_IND, 1, 0)

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
    SEMBUF_OP(0, semnum_of_another_pr, -1, IPC_NOWAIT)      \
    SEMBUF_OP(1, semnum, op, 0)                             \
    SEMBUF_OP(2, semnum_of_another_pr, 1, 0)                \
                                                            \
    int ret_semop = semop(semid, sops, 3);                  \
    if(ret_semop < 0)                                       \
        exit(EXIT_FAILURE);                                 \
}

P_V_FUNCIONS(P, -1)
P_V_FUNCIONS(V, +1)

ssize_t produceItem(int data_file_fd, char* buf)
{
    ssize_t read_bytes = read(data_file_fd, buf, BUF_SIZE);
    if(read_bytes < 0)
    {
        perror("read_from_data_file");
        exit(EXIT_FAILURE);
    }
    return read_bytes;
}

void putItem(const char* buf, char* shmem, ssize_t read_bytes)
{
    *(int*)shmem = (int) read_bytes;
    memcpy(shmem + sizeof(int), buf, BUF_SIZE);
}

int getItem(char* buf, const char* shmem, int semid)
{
    int read_bytes = *(int*)shmem;
    memcpy(buf, shmem + sizeof(int), read_bytes);
    return read_bytes;
}

void consumeItem(const char* buf, int read_bytes)
{
    write(STDOUT_FILENO, buf, read_bytes);
    if(read_bytes < BUF_SIZE)
        exit(EXIT_SUCCESS);
}

