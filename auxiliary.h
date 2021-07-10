
#if !defined(AUX_H)
#define AUX_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#define buffersize 4096 * 2

struct Csv
{
    char **header;
    char ***table;
    int cols;
    int size;
    int cap;

    int AR, AW, WW, WR;
    pthread_cond_t okToRead, okToWrite;
    pthread_mutex_t m;
};

struct Query
{
    char **data;
    int size;
    int cap;
};

void parse_file(char *str, char **tosave);
void realloctable(struct Csv *handle);
void insert_row(struct Csv *handle, char **row);
void get_line(char *str);
void find_col(char *filename, struct Csv *handle);
void read_file(char *filename, struct Csv *handle);
void set_csv(char *filename, struct Csv *handle);

void print_table(struct Csv *handle);
void set_query(struct Query *q);
void add_elem2query(struct Query *q, char *elem);
struct Query *parse_query(char *str);

int find_index(char **data, int len, char *str);
void add_string(char **dest, char *str);
char *searchforquery(struct Csv *handle, struct Query *q, int *updatevariable);
char *question(struct Csv *csv, char *query, int *updatevariable);
int find_entry_count(char *ret);

int send_data(int sockfd, const void *buffer, size_t bufsize);
int receive_data(int sockfd, void *buffer, size_t bufsize);
int send_int32(int sockfd, int32_t data);
int receive_int32(int sockfd, int32_t *data);
int send_str(int sockfd, const char *data);
int receive_str(int sockfd, char **data);

void reader_lock(struct Csv *handle);
void write_lock(struct Csv *handle);
void reader_unlock(struct Csv *handle);
void writer_unlock(struct Csv *handle);
void errexit(char *str);
void print_console(char *str);
void free_query(struct Query *query);
#endif // AUX_H
