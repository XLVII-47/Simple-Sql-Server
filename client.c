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

#include "auxiliary.h"
#include "client.h"

void usage_print()
{
    print_console("./client [-p PORT > 1000] [-i id > 1] [–a  ipaddress] [–o pathToQueryFile]\n");
    exit(EXIT_FAILURE);
}

void errExit(char *str)
{

    perror(str);
    exit(EXIT_FAILURE);
}
void clientlog(char *str)
{
    time_t tm = time(NULL);
    char time[MAX_PATH_SIZE];
    ctime_r(&tm, time);
    time[strlen(time) - 1] = '\0';
    char buffer[2 * MAX_PATH_SIZE];
    sprintf(buffer, "%s %s", time, str);

    size_t len = strlen(buffer);
    if (write(STDOUT_FILENO, buffer, len) == -1)
    {
        errExit("printStr, write, STDOUT_FILENO");
    }
}

void print_connectmsg(char *ip, uint16_t port, int id)
{

    char buffer[MAX_PATH_SIZE];
    sprintf(buffer, "Client-%d connecting to %s:%u\n", id, ip, port);
    clientlog(buffer);
}

void print_sendingmsg(int id, const char *receive)
{
    char buffer[MAX_PATH_SIZE];
    sprintf(buffer, "Client-%d connected and sending query '%s'\n", id, strstr(receive, " ") + 1);
    clientlog(buffer);
}

void print_terminatemsg(int count)
{
    char buffer[MAX_PATH_SIZE];
    sprintf(buffer, "A total of %d queries were executed, client is terminating.\n", count);
    clientlog(buffer);
}

void server_response(int id, int records, double time_spent)
{
    char buffer[MAX_PATH_SIZE];
    sprintf(buffer, "Server’s response to Client-%d is %d records, and arrived in %lf seconds.\n", id, records, time_spent);
    clientlog(buffer);
}

int main(int argc, char *argv[])
{

    if (argc != 9)
    {
        usage_print();
    }
    char ip[IP_LEN];
    char pathToQueryFile[MAX_PATH_SIZE];
    uint16_t port;
    int id;
    char c;

    while ((c = getopt(argc, argv, ":i:a:p:o:")) != -1)
    {
        switch (c)
        {
        case 'a':
            strcpy(ip, optarg);
            break;
        case 'p':
            port = atoi(optarg);
            if (port < 1000)
            {
                usage_print();
            }
            break;
        case 'i':
            id = atoi(optarg);
            if (id < 1)
            {
                usage_print();
            }
            break;
        case 'o':
            strcpy(pathToQueryFile, optarg);
            break;
        case ':':
        default:
            usage_print();
            break;
        }
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        errExit("main, socket");
    }
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr(ip);
    memset(&(serverAddr.sin_zero), '\0', 8);

    print_connectmsg(ip, port, id);

    struct sockaddr *sockAddr = (struct sockaddr *)&serverAddr;
    if (connect(sockfd, sockAddr, sizeof(struct sockaddr)) == -1)
    {
        errExit("main, connect");
    }

    int fd = open(pathToQueryFile, O_RDONLY);

    if (fd == -1)
    {
        errExit("open,main");
    }
    char buffer[buffersize];
    int seek = 0;
    int count = 0;
    while (read(fd, buffer, buffersize) > 0)
    {
        buffer[buffersize - 1] = '\0';
        get_line(buffer);
        if (strlen(buffer) == 0)
        {
            seek += 1;
            lseek(fd, seek, 0);
            continue;
        }
        seek += strlen(buffer) + 1;
        lseek(fd, seek, 0);
        char *tmpstr = strdup(buffer);
        struct Query *tmp = parse_query(tmpstr);
        free(tmpstr);

        int idofquery = atoi(tmp->data[0]);
        if (idofquery == id)
        {
            print_sendingmsg(id, buffer);
            send_str(sockfd, buffer);

            if (strcmp(tmp->data[1], "UPDATE") == 0)
            {
                int updatedvariable = 0;
                receive_int32(sockfd, &updatedvariable);
                sprintf(buffer, "Server’s response to Client-%d is %d records updated.\n", id, updatedvariable);
                clientlog(buffer);
            }
            else
            {
                char *data;
                clock_t begin = clock();
                if (receive_str(sockfd, &data) > 0)
                {
                    clock_t end = clock();
                    double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;

                    int rcount = find_entry_count(data) - 1;
                    server_response(id, rcount, time_spent);
                    size_t len = strlen(data);
                    if (write(STDOUT_FILENO, data, len) == -1)
                    {
                        errExit("printStr, write, STDOUT_FILENO");
                    }
                    free(data);
                }
            }
            count++;
        }
        free_query(tmp);
        memset(buffer, 0, buffersize);
    }
    send_str(sockfd, "finish");

    print_terminatemsg(count);
    if (-1 == close(sockfd))
    {
        errExit("main, close");
    }
    exit(EXIT_SUCCESS);
}
