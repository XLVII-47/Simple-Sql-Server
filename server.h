

#define MAX_PATH_SIZE 4096

struct Serversetup
{
    char datasetPath[MAX_PATH_SIZE];
    char pathToLogFile[MAX_PATH_SIZE];
    int logfd;
    uint16_t port;
    int poolSize;
    int socketfd;
    pthread_t *threads;
    int *sockets;
    uint16_t activethreadcount;
    pthread_mutex_t m;
    pthread_cond_t empty, full;
};

int main(int argc, char **argv);
void initThread();
void errExit(char *str);
void serverlog(char *str);
void threadlog(uint16_t index, char *str);
void *threadmain(void *data);
void thread(uint16_t index);
void scheduler();
void usage_print();
void init_log();
void signalhandle();
void exit_clear();
void free_thread();
void free_csv();