/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "backtrack/bt_thread.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #include <windows.h>

struct bt_thread { HANDLE h; bt_thread_fn fn; void *user; };
struct bt_mutex  { CRITICAL_SECTION cs; };
struct bt_cond   { CONDITION_VARIABLE cv; };

static DWORD WINAPI win_trampoline(LPVOID p) {
    bt_thread *t = (bt_thread *)p;
    t->fn(t->user);
    return 0;
}

bt_err bt_thread_start(bt_thread **out, bt_thread_fn fn, void *user) {
    if (!out || !fn) return BT_ERR_RANGE;
    bt_thread *t = (bt_thread *)calloc(1, sizeof(*t));
    if (!t) return BT_ERR_ALLOC;
    t->fn = fn; t->user = user;
    t->h = CreateThread(NULL, 0, win_trampoline, t, 0, NULL);
    if (!t->h) { free(t); return BT_ERR_STATE; }
    *out = t;
    return BT_OK;
}

void bt_thread_join(bt_thread *t) {
    if (!t) return;
    WaitForSingleObject(t->h, INFINITE);
    CloseHandle(t->h);
    free(t);
}

bt_err bt_mutex_create(bt_mutex **out) {
    if (!out) return BT_ERR_RANGE;
    bt_mutex *m = (bt_mutex *)calloc(1, sizeof(*m));
    if (!m) return BT_ERR_ALLOC;
    InitializeCriticalSection(&m->cs);
    *out = m;
    return BT_OK;
}
void bt_mutex_destroy(bt_mutex *m) {
    if (!m) return;
    DeleteCriticalSection(&m->cs);
    free(m);
}
void bt_mutex_lock(bt_mutex *m)   { if (m) EnterCriticalSection(&m->cs); }
void bt_mutex_unlock(bt_mutex *m) { if (m) LeaveCriticalSection(&m->cs); }

bt_err bt_cond_create(bt_cond **out) {
    if (!out) return BT_ERR_RANGE;
    bt_cond *c = (bt_cond *)calloc(1, sizeof(*c));
    if (!c) return BT_ERR_ALLOC;
    InitializeConditionVariable(&c->cv);
    *out = c;
    return BT_OK;
}
void bt_cond_destroy(bt_cond *c) { free(c); }
void bt_cond_wait(bt_cond *c, bt_mutex *m) {
    SleepConditionVariableCS(&c->cv, &m->cs, INFINITE);
}
bool bt_cond_wait_ms(bt_cond *c, bt_mutex *m, int32_t ms) {
    return SleepConditionVariableCS(&c->cv, &m->cs, (DWORD)ms) != 0;
}
void bt_cond_signal(bt_cond *c)    { WakeConditionVariable(&c->cv); }
void bt_cond_broadcast(bt_cond *c) { WakeAllConditionVariable(&c->cv); }
void bt_thread_sleep_ms(int32_t ms) { if (ms > 0) Sleep((DWORD)ms); }

#else
  #include <pthread.h>
  #include <time.h>
  #include <errno.h>

struct bt_thread { pthread_t h; bt_thread_fn fn; void *user; };
struct bt_mutex  { pthread_mutex_t m; };
struct bt_cond   { pthread_cond_t c; };

static void *posix_trampoline(void *p) {
    bt_thread *t = (bt_thread *)p;
    t->fn(t->user);
    return NULL;
}

bt_err bt_thread_start(bt_thread **out, bt_thread_fn fn, void *user) {
    if (!out || !fn) return BT_ERR_RANGE;
    bt_thread *t = (bt_thread *)calloc(1, sizeof(*t));
    if (!t) return BT_ERR_ALLOC;
    t->fn = fn; t->user = user;
    if (pthread_create(&t->h, NULL, posix_trampoline, t) != 0) {
        free(t);
        return BT_ERR_STATE;
    }
    *out = t;
    return BT_OK;
}

void bt_thread_join(bt_thread *t) {
    if (!t) return;
    pthread_join(t->h, NULL);
    free(t);
}

bt_err bt_mutex_create(bt_mutex **out) {
    if (!out) return BT_ERR_RANGE;
    bt_mutex *m = (bt_mutex *)calloc(1, sizeof(*m));
    if (!m) return BT_ERR_ALLOC;
    if (pthread_mutex_init(&m->m, NULL) != 0) { free(m); return BT_ERR_STATE; }
    *out = m;
    return BT_OK;
}
void bt_mutex_destroy(bt_mutex *m) {
    if (!m) return;
    pthread_mutex_destroy(&m->m);
    free(m);
}
void bt_mutex_lock(bt_mutex *m)   { if (m) pthread_mutex_lock(&m->m); }
void bt_mutex_unlock(bt_mutex *m) { if (m) pthread_mutex_unlock(&m->m); }

bt_err bt_cond_create(bt_cond **out) {
    if (!out) return BT_ERR_RANGE;
    bt_cond *c = (bt_cond *)calloc(1, sizeof(*c));
    if (!c) return BT_ERR_ALLOC;
    if (pthread_cond_init(&c->c, NULL) != 0) { free(c); return BT_ERR_STATE; }
    *out = c;
    return BT_OK;
}
void bt_cond_destroy(bt_cond *c) {
    if (!c) return;
    pthread_cond_destroy(&c->c);
    free(c);
}
void bt_cond_wait(bt_cond *c, bt_mutex *m) { pthread_cond_wait(&c->c, &m->m); }

bool bt_cond_wait_ms(bt_cond *c, bt_mutex *m, int32_t ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    return pthread_cond_timedwait(&c->c, &m->m, &ts) != ETIMEDOUT;
}
void bt_cond_signal(bt_cond *c)    { if (c) pthread_cond_signal(&c->c); }
void bt_cond_broadcast(bt_cond *c) { if (c) pthread_cond_broadcast(&c->c); }

void bt_thread_sleep_ms(int32_t ms) {
    if (ms <= 0) return;
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#endif
