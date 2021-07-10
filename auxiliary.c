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

void free_query(struct Query *query)
{
    for (int i = 0; i < query->size; i++)
    {
        free(query->data[i]);
    }
    free(query->data);
    free(query);
}

void parse_file(char *str, char **tosave)
{
    char s[4096];
    int len = strlen(str);
    int index = 0;
    int counter = 0;
    do
    {
        int tmplen;
        if (str[0] == '\n')
        {
            str[0] = '\0';
            break;
        }
        else if (str[0] == ',')
        {
            strcpy(s, " ");
            tmplen = 1;
            index += 1;
        }
        else if (str[0] == '\"')
        {
            sscanf(++str, "%[^\"]", s);
            tmplen = strlen(s) + 2;
            index += 1 + tmplen;
        }
        else
        {
            sscanf(str, "%[^,]", s);
            tmplen = strlen(s) + 1;
            index += tmplen;
        }
        str += tmplen;

        tosave[counter++] = strdup(s);

    } while (index < len);
}

void realloctable(struct Csv *handle)
{
    handle->table = (char ***)realloc(handle->table, handle->cap * sizeof(char **));
    if (handle->table == NULL)
    {
        errexit("realloctable");
    }
}

void insert_row(struct Csv *handle, char **row)
{
    if (handle->size == handle->cap)
    {
        handle->cap *= 2;
        realloctable(handle);
    }
    handle->table[handle->size] = row;
    handle->size++;
}

void get_line(char *str)
{
    int isquote = 0;
    int i;

    for (i = 0; i < buffersize; i++)
    {
        if (str[i] == EOF || str[i] == '\0')
            break;
        if (str[i] == '\n')
        {
            if (isquote % 2 == 0)
            {
                break;
            }
        }
        else if (str[i] == '\"')
        {
            isquote++;
        }
    }

    str[i] = '\0';
}

void find_col(char *filename, struct Csv *handle)
{
    int fd = open(filename, O_RDONLY);
    if (fd == -1)
    {
        errexit("find_col,open");
    }
    char *str = (char *)calloc(buffersize, sizeof(char));
    char *tmp = str;
    int seek = 0;

    while (read(fd, str, buffersize) > 0)
    {

        get_line(str);
        if (strlen(str) == 0)
        {
            seek += 1;
            lseek(fd, seek, 0);
            continue;
        }
        else
        {
            break;
        }
    }

    int col = 0;

    int len = strlen(str);

    char s[4096];
    int index = 0;
    do
    {
        int tmplen;
        if (str[0] == '\n')
        {
            break;
        }
        else if (str[0] == ',')
        {
            strcpy(s, " ");
            tmplen = 1;
            index += 1;
        }
        else if (str[0] == '\"')
        {
            sscanf(++str, "%[^\"]", s);
            tmplen = strlen(s) + 2;
            index += 1 + tmplen;
        }
        else
        {
            sscanf(str, "%[^,]", s);
            tmplen = strlen(s) + 1;
            index += tmplen;
        }
        str += tmplen;

        col++;

    } while (index < len);

    handle->cols = col;

    free(tmp);
    close(fd);
}

void read_file(char *filename, struct Csv *handle)
{

    int fd = open(filename, O_RDONLY);
    if (fd == -1)
    {
        errexit("find_col,open");
    }
    char buffer[buffersize];
    int i = 0;
    int seek = 0;
    int first = 0;
    while (read(fd, buffer, buffersize) > 0)
    {

        get_line(buffer);
        if (strlen(buffer) == 0)
        {
            seek += 1;
            lseek(fd, seek, 0);
            continue;
        }
        seek += strlen(buffer) + 1;
        lseek(fd, seek, 0);

        if (first == 0)
        {
            handle->header = (char **)malloc(handle->cols * sizeof(char *));
            parse_file(buffer, handle->header);
            first++;
        }
        else
        {

            char **row = (char **)malloc(handle->cols * sizeof(char *));
            parse_file(buffer, row);
            insert_row(handle, row);
        }

        memset(buffer, 0, buffersize);
        i++;
    }

    close(fd);
}
void errexit(char *str)
{
    perror(str);
    exit(EXIT_FAILURE);
}

