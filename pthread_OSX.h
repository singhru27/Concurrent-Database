#ifndef PTHREAD_OSX_H_
#define PTHREAD_OSX_H_

#ifndef PTHREAD_RWLOCK_INITIALIZER
#define PTHREAD_RWLOCK_INITIALIZER                           \
    {                                                        \
        PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, \
            PTHREAD_COND_INITIALIZER, 0, 0, 0                \
    }

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t readersQ;
    pthread_cond_t writersQ;
    unsigned int readers;
    unsigned int writers;
    unsigned int active_writers;
} pthread_rwlock_t;

typedef int pthread_rwlockattr_t;

int pthread_rwlock_init(pthread_rwlock_t *lock, pthread_rwlockattr_t *att);
int pthread_rwlock_destroy(pthread_rwlock_t *lock);
int pthread_rwlock_rdlock(pthread_rwlock_t *lock);
int pthread_rwlock_wrlock(pthread_rwlock_t *lock);
int pthread_rwlock_unlock(pthread_rwlock_t *lock);
#define NEED_RWLOCK
#endif

#ifndef PTHREAD_BARRIER_SERIAL_THREAD
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cv;
    unsigned int generation;
    unsigned int count;
    unsigned int number;
} pthread_barrier_t;

typedef int pthread_barrierattr_t;

int pthread_barrier_init(pthread_barrier_t *barrier,
                         pthread_barrierattr_t *attr, unsigned int count);
int pthread_barrier_destroy(pthread_barrier_t *barrier);
int pthread_barrier_wait(pthread_barrier_t *barrier);
#define PTHREAD_BARRIER_SERIAL_THREAD 1;
#define NEED_BARRIER
#endif

#endif  // PTHREAD_OSX_H_
