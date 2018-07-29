#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <strings.h>

#define BUF_SIZE 16384

int main(int argc, char* argv[])
{
    pid_t chid;
    char buf[100];
    int pipefd[2];

    if(argc != 2)
    {
        printf("\nWrong input, two arguments are needed\n");
        exit(EXIT_FAILURE);
    }

    if (pipe(pipefd) == -1)
    {
        perror("pipe");
        exit(EXIT_FAILURE);
    }
    
    chid = fork();

    if(chid == -1)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if(chid == 0)
    {
        printf("\n\tHi, I'm a child\n");
        close(pipefd[0]);

        int fd = open(argv[1], O_RDONLY);
        if(fd == -1)
        {
            perror(argv[1]);
            exit(EXIT_FAILURE);
        }

        char buf[BUF_SIZE] = {};

        ssize_t read_bytes = 0;
        while((read_bytes = read(fd, buf, BUF_SIZE)) == BUF_SIZE)
        {
            write(pipefd[1], buf, BUF_SIZE);
        }
        write(pipefd[1], buf, read_bytes);
    }
    else
    {
        printf("\n\tHi, I'm a parent\n");
        close(pipefd[1]);
        char out_buf[BUF_SIZE] = {};

        ssize_t read_bytes = 0;
        while((read_bytes = read(pipefd[0], out_buf, 
                                        BUF_SIZE) == BUF_SIZE))
        {
             write(STDOUT_FILENO, out_buf, BUF_SIZE);
        }
        write(STDOUT_FILENO, out_buf, read_bytes);
    }
    
    return 0;
}