void set_csv(char *filename, struct Csv *handle)
{
    find_col(filename, handle);
    handle->cap = 100;
    handle->size = 0;

    handle->table = (char ***)malloc(sizeof(char **) * handle->cap);

    read_file(filename, handle);

    handle->AR = 0;
    handle->WR = 0;
    handle->WW = 0;
    handle->AW = 0;

    if (pthread_mutex_init(&handle->m, NULL) != 0)
    {
        errexit("setcsv, pthread_mutex_init");
    }

    if (pthread_cond_init(&handle->okToWrite, NULL) != 0)
    {
        errexit("setcsv, pthread_cond_init");
    }
    if (pthread_cond_init(&handle->okToRead, NULL) != 0)
    {
        errexit("setcsv, pthread_cond_init");
    }
}

void set_query(struct Query *q)
{
    q->cap = 5;
    q->data = (char **)malloc(sizeof(char *) * q->cap);
    q->size = 0;
}

void add_elem2query(struct Query *q, char *elem)
{
    if (q->size == q->cap)
    {
        q->cap += 2;
        q->data = realloc(q->data, sizeof(char *) * q->cap);
    }
    int slen = strlen(elem) + 1;
    q->data[q->size] = (char *)malloc(sizeof(char) * slen);
    strcpy(q->data[q->size], elem);
    q->size++;
}

struct Query *parse_query(char *str)
{
    struct Query *q = (struct Query *)malloc(sizeof(struct Query));
    set_query(q);

    char *token;
    char *rest = str;

    while ((token = strtok_r(rest, " ,=;'\n", &rest)))
    {
        add_elem2query(q, token);
    }

    return q;
}

int find_index(char **data, int len, char *str)
{
    for (int i = 0; i < len; i++)
    {
        if (strcmp(data[i], str) == 0)
        {
            return i;
        }
    }
    return -1;
}

void add_string(char **dest, char *str)
{
    int size = strlen(*dest) + strlen(str) + 1;
    char *tmp = (char *)malloc(sizeof(char) * size);
    sprintf(tmp, "%s%s", *dest, str);
    free(*dest);
    *dest = tmp;
}

