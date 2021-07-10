#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/stat.h>
#include <syslog.h>
#include <fcntl.h>
#include <pthread.h>
#include "server.h"
#include "auxiliary.h"

#define EMPTY_SOCKET -11

struct Serversetup server;
struct Csv csv;
volatile sig_atomic_t exit_signal = 0;

int main(int argc, char **argv)
{

    sigset_t block;
    sigemptyset(&block);
    sigaddset(&block, SIGINT);
    if (sigprocmask(SIG_BLOCK, &block, NULL) < 0)
    {
        errExit("main, block");
    }

    if (argc != 9)
    {
        usage_print();
    }

    char c;
    while ((c = getopt(argc, argv, ":p:o:l:d:")) != -1)
    {
        switch (c)
        {
        case 'o':
            strcpy(server.pathToLogFile, optarg);
            break;
        case 'p':
            server.port = atoi(optarg);
            if (server.port < 1000)
            {
                usage_print();
            }
            break;
        case 'l':
            server.poolSize = atoi(optarg);
            if (server.poolSize < 2)
            {
                usage_print();
            }
            break;
        case 'd':
            strcpy(server.datasetPath, optarg);
            break;
        case ':':
        default:
            usage_print();
            break;
        }
    }

    server.socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == server.socketfd)
    {
        errExit("main, socket");
    }
    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(server.port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    memset(&(sa.sin_zero), '\0', 8);
    if (bind(server.socketfd, (struct sockaddr *)&sa, sizeof(struct sockaddr)) == -1)
    {
        errExit("main, bind");
    }
    if (listen(server.socketfd, SOMAXCONN) == -1)
    {
        errExit("main, listen");
    }
    //daemon

    pid_t child;
    //fork, detach from process group leader
    if ((child = fork()) < 0)
    { //failed fork
        fprintf(stderr, "error: failed fork\n");
        exit(EXIT_FAILURE);
    }
    if (child > 0)
    { //parent
        exit(EXIT_SUCCESS);
    }
    if (setsid() < 0)
    { //failed to become session leader
        fprintf(stderr, "error: failed setsid\n");
        exit(EXIT_FAILURE);
    }

    //catch/ignore signals
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    //fork second time
    if ((child = fork()) < 0)
    { //failed fork
        fprintf(stderr, "error: failed fork\n");
        exit(EXIT_FAILURE);
    }
    if (child > 0)
    { //parent
        exit(EXIT_SUCCESS);
    }

    //new file permissions
    umask(0);
    //change to path directory
    //chdir("/");

    //Close all open file descriptors

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    int fd = open(server.pathToLogFile, O_RDWR | O_CREAT, 0644);
    if (fd == -1)
    {
        errExit("main, open, fd");
    }

    server.logfd = fd;

    init_log();

    char buffer[MAX_PATH_SIZE];
    clock_t begin = clock();
    set_csv(server.datasetPath, &csv);
    clock_t end = clock();
    double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
    sprintf(buffer, "Dataset loaded in %lf seconds with %d records.\n", time_spent, csv.size);
    serverlog(buffer);

    if (chdir("/") == -1)
    {
        errExit("main, chdir");
    }

    initThread();

    struct sigaction ssa;
    ssa.sa_handler = signalhandle;
    sigemptyset(&ssa.sa_mask);
    ssa.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &ssa, NULL) < 0)
    {
        errExit("main, sigaction");
    }

    sigemptyset(&block);
    sigaddset(&block, SIGINT);
    if (sigprocmask(SIG_UNBLOCK, &block, NULL) < 0)
    {
        errExit("main, block");
    }

    while (exit_signal == 0)
    {
        scheduler();
    }

    exit_clear();
    return 0;
}

void usage_print()
{
    print_console("./server [-p PORT > 1000] [-o pathToLogFile] [–l poolSize > 2] [–d datasetPath]\n");
    exit(EXIT_FAILURE);
}

void init_log()
{
    char buffer[MAX_PATH_SIZE * 3];
    serverlog("Executing with parameters:\n");
    sprintf(buffer, "-p %d\n", server.port);
    serverlog(buffer);

    sprintf(buffer, "-o %s\n", server.pathToLogFile);
    serverlog(buffer);

    sprintf(buffer, "-l %d\n", server.poolSize);
    serverlog(buffer);

    sprintf(buffer, "-d %s\n", server.datasetPath);
    serverlog(buffer);
}

