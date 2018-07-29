#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define CHECK_COM_LINE_ARG(argc)                                  \
if(argc > 2)                                                      \
    {                                                             \
        printf("\n\tYou have to enter name of the programm"       \
                        "and[optional] name of file with data\n");\
        exit(EXIT_FAILURE);                                       \
    }


enum general_const
{
    BUF_SIZE    = 16384,
    PID_MAX_LEN = 10,
//16384
};

/*reader functions*/
void    createAccessFifoAndWriteIdInIt();
void    createUniqFifoAndReadFromIt(pid_t pid);

/*writer functions*/
int     getAccessAndOpenUniqFifo();
int     getDataFileFd();
void    writeInUniqFifo(int data_file_fd, int fifo_write_fd);

int main(int argc, char* argv[])
{
    CHECK_COM_LINE_ARG(argc)

    if(argc == 2) /*Writer*/
    {
        int fifo_write_fd = getAccessAndOpenUniqFifo();
        int data_file_fd  = getDataFileFd(argv[1]);
        writeInUniqFifo(data_file_fd, fifo_write_fd);

        close(data_file_fd);
        close(fifo_write_fd);
    }

    if(argc == 1) /*Reader*/
    {
        pid_t pid = getpid(); 
        createAccessFifoAndWriteIdInIt(pid);
        createUniqFifoAndReadFromIt(pid);
    }
    return 0;
}

void createAccessFifoAndWriteIdInIt(pid_t pid)
{
    int res_mkfifo = mkfifo("fifo_access", 0666);
    if((res_mkfifo == 0) || (errno == EEXIST))
    {
        char pid_str[PID_MAX_LEN] = {};
        sprintf(pid_str, "%d", pid);
        int fifo_access_fd = open("fifo_access", O_WRONLY);
        int wr_bytes = write(fifo_access_fd, pid_str, 
                                                PID_MAX_LEN);   
        if(wr_bytes < 0)
        {
            printf("\n\tCan't write in fifo_access\n");
            exit(EXIT_FAILURE);
        }
        close(fifo_access_fd);
    }
    else
    {
        printf("\n\tCan't create a fifo_access\n");
        exit(EXIT_FAILURE);
    }
}

int getAccessAndOpenUniqFifo()
{
    int res_mkfifo_access = mkfifo("fifo_access", 0666);
    if((res_mkfifo_access != 0) && (errno != EEXIST))
    {
        printf("\n\tCan't create a fifo_access\n");
        exit(EXIT_FAILURE);
    }
    int fifo_access_fd = open("fifo_access", O_RDONLY);
    int read_len = 0;
    char pid_str[PID_MAX_LEN] = {};
    if((read_len = read(fifo_access_fd, pid_str, 
                                            PID_MAX_LEN)) == 0)
    {
        exit(EXIT_FAILURE);
    }

//    exit(1);

    char fifo_name[5 + PID_MAX_LEN] = {};
    sprintf(fifo_name, "fifo_%s", pid_str);
    int res_mkfifo = mkfifo(fifo_name, 0666);
    if((res_mkfifo != 0) && (errno != EEXIST))
    {
        printf("\n\tCan't create a fifo_%s file\n", pid_str);
        exit(EXIT_FAILURE);
    }

    int fifo_write_fd = open(fifo_name, O_RDWR);
    if(fifo_write_fd == -1)
    {
        perror("open fifo for writing");
        exit(EXIT_FAILURE);
    }
    close(fifo_access_fd);
    return fifo_write_fd;
}

int getDataFileFd(const char* name_of_file)
{
    int fd = open(name_of_file, O_RDONLY);
    if(fd == -1)
    {
        perror(name_of_file);
        exit(EXIT_FAILURE);
    }
    return fd;
}

void writeInUniqFifo(int data_file_fd, int fifo_write_fd)
{
    char buf[BUF_SIZE] = {};
    ssize_t read_bytes = read(data_file_fd, buf, BUF_SIZE);
    while(read_bytes > 0)
    {
        write(fifo_write_fd, buf, read_bytes);
        read_bytes = read(data_file_fd, buf, BUF_SIZE);
    }
}

void createUniqFifoAndReadFromIt(pid_t pid)
{
    char fifo_name[5 + PID_MAX_LEN] = {};
    sprintf(fifo_name, "fifo_%d", pid);
    int res_mkfifo = mkfifo(fifo_name, 0666);
    if((res_mkfifo != 0) && (errno != EEXIST))
    {
        printf("\n\tCan't create a fifo_%d file\n", pid);
        exit(EXIT_FAILURE);
    }

    int fifo_fd = open(fifo_name, O_RDONLY | O_NONBLOCK);

    char out_buf[BUF_SIZE] = {};
    sleep(1);
    ssize_t read_bytes = read(fifo_fd, out_buf, BUF_SIZE);

    while(read_bytes > 0)
    {
        write(STDOUT_FILENO, out_buf, read_bytes);
        read_bytes = read(fifo_fd, out_buf, BUF_SIZE);
    }

    close(fifo_fd);
    char fifo_del_str[8 + PID_MAX_LEN] = {};
    sprintf(fifo_del_str, "fifo_%d", pid);
    unlink(fifo_del_str);
    unlink("fifo_access");
}


