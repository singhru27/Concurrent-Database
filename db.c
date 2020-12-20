#include "./db.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXLEN 256

// The root node of the binary tree, unlike all
// other nodes in the tree, this one is never
// freed (it's allocated in the data region).
node_t head = {"", "", 0, 0, PTHREAD_RWLOCK_INITIALIZER};

// This method creates a read or write lock on a node,
// give a lock_type

void lock_node(node_t *node, int lock_type) {
    if (lock_type == 0) {
        pthread_rwlock_rdlock(&node->lock);
    } else {
        pthread_rwlock_wrlock(&node->lock);
    }
}

node_t *node_constructor(char *arg_name, char *arg_value, node_t *arg_left,
                         node_t *arg_right) {
    size_t name_len = strlen(arg_name);
    size_t val_len = strlen(arg_value);

    if (name_len > MAXLEN || val_len > MAXLEN) return 0;

    node_t *new_node = (node_t *)malloc(sizeof(node_t));

    if (new_node == 0) return 0;

    if ((new_node->name = (char *)malloc(name_len + 1)) == 0) {
        free(new_node);
        return 0;
    }

    if ((new_node->value = (char *)malloc(val_len + 1)) == 0) {
        free(new_node->name);
        free(new_node);
        return 0;
    }

    if ((snprintf(new_node->name, MAXLEN, "%s", arg_name)) < 0) {
        free(new_node->value);
        free(new_node->name);
        free(new_node);
        return 0;
    } else if ((snprintf(new_node->value, MAXLEN, "%s", arg_value)) < 0) {
        free(new_node->value);
        free(new_node->name);
        free(new_node);
        return 0;
    }

    pthread_rwlock_init(&new_node->lock, 0);
    new_node->lchild = arg_left;
    new_node->rchild = arg_right;
    return new_node;
}

void node_destructor(node_t *node) {
    if (node->name != 0) free(node->name);
    if (node->value != 0) free(node->value);
    pthread_rwlock_destroy(&node->lock);
    free(node);
}

// A locktype of 0 indicates a read lock, while a locktype of
// 1 indicates a write lock
node_t *search(char *, node_t *, node_t **, int lock_type);

void db_query(char *name, char *result, int len) {
    // TODO: Make this thread-safe!
    node_t *target;
    node_t *parent;

    // Locking the head node and calling search
    lock_node(&head, 0);
    target = search(name, &head, &parent, 0);

    // Target was not found, the parent is locked so we must unlock it
    if (target == 0) {
        snprintf(result, len, "not found");
        pthread_rwlock_unlock(&parent->lock);
        return;

        // Target was found, parent and target are locked so both must be
        // unlocked
    } else {
        snprintf(result, len, "%s", target->value);
        pthread_rwlock_unlock(&target->lock);
        pthread_rwlock_unlock(&parent->lock);
        return;
    }
}

int db_add(char *name, char *value) {
    // TODO: Make this thread-safe! DONE

    node_t *parent;
    node_t *target;
    node_t *newnode;

    // Locking the head and calling search
    lock_node(&head, 1);

    // Target was already in the database, unlocking both and then
    // returning
    if ((target = search(name, &head, &parent, 1)) != 0) {
        pthread_rwlock_unlock(&target->lock);
        pthread_rwlock_unlock(&parent->lock);
        return (0);
    }

    newnode = node_constructor(name, value, 0, 0);

    // Target was not in the database. Adding the new node
    // then unlocking the parent, then returning
    if (strcmp(name, parent->name) < 0)
        parent->lchild = newnode;
    else
        parent->rchild = newnode;

    pthread_rwlock_unlock(&parent->lock);
    return (1);
}