void signalhandle()
{
    exit_signal = 1;

    serverlog("Termination signal received, waiting for ongoing threads to complete.\n");
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        errExit("signalhandle, socket");
    }
    struct sockaddr_in serveraddress;
    serveraddress.sin_family = AF_INET;
    serveraddress.sin_port = htons(server.port);
    serveraddress.sin_addr.s_addr = inet_addr("127.0.0.1");
    memset(&(serveraddress.sin_zero), '\0', 8);
    struct sockaddr *sockaddress = (struct sockaddr *)&serveraddress;
    if (connect(sockfd, sockaddress, sizeof(struct sockaddr)) == -1)
    {
        errExit("signalhandle, connect");
    }
    if (close(sockfd) == -1)
    {
        errExit("signalhandle, close");
    }
}

void errExit(char *str)
{

    perror(str);
    exit(EXIT_FAILURE);
}

void initThread()
{

    server.sockets = (int *)calloc(server.poolSize, sizeof(int));
    server.threads = (pthread_t *)calloc(server.poolSize, sizeof(pthread_t));
    if (NULL == server.sockets || NULL == server.threads)
    {
        errExit("initThread, calloc");
    }

    if (pthread_mutex_init(&server.m, NULL) != 0)
    {
        errExit("initThread, pthread_mutex_init");
    }
    if (pthread_cond_init(&server.empty, NULL) != 0)
    {
        errExit("initThread, pthread_cond_init");
    }
    if (pthread_cond_init(&server.full, NULL) != 0)
    {
        errExit("initThread, pthread_cond_init");
    }

    char line[MAX_PATH_SIZE];
    sprintf(line, "A pool of %d threads has been created\n", server.poolSize);
    serverlog(line);

    for (int i = 0; i < server.poolSize; ++i)
    {
        server.sockets[i] = EMPTY_SOCKET;
        if (pthread_create(&server.threads[i], NULL, threadmain, (void *)(uintptr_t)i) != 0)
        {
            errExit("initThread, pthread_create");
        }
    }
}

void *threadmain(void *data)
{
    uint16_t index = (uintptr_t)data;

    while (exit_signal == 0)
    {
        thread(index);
    }
    return NULL;
}

void thread(uint16_t index)
{
    threadlog(index, "waiting for connection\n");
    if (pthread_mutex_lock(&server.m) != 0)
    {
        errExit("thread, pthread_mutex_lock");
    }
    while (EMPTY_SOCKET == server.sockets[index])
    {
        pthread_cond_wait(&server.full, &server.m);

        if (exit_signal)
        {
            if (pthread_mutex_unlock(&server.m) != 0)
            {
                errExit("thread, pthread_mutex_unlock");
            }
            return;
        }
    }
    int sockfd = server.sockets[index];

    if (pthread_mutex_unlock(&server.m) != 0)
    {
        errExit("thread, pthread_mutex_unlock");
    }
    char *str;
    char *records;

    while (1)
    {
        int updatevariable = 0;
        if (receive_str(sockfd, &str) > 0)
        {
            if (strcmp(str, "finish") == 0)
            {
                free(str);
                break;
            }
            char receivemsg[MAX_PATH_SIZE] = "received query '";
            strcat(receivemsg, strstr(str, " ") + 1);
            strcat(receivemsg, "'\n");
            threadlog(index, receivemsg);

            records = question(&csv, str, &updatevariable);
            free(str);
        }
        if (records != NULL)
        {
            char receivemsg[MAX_PATH_SIZE] = "query completed, ";
            int rcount = find_entry_count(records) - 1;
            char number[30];
            sprintf(number, "%d", rcount);
            strcat(receivemsg, number);
            strcat(receivemsg, " records have been returned\n");
            threadlog(index, receivemsg);
            send_str(sockfd, records);
            free(records);
        }
        else
        {

            char receivemsg[MAX_PATH_SIZE] = "query completed, ";

            char number[30];
            sprintf(number, "%d", updatevariable);
            strcat(receivemsg, number);
            strcat(receivemsg, " records have been updated\n");
            threadlog(index, receivemsg);

            send_int32(sockfd, updatevariable);
        }
    }

    if (pthread_mutex_lock(&server.m) != 0)
    {
        errExit("thread, pthread_mutex_lock");
    }
    if (close(sockfd) == -1)
    {
        errExit("thread, close");
    }
    server.sockets[index] = EMPTY_SOCKET;
    server.activethreadcount--;
    if (pthread_cond_broadcast(&server.empty) != 0)
    {
        errExit("thread, pthread_cond_broadcast");
    }
    if (pthread_mutex_unlock(&server.m) != 0)
    {
        errExit("thread, pthread_mutex_unlock");
    }
}

