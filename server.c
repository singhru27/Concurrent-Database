#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "./comm.h"
#include "./db.h"
#ifdef __APPLE__
#include "pthread_OSX.h"
#endif

/*
 * Use the variables in this struct to synchronize your main thread with client
 * threads. Note that all client threads must have terminated before you clean
 * up the database.
 */
typedef struct server_control {
    pthread_mutex_t server_mutex;
    pthread_cond_t server_cond;
    int num_client_threads;
    int server_stopped;
} server_control_t;

/*
 * Controls when the clients in the client thread list should be stopped and
 * let go.
 */
typedef struct client_control {
    pthread_mutex_t go_mutex;
    pthread_cond_t go;
    int stopped;
} client_control_t;

/*
 * The encapsulation of a client thread, i.e., the thread that handles
 * commands from clients.
 */
typedef struct client {
    pthread_t thread;
    FILE *cxstr;  // File stream for input and output

    // For client list
    struct client *prev;
    struct client *next;
} client_t;

/*
 * The encapsulation of a thread that handles signals sent to the server.
 * When SIGINT is sent to the server all client threads should be destroyed.
 */
typedef struct sig_handler {
    sigset_t set;
    pthread_t thread;
} sig_handler_t;

client_t *thread_list_head;
pthread_mutex_t thread_list_mutex = PTHREAD_MUTEX_INITIALIZER;
server_control_t server_struct = {PTHREAD_MUTEX_INITIALIZER,
                                  PTHREAD_COND_INITIALIZER, 0, 0};
client_control_t client_struct = {PTHREAD_MUTEX_INITIALIZER,
                                  PTHREAD_COND_INITIALIZER, 0};

// Function declarations
void *run_client(void *arg);
void *monitor_signal(void *arg);
void thread_cleanup(void *arg);

// function which unlocks a passed in mutex
void unlock_mutex(void *arg) {
    int error;

    pthread_mutex_t *mutex = (pthread_mutex_t *)arg;
    if ((error = pthread_mutex_unlock(mutex))) {
        handle_error_en(error, "pthread_mutex_unlock");
    }
}

// Called by client threads to wait until progress is permitted
void client_control_wait() {
    // TODO: Block the calling thread until the main thread calls
    // client_control_release(). See the client_control_t struct. DONE

    // Locking the go_mutex, and waiting for a broadcast to continue
    int error;

    if ((error = pthread_mutex_lock(&client_struct.go_mutex))) {
        handle_error_en(error, "pthread_mutex_lock");
    }

    pthread_cleanup_push(unlock_mutex, &client_struct.go_mutex);

    while (client_struct.stopped == 1) {
        if ((error = pthread_cond_wait(&client_struct.go,
                                       &client_struct.go_mutex))) {
            handle_error_en(error, "pthread_cond_wait");
        }
    }

    // Unlocking the go_mutex if no cancellation was issued
    pthread_cleanup_pop(1);
}

// Called by main thread to stop client threads
void client_control_stop() {
    // TODO: Ensure that the next time client threads call client_control_wait()
    // at the top of the event loop in run_client, they will block.
    int error;

    if ((error = pthread_mutex_lock(&client_struct.go_mutex))) {
        handle_error_en(error, "pthread_mutex_lock");
    }

    client_struct.stopped = 1;

    if ((error = pthread_mutex_unlock(&client_struct.go_mutex))) {
        handle_error_en(error, "pthread_mutex_unlock");
    }
}

// Called by main thread to resume client threads
void client_control_release() {
    // TODO: Allow clients that are blocked within client_control_wait()
    // to continue. See the client_control_t struct. DONE
    int error;

    if ((error = pthread_mutex_lock(&client_struct.go_mutex))) {
        handle_error_en(error, "pthread_mutex_lock");
    }

    client_struct.stopped = 0;

    if ((error = pthread_cond_broadcast(&client_struct.go))) {
        handle_error_en(error, "pthread_broadcast");
    }

    if ((error = pthread_mutex_unlock(&client_struct.go_mutex))) {
        handle_error_en(error, "pthread_mutex_unlock");
    }
}

