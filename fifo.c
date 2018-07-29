#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#define CHECK_COM_LINE_ARG(argc)                                  \
if(argc > 2)                                                      \
    {                                                             \
        printf("\n\tYou have to enter name of the programm"       \
                        "and[optional] name of file with data\n");\
        exit(EXIT_FAILURE);                                       \
    }

#define ERROR_CREATING(file_name)                               \
    {                                                           \
        printf("\n\tCan't create "#file_name"\n");              \
        exit(EXIT_FAILURE);                                     \
    }

#define ERROR_WRITING(file_name)                                \
    {                                                           \
        printf("\n\tCan't write in "#file_name"\n");            \
        exit(EXIT_FAILURE);                                     \
    }

#define ERROR_OPENING(file_name)                                \
    {                                                           \
        perror(#file_name);                                     \
        exit(EXIT_FAILURE);                                     \
    }

enum general_const
{
    BUF_SIZE    = 16384,
    PID_MAX_LEN = 10
};

/*reader function*/
void setAccessAndReadFromUniqFifo(pid_t pid);
/*writer function*/
void getAccessAndWriteInUniqFifo(const char* name_of_data_file);

int main(int argc, char* argv[])
{
    CHECK_COM_LINE_ARG(argc)

    if(argc == 1)
    {
        pid_t pid = getpid();
        setAccessAndReadFromUniqFifo(pid);
    }
    if(argc == 2)
        getAccessAndWriteInUniqFifo(argv[1]);

    return 0;
}

void setAccessAndReadFromUniqFifo(pid_t pid)
{
    int ret_mkfifo = mkfifo("fifo_access", 0666);
    if((ret_mkfifo != 0) && (errno != EEXIST))
        ERROR_CREATING("fifo_access");
    char pid_str[PID_MAX_LEN] = {};
    sprintf(pid_str, "%d", pid);
    int fifo_access_fd = open("fifo_access", O_WRONLY);
    int wr_bytes = write(fifo_access_fd, pid_str, PID_MAX_LEN);   
    if(wr_bytes < 0)
        ERROR_WRITING("fifo_access");
    close(fifo_access_fd);
    
    char uniq_fifo_name[5 + PID_MAX_LEN] = {};
    sprintf(uniq_fifo_name, "fifo_%d", pid);
    ret_mkfifo = mkfifo(uniq_fifo_name, 0666);
    if((ret_mkfifo != 0) && (errno != EEXIST))
        ERROR_CREATING(uniq_fifo_name);

   // exit(1);
    int uniq_fifo_fd1 = open(uniq_fifo_name, O_RDWR);
    int uniq_fifo_fd2 = open(uniq_fifo_name, O_RDONLY);
    close(uniq_fifo_fd1);

    sleep(1);
    char out_buf[BUF_SIZE] = {};
    ssize_t read_bytes = read(uniq_fifo_fd2, out_buf, BUF_SIZE);
    while(read_bytes > 0)
    {
        write(STDOUT_FILENO, out_buf, read_bytes);
        read_bytes = read(uniq_fifo_fd2, out_buf, BUF_SIZE);
    }
    close(uniq_fifo_fd2);
}

void getAccessAndWriteInUniqFifo(const char* name_of_data_file)
{
    int ret_mkfifo = mkfifo("fifo_access", 0666);
    if((ret_mkfifo != 0) && (errno != EEXIST))
        ERROR_CREATING("fifo_access");
    int fifo_access_fd = open("fifo_access", O_RDONLY);
    int read_len = 0;
    char pid_str[PID_MAX_LEN] = {};
    if((read_len = read(fifo_access_fd, pid_str,PID_MAX_LEN)) == 0)
        exit(EXIT_FAILURE);
    close(fifo_access_fd);
 //   exit(1);
    char uniq_fifo_name[5 + PID_MAX_LEN] = {};
    sprintf(uniq_fifo_name, "fifo_%s", pid_str);
    ret_mkfifo = mkfifo(uniq_fifo_name, 0666);
    if((ret_mkfifo != 0) && (errno != EEXIST))
        ERROR_CREATING(uniq_fifo_name);

    int uniq_fifo_fd1 = open(uniq_fifo_name, 
                                            O_RDONLY | O_NONBLOCK);
    int uniq_fifo_fd2 = open(uniq_fifo_name, O_WRONLY);
    close(uniq_fifo_fd1);
      
 //     exit(1);
    int data_file_fd = open(name_of_data_file, O_RDONLY);
    if(data_file_fd == -1)
        ERROR_OPENING(name_of_data_file);
    char buf[BUF_SIZE] = {};
    ssize_t read_bytes = read(data_file_fd, buf, BUF_SIZE);
    while(read_bytes > 0)
    {
        ssize_t ret_write = write(uniq_fifo_fd2, buf, read_bytes);
        if(ret_write < read_bytes)
            exit(EXIT_FAILURE);
        read_bytes = read(data_file_fd, buf, BUF_SIZE);
    }
    close(uniq_fifo_fd2);
}
