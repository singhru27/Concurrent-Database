#ifndef DB_H_
#define DB_H_

#include <pthread.h>

typedef struct node {
    char *name;
    char *value;
    struct node *lchild;
    struct node *rchild;
    pthread_rwlock_t lock;
} node_t;

extern node_t head;

void interpret_command(char *command, char *response, int resp_capacity);
int db_print(char *filename);
void db_cleanup(void);

#endif  // DB_H_
