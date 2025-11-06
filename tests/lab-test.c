#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include "harness/unity.h"
#include "../src/lab.h"

static void msleep(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

typedef struct {
    queue_t q;
    void **outp;
} dq_args_t;

typedef struct {
    queue_t q;
    void *val;
} enq_args_t;

static void *th_dequeue_capture(void *arg) {
    dq_args_t *a = (dq_args_t *)arg;
    void *v = dequeue(a->q);
    if (a->outp) *a->outp = v;
    return NULL;
}

static void *th_enqueue_value(void *arg) {
    enq_args_t *a = (enq_args_t *)arg;
    enqueue(a->q, a->val);
    return NULL;
}

typedef struct {
    queue_t q;
    int base;
    int nper;
    int *produced;
    pthread_mutex_t *mtx;
} pctx_t;

typedef struct {
    queue_t q;
    int need;
    int *consumed;
    pthread_mutex_t *mtx;
} cctx_t;

static void *prod_fn(void *arg) {
    pctx_t *c = (pctx_t *)arg;
    for (int i = 0; i < c->nper; i++) {
        int *v = (int *)malloc(sizeof(int));
        *v = c->base + i;
        enqueue(c->q, v);
        pthread_mutex_lock(c->mtx);
        (*c->produced)++;
        pthread_mutex_unlock(c->mtx);
    }
    return NULL;
}

static void *cons_fn(void *arg) {
    cctx_t *c = (cctx_t *)arg;
    int got = 0;
    while (got < c->need) {
        void *p = dequeue(c->q);
        if (p == NULL && is_shutdown(c->q)) break;
        if (p) {
            free(p);
            got++;
            pthread_mutex_lock(c->mtx);
            (*c->consumed)++;
            pthread_mutex_unlock(c->mtx);
        } else {
            msleep(1);
        }
    }
    return NULL;
}

void setUp(void) {
    printf("Setting up tests...\n");
}

void tearDown(void) {
    printf("Tearing down tests...\n");
}

void test_init_with_zero_capacity_behaves_valid(void) {
    queue_t q = queue_init(0);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_TRUE(is_empty(q));
    int v = 42;
    enqueue(q, &v);
    TEST_ASSERT_FALSE(is_empty(q));
    void *out = dequeue(q);
    TEST_ASSERT_EQUAL_PTR(&v, out);
    TEST_ASSERT_TRUE(is_empty(q));
    queue_destroy(q);
}

void test_fifo_order_and_wraparound(void) {
    queue_t q = queue_init(3);
    int v[6] = {1,2,3,4,5,6};
    enqueue(q, &v[0]);
    enqueue(q, &v[1]);
    enqueue(q, &v[2]);
    TEST_ASSERT_FALSE(is_empty(q));
    TEST_ASSERT_EQUAL_PTR(&v[0], dequeue(q));
    enqueue(q, &v[3]);
    TEST_ASSERT_EQUAL_PTR(&v[1], dequeue(q));
    TEST_ASSERT_EQUAL_PTR(&v[2], dequeue(q));
    TEST_ASSERT_EQUAL_PTR(&v[3], dequeue(q));
    TEST_ASSERT_TRUE(is_empty(q));
    enqueue(q, &v[4]);
    enqueue(q, &v[5]);
    TEST_ASSERT_EQUAL_PTR(&v[4], dequeue(q));
    TEST_ASSERT_EQUAL_PTR(&v[5], dequeue(q));
    TEST_ASSERT_TRUE(is_empty(q));
    queue_destroy(q);
}

void test_is_empty_reflects_state_with_interleaving(void) {
    queue_t q = queue_init(2);
    int a = 7, b = 8;
    TEST_ASSERT_TRUE(is_empty(q));
    enqueue(q, &a);
    TEST_ASSERT_FALSE(is_empty(q));
    enqueue(q, &b);
    TEST_ASSERT_FALSE(is_empty(q));
    (void)dequeue(q);
    TEST_ASSERT_FALSE(is_empty(q));
    (void)dequeue(q);
    TEST_ASSERT_TRUE(is_empty(q));
    queue_destroy(q);
}

void test_dequeue_blocks_until_item_enqueued(void) {
    queue_t q = queue_init(2);
    void *captured = (void *)0xDEAD;
    dq_args_t args = { .q = q, .outp = &captured };
    pthread_t t;
    pthread_create(&t, NULL, th_dequeue_capture, &args);
    msleep(100);
    TEST_ASSERT_EQUAL_PTR((void *)0xDEAD, captured);
    int v = 99;
    enqueue(q, &v);
    pthread_join(t, NULL);
    TEST_ASSERT_EQUAL_PTR(&v, captured);
    queue_destroy(q);
}

void test_enqueue_blocks_when_full_until_space_available(void) {
    queue_t q = queue_init(1);
    int a = 1, b = 2;
    enqueue(q, &a);
    enq_args_t args = { .q = q, .val = &b };
    pthread_t t;
    pthread_create(&t, NULL, th_enqueue_value, &args);
    msleep(100);
    void *out = dequeue(q);
    TEST_ASSERT_EQUAL_PTR(&a, out);
    pthread_join(t, NULL);
    out = dequeue(q);
    TEST_ASSERT_EQUAL_PTR(&b, out);
    TEST_ASSERT_TRUE(is_empty(q));
    queue_destroy(q);
}

void test_shutdown_makes_blocked_dequeue_return_null(void) {
    queue_t q = queue_init(2);
    void *captured = (void *)0xBEEF;
    dq_args_t args = { .q = q, .outp = &captured };
    pthread_t t;
    pthread_create(&t, NULL, th_dequeue_capture, &args);
    msleep(100);
    queue_shutdown(q);
    pthread_join(t, NULL);
    TEST_ASSERT_NULL(captured);
    queue_destroy(q);
}

void test_shutdown_unblocks_enqueue_and_prevents_insertion(void) {
    queue_t q = queue_init(1);
    int a = 1, b = 2;
    enqueue(q, &a);
    enq_args_t args = { .q = q, .val = &b };
    pthread_t t;
    pthread_create(&t, NULL, th_enqueue_value, &args);
    msleep(100);
    queue_shutdown(q);
    pthread_join(t, NULL);
    void *out = dequeue(q);
    TEST_ASSERT_EQUAL_PTR(&a, out);
    TEST_ASSERT_TRUE(is_empty(q));
    queue_destroy(q);
}

void test_enqueue_after_shutdown_is_noop_and_dequeue_returns_null_when_empty(void) {
    queue_t q = queue_init(4);
    queue_shutdown(q);
    int v = 3;
    enqueue(q, &v);
    TEST_ASSERT_TRUE(is_empty(q));
    void *out = dequeue(q);
    TEST_ASSERT_NULL(out);
    queue_destroy(q);
}

void test_multiple_producers_consumers_integrity(void) {
    queue_t q = queue_init(8);
    int produced = 0;
    int consumed = 0;
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    enum { NPROD = 4, NPER = 1000, NCONS = 4 };
    pthread_t prod[NPROD];
    pthread_t cons[NCONS];
    pctx_t pctxs[NPROD];
    cctx_t cctxs[NCONS];
    for (int i = 0; i < NPROD; i++) {
        pctxs[i].q = q;
        pctxs[i].base = i * 1000000;
        pctxs[i].nper = NPER;
        pctxs[i].produced = &produced;
        pctxs[i].mtx = &mtx;
        pthread_create(&prod[i], NULL, prod_fn, &pctxs[i]);
    }
    for (int i = 0; i < NCONS; i++) {
        cctxs[i].q = q;
        cctxs[i].need = (NPROD * NPER) / NCONS;
        cctxs[i].consumed = &consumed;
        cctxs[i].mtx = &mtx;
        pthread_create(&cons[i], NULL, cons_fn, &cctxs[i]);
    }
    for (int i = 0; i < NPROD; i++) pthread_join(prod[i], NULL);
    while (1) {
        pthread_mutex_lock(&mtx);
        int done = (consumed >= NPROD * NPER);
        pthread_mutex_unlock(&mtx);
        if (done) break;
        msleep(1);
    }
    queue_shutdown(q);
    for (int i = 0; i < NCONS; i++) pthread_join(cons[i], NULL);
    TEST_ASSERT_EQUAL_INT(NPROD * NPER, produced);
    TEST_ASSERT_EQUAL_INT(NPROD * NPER, consumed);
    TEST_ASSERT_TRUE(is_empty(q));
    queue_destroy(q);
}

void test_dequeue_from_empty_after_shutdown_is_immediate_null(void) {
    queue_t q = queue_init(2);
    queue_shutdown(q);
    void *v = dequeue(q);
    TEST_ASSERT_NULL(v);
    queue_destroy(q);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_with_zero_capacity_behaves_valid);
    RUN_TEST(test_fifo_order_and_wraparound);
    RUN_TEST(test_is_empty_reflects_state_with_interleaving);
    RUN_TEST(test_dequeue_blocks_until_item_enqueued);
    RUN_TEST(test_enqueue_blocks_when_full_until_space_available);
    RUN_TEST(test_shutdown_makes_blocked_dequeue_return_null);
    RUN_TEST(test_shutdown_unblocks_enqueue_and_prevents_insertion);
    RUN_TEST(test_enqueue_after_shutdown_is_noop_and_dequeue_returns_null_when_empty);
    RUN_TEST(test_multiple_producers_consumers_integrity);
    RUN_TEST(test_dequeue_from_empty_after_shutdown_is_immediate_null);
    return UNITY_END();
}