int db_remove(char *name) {
    // TODO: Make this thread-safe!

    node_t *parent;
    node_t *dnode;
    node_t *next;

    // Locking the head and searching for the node
    lock_node(&head, 1);
    // first, find the node to be removed
    if ((dnode = search(name, &head, &parent, 1)) == 0) {
        // it's not there
        pthread_rwlock_unlock(&parent->lock);
        return (0);
    }

    // dnode is currently locked, parent is currently locked

    // We found it, if the node has no right child, then we can merely replace
    // its parent's pointer to it with the node's left child.

    if (dnode->rchild == 0) {
        if (strcmp(dnode->name, parent->name) < 0)
            parent->lchild = dnode->lchild;
        else
            parent->rchild = dnode->lchild;

        // done with dnode
        pthread_rwlock_unlock(&dnode->lock);
        node_destructor(dnode);
        pthread_rwlock_unlock(&parent->lock);
    } else if (dnode->lchild == 0) {
        // ditto if the node had no left child
        if (strcmp(dnode->name, parent->name) < 0)
            parent->lchild = dnode->rchild;
        else
            parent->rchild = dnode->rchild;

        // done with dnode
        pthread_rwlock_unlock(&dnode->lock);
        node_destructor(dnode);
        pthread_rwlock_unlock(&parent->lock);
    } else {
        // Find the lexicographically smallest node in the right subtree and
        // replace the node to be deleted with that node. This new node thus is
        // lexicographically smaller than all nodes in its right subtree, and
        // greater than all nodes in its left subtree

        lock_node(dnode->rchild, 1);
        next = dnode->rchild;
        node_t **pnext = &dnode->rchild;

        while (next->lchild != 0) {
            // work our way down the lchild chain, finding the smallest node
            // in the subtree.
            node_t *nextl = next->lchild;
            pnext = &next->lchild;

            // Locking the left child of the given node
            lock_node(nextl, 1);

            // Unlocking the parent node
            pthread_rwlock_unlock(&next->lock);
            next = nextl;
        }

        // Moving the information from next node into dnode
        dnode->name = realloc(dnode->name, strlen(next->name) + 1);
        dnode->value = realloc(dnode->value, strlen(next->value) + 1);
        snprintf(dnode->name, MAXLEN, "%s", next->name);
        snprintf(dnode->value, MAXLEN, "%s", next->value);

        // Setting the child node of parent to be the right child of next node
        *pnext = next->rchild;
        pthread_rwlock_unlock(&next->lock);

        node_destructor(next);
        pthread_rwlock_unlock(&dnode->lock);
        pthread_rwlock_unlock(&parent->lock);
    }

    return (1);
}

node_t *search(char *name, node_t *parent, node_t **parentpp, int lock_type) {
    // Search the tree, starting at parent, for a node containing
    // name (the "target node").  Return a pointer to the node,
    // if found, otherwise return 0.  If parentpp is not 0, then it points
    // to a location at which the address of the parent of the target node
    // is stored.  If the target node is not found, the location pointed to
    // by parentpp is set to what would be the the address of the parent of
    // the target node, if it were there.
    //
    // TODO: Make this thread-safe! DONE

    node_t *next;
    node_t *result;

    // Setting next to left child if name is less than parent
    if (strcmp(name, parent->name) < 0) {
        next = parent->lchild;

        // Setting next to right child otherwise
    } else {
        next = parent->rchild;
    }

    // Setting result to null if the next pointer is null
    if (next == NULL) {
        result = NULL;

    } else {
        // Locking the child node
        lock_node(next, lock_type);
        // If the child is equal to the desired key, the result is set to the
        // child
        if (strcmp(name, next->name) == 0) {
            result = next;

            // If it is not, search is called again with the child as the parent
            // node
        } else {
            pthread_rwlock_unlock(&parent->lock);
            return search(name, next, parentpp, lock_type);
        }
    }

    // Setting the parent pointer to point to the parent node
    if (parentpp != NULL) {
        *parentpp = parent;
    }

    return result;
}

static inline void print_spaces(int lvl, FILE *out) {
    for (int i = 0; i < lvl; i++) {
        fprintf(out, " ");
    }
}

/* Recursively traverses the database tree and prints nodes
 * pre-order. */
void db_print_recurs(node_t *node, int lvl, FILE *out) {
    // print spaces to differentiate levels
    print_spaces(lvl, out);

    // print out the current node
    if (node == NULL) {
        fprintf(out, "(null)\n");
        return;
    }

    if (node == &head) {
        fprintf(out, "(root)\n");
    } else {
        fprintf(out, "%s %s\n", node->name, node->value);
    }

    // The passed in node is always locked. We must lock its children,
    // unlock it, and then call the recurse function. Once the recurs function
    // returns, we must unlock the children.
    if (node->lchild != NULL) {
        lock_node(node->lchild, 0);
    }
    if (node->rchild != NULL) {
        lock_node(node->rchild, 0);
    }

    db_print_recurs(node->lchild, lvl + 1, out);
    db_print_recurs(node->rchild, lvl + 1, out);

    if (node->lchild != NULL) {
        pthread_rwlock_unlock(&node->lchild->lock);
    }
    if (node->rchild != NULL) {
        pthread_rwlock_unlock(&node->rchild->lock);
    }
}