// Called by listener (in comm.c) to create a new client thread
void client_constructor(FILE *cxstr) {
    // You should create a new client_t struct here and initialize ALL
    // of its fields. Remember that these initializations should be
    // error-checked.
    // TODO:
    // Step 1: Allocate memory for a new client and set its connection stream
    // to the input argument. DONE

    client_t *new_client = malloc(sizeof(client_t));

    // Error checking the new client
    if (new_client == NULL) {
        perror("malloc\n");
        exit(1);
    }

    new_client->cxstr = cxstr;
    new_client->next = NULL;
    new_client->prev = NULL;
    int error;

    // Step 2: Create the new client thread running the run_client routine.
    // We cretae a thread with the server_mutex locked, to ensure proper
    // cancellation  DONE
    if ((error = pthread_mutex_lock(&server_struct.server_mutex))) {
        handle_error_en(error, "pthread_mutex_lock");
    }

    // Incrementing the number of client threads, and then detaching the new
    // thread
    server_struct.num_client_threads++;
    if ((error =
             pthread_create(&new_client->thread, 0, run_client, new_client))) {
        handle_error_en(error, "pthread_create");
    }
    if ((error = pthread_detach(new_client->thread))) {
        handle_error_en(error, "pthread_detach");
    }
    if ((error = pthread_mutex_unlock(&server_struct.server_mutex))) {
        handle_error_en(error, "pthread_mutex_unlock");
    }
}

void client_destructor(client_t *client) {
    // TODO: Free all resources associated with a client.
    // Whatever was malloc'd in client_constructor should
    // be freed here! DONE
    client_t *new_client = client;
    comm_shutdown(new_client->cxstr);
    free(new_client);
}

// Code executed by a client thread
void *run_client(void *arg) {
    // Locking the server_mutex since we will be editing the num_client
    // threads
    int error;

    if ((error = pthread_mutex_lock(&server_struct.server_mutex))) {
        handle_error_en(error, "pthread_mutex_lock");
    }

    // Step 1: Make sure that the server is still accepting clients.
    client_t *new_client = (client_t *)arg;

    // If stopped, we remove the client and then call exit on the thread.
    // The unlock function is pushed before the client_destructor is called
    // since client_destructor contains a cancellation point

    if (server_struct.server_stopped == 1) {
        pthread_cleanup_push(unlock_mutex, &server_struct.server_mutex);
        server_struct.num_client_threads--;
        client_destructor(new_client);

        // Broadcasting to the server_struct condition variable if the thread
        // list is empty
        if (server_struct.num_client_threads == 0) {
            if ((error = pthread_cond_broadcast(&server_struct.server_cond))) {
                handle_error_en(error, "pthread_broadcast");
            }
        }

        pthread_cleanup_pop(1);
        pthread_exit(0);
    }

    // Step 2: Add client to the client list  DONE

    // Locking the thread_list_mutex
    if ((error = pthread_mutex_lock(&thread_list_mutex))) {
        handle_error_en(error, "pthread_mutex_lock");
    }

    // If there is nothing in the list, the head is just set
    // to the newly created client
    if (thread_list_head == NULL) {
        thread_list_head = new_client;

        // Otherwise, we iterate through the list to find the last client which
        // does not have a next client, and set accordingly
    } else {
        client_t *current_client = thread_list_head;
        while (current_client->next != NULL) {
            current_client = current_client->next;
        }
        // Setting the next and previous pointers of the last client and the new
        // client
        current_client->next = new_client;
        new_client->prev = current_client;
    }

    // Unlocking both the thread_list_mutex and the server_struct mutex
    if ((error = pthread_mutex_unlock(&thread_list_mutex))) {
        handle_error_en(error, "pthread_mutex_unlock");
    }
    if ((error = pthread_mutex_unlock(&server_struct.server_mutex))) {
        handle_error_en(error, "pthread_mutex_unlock");
    }

    // Pushing a cleanup handler for cleaning up the thread onto the stack,
    // since there is now a cancellation point in the comm_serve function
    pthread_cleanup_push(thread_cleanup, new_client);
    char response[512] = {0};
    char command[512] = {0};

    // Step 3: Loop comm_serve (in comm.c) to receive commands and output
    //       responses. Note that the client may terminate the connection at
    //       any moment, in which case reading/writing to the connection stream
    //       on the server side will send this process a SIGPIPE. You must
    //       ensure that the server doesn't crash when this happens!

    while (comm_serve(new_client->cxstr, response, command) != -1) {
        client_control_wait();

        interpret_command(command, response, 512);
    }

    //

    // Step 4: When the client is done sending commands, exit the thread
    //       cleanly.
    // Keep the signal handler thread in mind when writing this function!

    if ((error = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0))) {
        handle_error_en(error, "pthread_setcancelstate");
    }
    pthread_cleanup_pop(1);

    return NULL;
}

