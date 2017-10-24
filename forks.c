#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <string.h>

#define CHECK_QUEUE_CREATED(qid)                        \
if(qid < 0)                                             \
    {                                                   \
        printf("\n\tError creating msg queue\n");       \
        exit(EXIT_FAILURE);                             \
    }                       

#define ERROR_RCV                                       \
{                                                       \
    printf("\n\tError receiving msg\n");                \
    exit(EXIT_FAILURE);                                 \
}

#define CHECK_PID_NONNEG(pid)                           \
if(pid < 0)                                             \
{                                                       \
    printf("\n\tError forking\n");                      \
    exit(EXIT_FAILURE);                                 \
}


long read_number(int argc, char *argv[]);

struct msgbuf
{
    long type;
    char text[10];
};

int main(int argc, char *argv[])
{
	long number = read_number(argc, argv);
    int qid = msgget(IPC_PRIVATE, 0666);
    CHECK_QUEUE_CREATED(qid) 
    struct msgbuf msg;
    sprintf(msg.text , "Printf num");
    long i = 0;
	for(i = 2; i <= number + 1; i++)
	{
        int pid = fork();
        CHECK_PID_NONNEG(pid)
        if(pid == 0)
        {
            char text[10];
            if(!msgrcv(qid, (void*) text, 10 , i, 0))
                ERROR_RCV
            printf("%lu ", i - 1);
            fflush(stdout);
            msg.type = 1;
            if(msgsnd(qid, (void*)&msg, 10, 0) != 0)
                exit(EXIT_FAILURE);
            exit(EXIT_SUCCESS);
        }
    }
    msg.type = 1;
    if(msgsnd(qid, (void*)&msg, 10, 0) != 0)
        exit(EXIT_FAILURE);
    char text[10];
    for(i = 2; i <= number + 1; i++)
    {
        if(!msgrcv(qid, (void*) text, 10 , 1, 0))
            ERROR_RCV   
        msg.type = i;
        if(msgsnd(qid, (void*)&msg, 10, 0) != 0)
            exit(EXIT_FAILURE);
    }
    if(!msgrcv(qid, (void*) text, 10 , 1, 0))
        ERROR_RCV   
    msgctl(qid, IPC_RMID, NULL);
    printf("\n");
    return 0;
}

long read_number(int argc, char *argv[])
{
    int base;
	char *endptr, *str;
	long val;

	if (argc < 2) 
	{
	    fprintf(stderr, "Usage: %s str [base]\n", argv[0]);
	    exit(EXIT_FAILURE);
	}

	str = argv[1];
	base = (argc > 2) ? atoi(argv[2]) : 10;

	errno = 0;    /* To distinguish success/failure after call */
	val = strtol(str, &endptr, base);

	/* Check for various possible errors */

	if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
		|| (errno != 0 && val == 0)) {
	    perror("strtol");
	    exit(EXIT_FAILURE);
	}

	if (endptr == str) 
	{
               fprintf(stderr, "No digits were found\n");
               exit(EXIT_FAILURE);
        }

        /* If we got here, strtol() successfully parsed a number */


	if (*endptr != '\0')        /* Not necessarily an error... */
	{
		printf("Further characters after number: %s\n",endptr);

	  	exit(EXIT_SUCCESS);
	}

    return val;
}