/* Prints the whole database, using db_print_recurs, to a file with
 * the given filename, or to stdout if the filename is empty or NULL.
 * If the file does not exist, it is created. The file is truncated
 * in all cases.
 *
 * Returns 0 on success, or -1 if the file could not be opened
 * for writing. */
int db_print(char *filename) {
    FILE *out;

    // Locking the head
    lock_node(&head, 0);

    if (filename == NULL) {
        db_print_recurs(&head, 0, stdout);
        pthread_rwlock_unlock(&head.lock);
        return 0;
    }
    // skip over leading whitespace
    while (isspace(*filename)) {
        filename++;
    }

    if (*filename == '\0') {
        db_print_recurs(&head, 0, stdout);
        pthread_rwlock_unlock(&head.lock);
        return 0;
    }

    if ((out = fopen(filename, "w+")) == NULL) {
        pthread_rwlock_unlock(&head.lock);
        return -1;
    }

    db_print_recurs(&head, 0, out);
    fclose(out);
    pthread_rwlock_unlock(&head.lock);
    return 0;
}

/* Recursively destroys node and all its children. */
void db_cleanup_recurs(node_t *node) {
    if (node == NULL) {
        return;
    }

    db_cleanup_recurs(node->lchild);
    db_cleanup_recurs(node->rchild);

    node_destructor(node);
}

/* Destroys all nodes in the database other than the head.
 * No threads should be using the database when this is called. */
void db_cleanup() {
    db_cleanup_recurs(head.lchild);
    db_cleanup_recurs(head.rchild);
}

/* Interprets the given command string and calls the appropriate database
 * function. Writes up to len-1 bytes of the response message string produced
 * by the database to the response buffer. */
void interpret_command(char *command, char *response, int len) {
    char value[MAXLEN];
    char ibuf[MAXLEN];
    char name[MAXLEN];
    int sscanf_ret;

    if (strlen(command) <= 1) {
        snprintf(response, len, "ill-formed command");
        return;
    }

    // which command is it?
    switch (command[0]) {
        case 'q':
            // Query
            sscanf_ret = sscanf(&command[1], "%255s", name);
            if (sscanf_ret < 1) {
                snprintf(response, len, "ill-formed command");
                return;
            }
            db_query(name, response, len);
            if (strlen(response) == 0) {
                snprintf(response, len, "not found");
            }

            return;

        case 'a':
            // Add to the database
            sscanf_ret = sscanf(&command[1], "%255s %255s", name, value);
            if (sscanf_ret < 2) {
                snprintf(response, len, "ill-formed command");
                return;
            }
            if (db_add(name, value)) {
                snprintf(response, len, "added");
            } else {
                snprintf(response, len, "already in database");
            }

            return;

        case 'd':
            // Delete from the database
            sscanf_ret = sscanf(&command[1], "%255s", name);
            if (sscanf_ret < 1) {
                snprintf(response, len, "ill-formed command");
                return;
            }
            if (db_remove(name)) {
                snprintf(response, len, "removed");
            } else {
                snprintf(response, len, "not in database");
            }

            return;

        case 'f':
            // process the commands in a file (silently)
            sscanf_ret = sscanf(&command[1], "%255s", name);
            if (sscanf_ret < 1) {
                snprintf(response, len, "ill-formed command");
                return;
            }

            FILE *finput = fopen(name, "r");
            if (!finput) {
                snprintf(response, len, "bad file name");
                return;
            }
            while (fgets(ibuf, sizeof(ibuf), finput) != 0) {
                pthread_testcancel();  // fgets is not a cancellation point
                interpret_command(ibuf, response, len);
            }
            fclose(finput);
            snprintf(response, len, "file processed");
            return;

        default:
            snprintf(response, len, "ill-formed command");
            return;
    }
}
