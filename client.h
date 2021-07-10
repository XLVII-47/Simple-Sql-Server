#pragma once

#define IP_LEN 16
#define MAX_PATH_SIZE 4096

void errExit(char *str);
void clientlog(char *str);
void print_connectmsg(char *ip, uint16_t port, int id);
void print_sendingmsg(int id, const char *receive);
void print_terminatemsg(int count);
void server_response(int id, int records, double time_spent);
void usage_print();
int main(int argc, char *argv[]);