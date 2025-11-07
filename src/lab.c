#include "lab.h"
#include <pthread.h>
#include <stdlib.h>

struct queue {
    void **buf;
    int capacity;
    int head;
    int tail;
    int count;
    bool shutdown;
    pthread_mutex_t mtx;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
};

/**
 * @brief Initialize a bounded FIFO queue (capacity <= 0 becomes 1).
 * AI Use: Written By AI
 */
queue_t queue_init(int capacity) {
    if (capacity <= 0) capacity = 1;
    struct queue *q = (struct queue *)malloc(sizeof(struct queue));
    if (!q) return NULL;
    q->buf = (void **)malloc(sizeof(void *) * (size_t)(capacity));
    if (!q->buf) { //GCOVR_EXCL_START
        free(q);
        return NULL;
    } //GCOVR_EXCL_STOP
    q->capacity = capacity;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->shutdown = false;
    pthread_mutex_init(&q->mtx, NULL);
    pthread_cond_init(&q->not_full, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    return q;
}

/**
 * @brief Destroy the queue, wake waiters, and free all resources.
 * AI Use: Written By AI
 */
void queue_destroy(queue_t q) {
    if (!q) return;
    pthread_mutex_lock(&q->mtx);
    q->shutdown = true;
    pthread_cond_broadcast(&q->not_full);
    pthread_cond_broadcast(&q->not_empty);
    pthread_mutex_unlock(&q->mtx);
    pthread_cond_destroy(&q->not_full);
    pthread_cond_destroy(&q->not_empty);
    pthread_mutex_destroy(&q->mtx);
    free(q->buf);
    free(q);
}

/**
 * @brief Enqueue one element; block if full until space or shutdown.
 * AI Use: Written By AI
 */
void enqueue(queue_t q, void *data) {
    if (!q) return;
    pthread_mutex_lock(&q->mtx);
    while (!q->shutdown && q->count == q->capacity) {
        pthread_cond_wait(&q->not_full, &q->mtx);
    }
    if (!q->shutdown) {
        q->buf[q->tail] = data;
        q->tail = (q->tail + 1) % q->capacity;
        q->count++;
        pthread_cond_signal(&q->not_empty);
    }
    pthread_mutex_unlock(&q->mtx);
}

/**
 * @brief Dequeue one element; block if empty; return NULL on shutdown+empty.
 * AI Use: Written By AI
 */
void *dequeue(queue_t q) {
    if (!q) return NULL;
    pthread_mutex_lock(&q->mtx);
    while (!q->shutdown && q->count == 0) {
        pthread_cond_wait(&q->not_empty, &q->mtx);
    }
    if (q->count == 0 && q->shutdown) {
        pthread_mutex_unlock(&q->mtx);
        return NULL;
    }
    void *data = q->buf[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mtx);
    return data;
}

/**
 * @brief Set shutdown flag and wake all waiting threads.
 * AI Use: Written By AI
 */
void queue_shutdown(queue_t q) {
    if (!q) return;
    pthread_mutex_lock(&q->mtx);
    q->shutdown = true;
    pthread_cond_broadcast(&q->not_full);
    pthread_cond_broadcast(&q->not_empty);
    pthread_mutex_unlock(&q->mtx);
}

/**
 * @brief Return true if the queue is empty at call time (NULL => true).
 * AI Use: Written By AI
 */
bool is_empty(queue_t q) {
    if (!q) return true;
    pthread_mutex_lock(&q->mtx);
    bool v = (q->count == 0);
    pthread_mutex_unlock(&q->mtx);
    return v;
}

/**
 * @brief Return true if the queue is shutdown (NULL => true).
 * AI Use: Written By AI
 */
bool is_shutdown(queue_t q) { //GCOVR_EXCL_START
    if (!q) return true;
    pthread_mutex_lock(&q->mtx);
    bool v = q->shutdown;
    pthread_mutex_unlock(&q->mtx);
    return v;
} //GCOVR_EXCL_STOP