void delete_all() {
    // TODO: Cancel every thread in the client thread list with the
    // pthread_cancel function.
    int error;

    if ((error = pthread_mutex_lock(&thread_list_mutex))) {
        handle_error_en(error, "pthread_mutex_lock");
    }
    pthread_cleanup_push(unlock_mutex, &thread_list_mutex);
    client_t *current_client = thread_list_head;
    while (current_client != NULL) {
        if ((error = pthread_cancel(current_client->thread))) {
            handle_error_en(error, "pthread_cancel");
        }
        current_client = current_client->next;
    }
    pthread_cleanup_pop(1);
}

// Cleanup routine for client threads, called on cancels and exit.
void thread_cleanup(void *arg) {
    // TODO: Remove the client object from thread list and call
    // client_destructor. This function must be thread safe! The client must
    // be in the list before this routine is ever run.
    int error;

    // Locking both the server_struct mutex and the threadlist mutex
    if ((error = pthread_mutex_lock(&server_struct.server_mutex))) {
        handle_error_en(error, "pthread_mutex_lock");
    }
    if ((error = pthread_mutex_lock(&thread_list_mutex))) {
        handle_error_en(error, "pthread_mutex_lock");
    }

    client_t *new_client = (client_t *)arg;

    // If the client is the head, we just set the head to NULL
    // and then call client_destructor
    if (new_client == thread_list_head) {
        if (new_client->next != NULL) {
            thread_list_head = new_client->next;
            thread_list_head->prev = NULL;
        } else {
            thread_list_head = NULL;
        }

        // Otherise, we set the prev_client->next to be the next_client
        // of the current client and then set the previous pointer of the
        // next client
    } else {
        client_t *prev_client = new_client->prev;
        prev_client->next = new_client->next;

        if (prev_client->next != NULL) {
            prev_client->next->prev = prev_client;
        }
    }

    // Decrementing the number of clients in our list
    client_destructor(new_client);
    server_struct.num_client_threads--;

    // Broadcasting to the server_struct condition variable if the thread list
    // is empty
    if (server_struct.num_client_threads == 0) {
        if ((error = pthread_cond_broadcast(&server_struct.server_cond))) {
            handle_error_en(error, "pthread_broadcast");
        }
    }

    // Unlocking both mutexes
    if ((error = pthread_mutex_unlock(&thread_list_mutex))) {
        handle_error_en(error, "pthread_mutex_unlock");
    }
    if ((error = pthread_mutex_unlock(&server_struct.server_mutex))) {
        handle_error_en(error, "pthread_mutex_unlock");
    }
}

// Code executed by the signal handler thread. For the purpose of this
// assignment, there are two reasonable ways to implement this.
// The one you choose will depend on logic in sig_handler_constructor.
// 'man 7 signal' and 'man sigwait' are both helpful for making this
// decision. One way or another, all of the server's client threads
// should terminate on SIGINT. The server (this includes the listener
// thread) should not, however, terminate on SIGINT!
void *monitor_signal(void *arg) {
    // TODO: Wait for a SIGINT to be sent to the server process and cancel
    // all client threads when one arrives.
    sig_handler_t *signal_handler = (sig_handler_t *)arg;
    int error;

    while (1) {
        int sig;
        if ((error = sigwait(&signal_handler->set, &sig))) {
            handle_error_en(error, "sigwait");
        }
        fprintf(stderr, "SIGINT received, cancelling all clients \n");

        // This cancels all client threads when the signal has arrived
        // Setting the "stopped" variable to true, deleting all the threads,
        // and then waiting for all threads to complete deletion before
        // continuing

        if ((error = pthread_mutex_lock(&server_struct.server_mutex))) {
            handle_error_en(error, "pthread_mutex_lock");
        }
        server_struct.server_stopped = 1;
        // Calling delete all
        delete_all();

        // Waiting for a client thread to signal that it has been terminated
        while (server_struct.num_client_threads != 0) {
            if ((error = pthread_cond_wait(&server_struct.server_cond,
                                           &server_struct.server_mutex))) {
                handle_error_en(error, "pthread broadcast");
            }
        }

        // Resetting the stopped flag to 0
        server_struct.server_stopped = 0;

        // Unlocking the server_struct mutex once all threads are terminated
        if ((error = pthread_mutex_unlock(&server_struct.server_mutex))) {
            handle_error_en(error, "pthread_mutex_unlock");
        }
    }
}