char *searchforquery(struct Csv *handle, struct Query *q, int *updatevariable)
{
    if (strcmp(q->data[1], "SELECT") == 0 && strcmp(q->data[2], "DISTINCT") != 0)
    {
        if (strcmp(q->data[2], "*") == 0)
        {
            reader_lock(handle);
            char *ret = calloc(1, 1);

            for (int i = 0; i < handle->cols; i++)
            {
                add_string(&ret, handle->header[i]);
                add_string(&ret, "\t");
            }
            add_string(&ret, "\n");

            for (int i = 0; i < handle->size; i++)
            {
                for (int j = 0; j < handle->cols; j++)
                {

                    add_string(&ret, handle->table[i][j]);
                    add_string(&ret, "\t");
                }

                add_string(&ret, "\n");
            }
            reader_unlock(handle);
            return ret;
        }
        else
        {
            reader_lock(handle);
            int index_count = q->size - 4;
            int indexes[index_count];
            char *ret = calloc(1, 1);
            for (int i = 0; i < index_count; i++)
            {
                indexes[i] = find_index(handle->header, handle->cols, q->data[i + 2]);
                add_string(&ret, handle->header[indexes[i]]);
                add_string(&ret, "\t");
            }
            add_string(&ret, "\n");

            for (int i = 0; i < handle->size; i++)
            {

                for (int j = 0; j < index_count; j++)
                {
                    add_string(&ret, handle->table[i][indexes[j]]);
                    add_string(&ret, "\t");
                }

                add_string(&ret, "\n");
            }
            reader_unlock(handle);
            return ret;
        }
    }
    else if (strcmp(q->data[1], "SELECT") == 0 && strcmp(q->data[2], "DISTINCT") == 0) // 222222222222222222
    {
        if (strcmp(q->data[3], "*") == 0)
        {
            reader_lock(handle);
            char *ret = calloc(1, 1);
            for (int i = 0; i < handle->cols; i++)
            {
                add_string(&ret, handle->header[i]);
                add_string(&ret, "\t");
            }
            add_string(&ret, "\n");

            for (int i = 0; i < handle->size; i++)
            {
                char *rettmp = calloc(1, 1);
                for (int j = 0; j < handle->cols; j++)
                {
                    add_string(&rettmp, handle->table[i][j]);
                    add_string(&rettmp, "\t");
                }
                if (!strstr(ret, rettmp))
                {
                    add_string(&ret, rettmp);
                    add_string(&ret, "\n");
                }
                free(rettmp);
            }
            reader_unlock(handle);
            return ret;
        }
        else
        {
            reader_lock(handle);
            int index_count = q->size - 5;
            int indexes[index_count];
            char *ret = calloc(1, 1);
            for (int i = 0; i < index_count; i++)
            {
                indexes[i] = find_index(handle->header, handle->cols, q->data[i + 3]);
                add_string(&ret, handle->header[indexes[i]]);
                add_string(&ret, "\t");
            }
            add_string(&ret, "\n");
            for (int i = 0; i < handle->size; i++)
            {
                char *rettmp = calloc(1, 1);

                for (int j = 0; j < index_count; j++)
                {
                    add_string(&rettmp, handle->table[i][indexes[j]]);
                    add_string(&rettmp, "\t");
                }

                if (strstr(ret, rettmp) == NULL)
                {
                    add_string(&ret, rettmp);
                    add_string(&ret, "\n");
                }
                free(rettmp);
            }
            reader_unlock(handle);
            return ret;
        }
    }
    else if (strcmp(q->data[1], "UPDATE") == 0 && strcmp(q->data[2], "TABLE") == 0 && strcmp(q->data[3], "SET") == 0)
    {
        write_lock(handle);
        int whereindex = q->size - 3;
        int wherecolindex = whereindex + 1;
        int wherevalueindex = wherecolindex + 1;

        char wherecol[1024], wherevalue[1024];
        strcpy(wherevalue, q->data[wherevalueindex]);
        strcpy(wherecol, q->data[wherecolindex]);
        int setcount = q->size - 7;
        int col[setcount / 2];
        int value[setcount / 2];

        for (int j = 0, i = 0; j < setcount / 2; i += 2, j++)
        {
            col[j] = find_index(handle->header, handle->cols, q->data[i + 4]);
        }
        for (int j = 0, i = 0; j < setcount / 2; i += 2, j++)
        {
            value[j] = i + 5;
        }

        int ind = find_index(handle->header, handle->cols, wherecol);

        for (int i = 0; i < handle->size; i++)
        {
            if (strcmp(handle->table[i][ind], wherevalue) == 0)
            {

                for (int k = 0; k < setcount / 2; k++)
                {
                    free(handle->table[i][col[k]]);
                    handle->table[i][col[k]] = strdup(q->data[value[k]]);
                }
                (*updatevariable)++;
            }
        }
        writer_unlock(handle);
    }

    return NULL;
}

char *question(struct Csv *csv, char *query, int *updatevariable)
{
    struct Query *tmp = parse_query(query);
    char *ret = searchforquery(csv, tmp, updatevariable);
    free_query(tmp);
    return ret;
}

int find_entry_count(char *ret)
{
    int size = strlen(ret);
    int count = 0;
    for (int i = 0; i < size; i++)
    {
        if (ret[i] == '\n')
        {
            count++;
        }
    }
    return count;
}

int send_data(int sockfd, const void *buffer, size_t bufsize)
{
    const char *pbuffer = (const char *)buffer;
    while (bufsize > 0)
    {
        int n = send(sockfd, pbuffer, bufsize, 0);
        if (n < 0)
            return -1;
        pbuffer += n;
        bufsize -= n;
    }
    return 0;
}

int receive_data(int sockfd, void *buffer, size_t bufsize)
{
    char *pbuffer = (char *)buffer;
    while (bufsize > 0)
    {
        int n = recv(sockfd, pbuffer, bufsize, 0);
        if (n <= 0)
            return n;
        pbuffer += n;
        bufsize -= n;
    }
    return 1;
}