void serverlog(char *str)
{
    time_t tm = time(NULL);
    char time[MAX_PATH_SIZE];
    ctime_r(&tm, time);
    time[strlen(time) - 1] = '\0';
    char buffer[2 * MAX_PATH_SIZE];
    sprintf(buffer, "%s %s", time, str);

    if (write(server.logfd, buffer, strlen(buffer)) == -1)
    {
        errExit("serverlog, write, logFD");
    }
}

void threadlog(uint16_t index, char *str)
{
    char buffer[MAX_PATH_SIZE];
    sprintf(buffer, "Thread #%u: %s", index, str);
    serverlog(buffer);
}

void scheduler()
{
    char buffer[MAX_PATH_SIZE];
    int sockfd = EMPTY_SOCKET;
    struct sockaddr_in clientaddress = {0};
    socklen_t structSize = sizeof(clientaddress);
    sockfd = accept(server.socketfd, (struct sockaddr *)&clientaddress, &structSize);

    if (exit_signal)
    {
        exit_clear();
    }

    if (sockfd == -1)
    {
        errExit("scheduler, accept");
    }
    if (pthread_mutex_lock(&server.m) != 0)
    {
        errExit("scheduler, pthread_mutex_lock");
    }
    while (server.activethreadcount == server.poolSize)
    {
        serverlog("No thread is available! Waiting for one.\n");
        pthread_cond_wait(&server.empty, &server.m);

        if (exit_signal)
        {
            if (pthread_mutex_unlock(&server.m) != 0)
            {
                errExit("scheduler, pthread_mutex_unlock");
            }
            exit_clear();
        }
    }
    for (uint16_t i = 0; i < server.poolSize; ++i)
    {
        if (server.sockets[i] == EMPTY_SOCKET)
        {
            server.sockets[i] = sockfd;
            server.activethreadcount++;
            sprintf(buffer, "A connection has been delegated to thread id #%u\n", i);
            serverlog(buffer);
            if (pthread_cond_broadcast(&server.full) != 0)
            {
                errExit("scheduler, pthread_cond_broadcast");
            }
            break;
        }
    }
    if (pthread_mutex_unlock(&server.m) != 0)
    {
        errExit("scheduler, pthread_mutex_unlock");
    }
}

void exit_clear()
{
    if (pthread_mutex_lock(&server.m) != 0)
    {
        errExit("exit_clear, pthread_mutex_lock");
    }
    if (pthread_cond_broadcast(&server.full) != 0)
    {
        errExit("exit_clear, pthread_cond_broadcast");
    }
    if (pthread_cond_broadcast(&server.empty) != 0)
    {
        errExit("exit_clear, pthread_cond_broadcast");
    }

    if (pthread_mutex_unlock(&server.m) != 0)
    {
        errExit("exit_clear, pthread_mutex_unlock");
    }
    free_csv();
    free_thread();
    serverlog("All threads have terminated, server shutting down.\n");
    if (close(server.logfd) == -1)
    {
        errExit("exit_clear, close");
    }
    if (close(server.socketfd) == -1)
    {
        errExit("exit_clear, close, sockfd");
    }
    exit(EXIT_SUCCESS);
}

void free_thread()
{

    for (int i = 0; i < server.poolSize; i++)
    {
        if (pthread_join(server.threads[i], NULL) != 0)
        {
            errExit("free_thread, pthread_join");
        }
    }

    if (pthread_mutex_destroy(&server.m) != 0)
    {
        errExit("free_thread, pthread_mutex_destroy");
    }
    if (pthread_cond_destroy(&server.empty) != 0)
    {
        errExit("free_thread, pthread_cond_destroy");
    }
    if (pthread_cond_destroy(&server.full) != 0)
    {
        errExit("free_thread, pthread_cond_destroy");
    }

    free(server.threads);
    free(server.sockets);
}

void free_csv()
{

    if (csv.header != NULL)
    {
        for (int i = 0; i < csv.cols; i++)
        {
            if (csv.header[i] != NULL)
                free(csv.header[i]);
        }
        free(csv.header);
    }

    if (csv.table != NULL)
    {

        for (int i = 0; i < csv.size; i++)
        {
            for (int j = 0; j < csv.cols; j++)
            {
                free(csv.table[i][j]);
            }
            free(csv.table[i]);
        }

        free(csv.table);
    }
    if (pthread_mutex_destroy(&csv.m) != 0)
    {
        errExit("free_csv, pthread_mutex_destroy");
    }
    if (pthread_cond_destroy(&csv.okToRead) != 0)
    {
        errExit("free_csv, pthread_cond_destroy");
    }
    if (pthread_cond_destroy(&csv.okToWrite) != 0)
    {
        errExit("free_csv, pthread_cond_destroy");
    }
}