sig_handler_t *sig_handler_constructor() {
    // TODO: Create a thread to handle SIGINT. The thread that this function
    // creates should be the ONLY thread that ever responds to SIGINT.
    int error;

    // Set with SIGINT
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);

    // Set with SIGPIPE
    sigset_t sigpipe_set;
    sigemptyset(&sigpipe_set);
    sigaddset(&sigpipe_set, SIGPIPE);

    // Masking SIGPIPE and SIGINT
    if ((error = pthread_sigmask(SIG_BLOCK, &set, 0))) {
        handle_error_en(error, "pthread_sigmask");
    }
    if ((error = pthread_sigmask(SIG_BLOCK, &sigpipe_set, 0))) {
        handle_error_en(error, "pthread_sigmask");
    }

    sig_handler_t *signal_handler = malloc(sizeof(sig_handler_t));
    signal_handler->set = set;
    if ((error = pthread_create(&signal_handler->thread, 0, monitor_signal,
                                signal_handler))) {
        handle_error_en(error, "pthread_mutex_lock");
    }

    return signal_handler;
}

void sig_handler_destructor(sig_handler_t *sighandler) {
    // TODO: Free any resources allocated in sig_handler_constructor.
    free(sighandler);
}

// The arguments to the server should be the port number.
int main(int argc, char *argv[]) {
    int error;
    // This first checks to ensure that the port number was properly
    // passed as an argument.
    int port_number;
    if (argc != 2) {
        fprintf(stderr, "Incorrect Arguments: Please supply port number\n");
        exit(1);
    }

    // Setting the port number to be the first element of the arg array
    port_number = atoi(argv[1]);

    // TODO:
    // Step 1: Set up the signal handler. This also creates the mask for the
    // SIGPIPE

    sig_handler_t *signal_handler = sig_handler_constructor();

    // STEP 2: Start a listener thread for clients (see start_listener in
    // comm.c). DONE
    pthread_t listener_thread = start_listener(port_number, client_constructor);

    // Step 3: Loop for command line input and handle accordingly until EOF.
    while (1) {
        char buffer[16] = {0};
        int characters_read;

        // This is a buffer which the read command reads into, and a pointer
        // used by strtok as well. The delimiter is set to be a space or a tab
        char *buffer_pointer = NULL;
        char *delimiter = " \t";

        // Reading in the data from the user

        if ((characters_read = read(0, buffer, 16)) < 0) {
            perror("read");
        }

        // Step 4: Destroy the signal handler, delete all clients, cleanup the
        //       database, cancel the listener thread, and exit.
        //
        // You should ensure that the thread list is empty before cleaning up
        // the database and canceling the listener thread. Think carefully about
        // what happens in a call to delete_all() and ensure that there is no
        // way for a thread to add itself to the thread list after the server's
        // final delete_all().

        // This indicates that CTRL-D was input
        if (characters_read == 0) {
            // Setting the "stopped" variable to true, deleting all the threads,
            // and then waiting for all threads to complete deletion before
            // continuing
            if ((error = pthread_mutex_lock(&server_struct.server_mutex))) {
                handle_error_en(error, "pthread_mutex_lock");
            }
            // Setting a flag to forbid new threads from creating
            server_struct.server_stopped = 1;
            // Calling delete all
            delete_all();

            // Waiting for a client thread to signal that it has been terminated
            while (server_struct.num_client_threads != 0) {
                if ((error = pthread_cond_wait(&server_struct.server_cond,
                                               &server_struct.server_mutex))) {
                    handle_error_en(error, "pthread_wait");
                }
            }

            // Unlocking the server_struct mutex once all threads are terminated
            if ((error = pthread_mutex_unlock(&server_struct.server_mutex))) {
                handle_error_en(error, "pthread_mutex_unlock");
            }

            // Exiting the listener_thread
            if ((error = pthread_cancel(listener_thread))) {
                handle_error_en(error, "pthread_cancel");
            }

            // Eliminating the sig_handler
            sig_handler_destructor(signal_handler);

            db_cleanup();
            exit(0);
        }

        // Setting the last slot in the buffer to null, and tokenizing
        // the buffer
        buffer[characters_read - 1] = '\0';
        buffer_pointer = strtok(buffer, delimiter);

        if (buffer_pointer == NULL) {
            continue;
        }

        // Handling the P case
        if (strcmp(buffer_pointer, "p") == 0) {
            // Tokenizing to retrieve the filename
            buffer_pointer = strtok(NULL, delimiter);

            if (db_print(buffer_pointer) == -1) {
                fprintf(stderr, "Error Printing\n");
            }

            continue;
        }

        // Handling the S case
        if (strcmp(buffer_pointer, "s") == 0) {
            fprintf(stderr, "Stopping all clients\n");
            client_control_stop();
            continue;
        }
        // Handling the G case
        if (strcmp(buffer_pointer, "g") == 0) {
            fprintf(stderr, "Releasing all clients\n");
            client_control_release();
            continue;
        }
    }
}