int send_int32(int sockfd, int32_t data)
{
    data = htonl(data);
    return send_data(sockfd, &data, sizeof(data));
}

int receive_int32(int sockfd, int32_t *data)
{
    int ret = receive_data(sockfd, data, sizeof(*data));
    if (ret <= 0)
        *data = 0;
    else
        *data = ntohl(*data);
    return ret;
}

int send_str(int sockfd, const char *data)
{
    int32_t size = strlen(data);
    int ret = send_int32(sockfd, size);
    if (ret == 0)
        ret = send_data(sockfd, data, size);
    return ret;
}

int receive_str(int sockfd, char **data)
{
    *data = NULL;
    int32_t size;
    int ret = receive_int32(sockfd, &size);
    if (ret > 0)
    {
        *data = malloc(size + 1);
        if (*data == NULL)
            return -1;
        ret = receive_data(sockfd, *data, size);
        if (ret <= 0)
        {
            free(*data);
            *data = NULL;
        }
        else
        {
            (*data)[size] = '\0';
        }
    }
    return ret;
}

void reader_lock(struct Csv *handle)
{
    if (pthread_mutex_lock(&handle->m) != 0)
    {
        errexit("reader_lock, pthread_mutex_lock");
    }

    while ((handle->AW + handle->WW) > 0)
    {
        handle->WR++;
        pthread_cond_wait(&handle->okToRead, &handle->m);
        handle->WR--;
    }
    handle->AR++;

    if (pthread_mutex_unlock(&handle->m) != 0)
    {
        errexit("reader_lock, pthread_mutex_unlock");
    }
}

void write_lock(struct Csv *handle)
{
    if (pthread_mutex_lock(&handle->m) != 0)
    {
        errexit("reader_lock, pthread_mutex_lock");
    }

    while ((handle->AW + handle->AR) > 0)
    {
        handle->WW++;
        pthread_cond_wait(&handle->okToWrite, &handle->m);
        handle->WW--;
    }
    handle->AW++;

    if (pthread_mutex_unlock(&handle->m) != 0)
    {
        errexit("writer_lock, pthread_mutex_unlock");
    }
}

void reader_unlock(struct Csv *handle)
{
    if (pthread_mutex_lock(&handle->m) != 0)
    {
        errexit("reader_unlock, pthread_mutex_lock");
    }

    handle->AR--;

    if (handle->AR == 0 && handle->WW > 0)
    {
        if (pthread_cond_signal(&handle->okToWrite) != 0)
        {
            errexit("reader_unlock, pthread_cond_signal");
        }
    }

    if (pthread_mutex_unlock(&handle->m) != 0)
    {
        errexit("reader_unlock, pthread_mutex_unlock");
    }
}

void writer_unlock(struct Csv *handle)
{
    if (pthread_mutex_lock(&handle->m) != 0)
    {
        errexit("writer_unlock, pthread_mutex_lock");
    }

    handle->AW--;

    if (handle->WW > 0)
    {
        if (pthread_cond_signal(&handle->okToWrite) != 0)
        {
            errexit("writer_unlock, pthread_cond_signal");
        }
    }
    else if (handle->WR > 0)
    {
        if (pthread_cond_broadcast(&handle->okToRead) != 0)
        {
            errexit("writer_unlock, pthread_cond_broadcast");
        }
    }

    if (pthread_mutex_unlock(&handle->m) != 0)
    {
        errexit("writer_unlock, pthread_mutex_unlock");
    }
}

void print_console(char *str)
{
    size_t len = strlen(str);
    if (write(STDOUT_FILENO, str, len) == -1)
    {
        errexit("print_console, write");
    }
}

/*
int main(void)
{
    set_csv("aa.csv", &csv);
    char query[1024] = "2 UPDATE TABLE SET unit='value1', value='value2' WHERE year='2019';";

    char query1[1024] = "2 SELECT DISTINCT year,unit,value FROM TABLE;";
    printf("%s", question(query1));
    printf("%s", question(query));

    //print_table(&csv);

    return 0;
}

*/