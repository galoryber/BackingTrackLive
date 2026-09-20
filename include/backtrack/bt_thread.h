/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * The smallest portable thread primitives this project needs.
 *
 * C11 <threads.h> would be the obvious answer and is not usable: MSVC has
 * never shipped it, and it is optional in the standard. pthreads and the Win32
 * API between them cover every platform this targets, behind about eighty
 * lines.
 *
 * Note this is not a device dependency: libbacktrack still has no audio
 * backend in it, which is the property that keeps the engine testable
 * anywhere. Threads are merely an operating system.
 */
#ifndef BT_THREAD_H
#define BT_THREAD_H

#include "bt_types.h"
#include "bt_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bt_thread bt_thread;
typedef struct bt_mutex  bt_mutex;
typedef struct bt_cond   bt_cond;

typedef void (*bt_thread_fn)(void *user);

bt_err bt_thread_start(bt_thread **out, bt_thread_fn fn, void *user);
void   bt_thread_join(bt_thread *t);          /* also frees the handle */

bt_err bt_mutex_create(bt_mutex **out);
void   bt_mutex_destroy(bt_mutex *m);
void   bt_mutex_lock(bt_mutex *m);
void   bt_mutex_unlock(bt_mutex *m);

bt_err bt_cond_create(bt_cond **out);
void   bt_cond_destroy(bt_cond *c);
void   bt_cond_wait(bt_cond *c, bt_mutex *m);
/* Returns false on timeout. */
bool   bt_cond_wait_ms(bt_cond *c, bt_mutex *m, int32_t ms);
void   bt_cond_signal(bt_cond *c);
void   bt_cond_broadcast(bt_cond *c);

void   bt_thread_sleep_ms(int32_t ms);

#ifdef __cplusplus
}
#endif
#endif /* BT_THREAD_H */
