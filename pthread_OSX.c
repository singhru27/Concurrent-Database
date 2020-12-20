#include <pthread.h>
#include <pthread_OSX.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * This files contains impletmentations of rwlocks and barriers that are
 * compiled only if the underlying pthreads implementation doesn't
 * support them.
 */

#ifdef NEED_RWLOCK
int pthread_rwlock_init(pthread_rwlock_t *lock, pthread_rwlockattr_t *att) {
    pthread_mutex_init(&lock->mutex, 0);
    pthread_cond_init(&lock->readersQ, 0);
    pthread_cond_init(&lock->writersQ, 0);
    lock->readers = 0;
    lock->writers = 0;
    lock->active_writers = 0;
    return (0);
}

int pthread_rwlock_destroy(pthread_rwlock_t *lock) {
    pthread_mutex_destroy(&lock->mutex);
    pthread_cond_destroy(&lock->readersQ);
    pthread_cond_destroy(&lock->writersQ);
    return (0);
}

int pthread_rwlock_rdlock(pthread_rwlock_t *lock) {
    pthread_mutex_lock(&lock->mutex);
    while (writers != 0) {
        pthread_cond_wait(&lock->readersQ, &lock->mutex);
    }
    lock->readers++;
    pthread_mutex_unlock(&lock->mutex);
    return (0);
}

int pthread_rwlock_wrlock(pthread_rwlock_t *lock) {
    pthread_mutex_lock(&lock->mutex);
    while ((readers != 0) && (writers != 0)) {
        pthread_cond_wait(&lock->writersQ, &lock->mutex);
    }
    lock->active_writers++;
    pthread_mutex_unlock(&lock->mutex);
    return (0);
}

int pthread_rwlock_unlock(pthread_rwlock_t *lock) {
    pthread_mutex_lock(&lock->mutex);
    if (lock->active_writers) {
        lock->writers--;
        lock->active_writers--;
        if (lock->writers)
            pthread_cond_signal(&lock->writersQ);
        else
            pthread_cond_broadcast(&lock->readersQ);
    } else {
        if (--lock->readers == 0) pthread_cond_signal(&lock->writersQ);
    }
    pthread_mutex_unlock(&lock->mutex);
    return (0);
}
#endif

#ifdef NEED_BARRIER
int pthread_barrier_init(pthread_barrier_t *barrier, pthread_barrierattr_t *att,
                         unsigned int count) {
    pthread_mutex_init(&barrier->mutex, 0);
    pthread_cond_init(&barrier->cv, 0);
    barrier->generation = 0;
    barrier->count = 0;
    barrier->number = count;
    return (0);
}

int pthread_barrier_destroy(pthread_barrier_t *barrier) {
    pthread_mutex_destroy(&barrier->mutex);
    pthread_cond_destroy(&barrier->cv);
    return (0);
}

int pthread_barrier_wait(pthread_barrier_t *barrier) {
    int ret;
    pthread_mutex_lock(&barrier->mutex);
    if (++barrier->count < barrier->number) {
        int my_generation = barrier->generation;
        while (my_generation == barrier->generation) {
            pthread_cond_wait(&barrier->cv, &barrier->mutex);
        }
        ret = 0;
    } else {
        barrier->count = 0;
        barrier->generation++;
        pthread_cond_broadcast(&barrier->cv);
        ret = PTHREAD_BARRIER_SERIAL_THREAD;
    }
    pthread_mutex_unlock(&barrier->mutex);
    return (ret);
}
#endif
