/**
 * ================================================================
 *  MiniOS 进程同步原语实现
 *
 *  信号量：基于 block/wakeup 的 P/V 操作
 *  互斥锁：带所有权检查的排他锁
 * ================================================================ */

#include "hal.h"
#include "sync.h"
#include "sched.h"

/* ═══════════════════════════════════════════════════════════════
 *  信号量 (Semaphore)
 * ═══════════════════════════════════════════════════════════════ */

void sem_init(sem_t *sem, int initial_value) {
    sem->value      = initial_value;
    sem->wait_count = 0;
    memset(sem->wait_queue, 0, sizeof(sem->wait_queue));
}

/**
 * sem_wait — P 操作 (Proberen, "尝试")
 *
 * if value > 0: value--, 继续执行
 * else: 将自己加入等待队列, BLOCK
 */
void sem_wait(sem_t *sem) {
    if (sem->value > 0) {
        sem->value--;
        printf("[sem] wait: value=%d (acquired)\n", sem->value);
    } else {
        /* 资源不足 → 阻塞当前进程 */
        process_t *cur = process_get_current();
        if (cur == NULL) return;

        /* 加入等待队列 */
        if (sem->wait_count < MAX_PROCESSES) {
            sem->wait_queue[sem->wait_count++] = cur->pid;
        }

        printf("[sem] wait: value=%d, pid=%d BLOCKED (waiting...)\n",
               sem->value, cur->pid);

        process_block(cur->pid);
        schedule();
        /* 被唤醒后继续（value 已被 sem_post 预减） */
    }
}

/**
 * sem_post — V 操作 (Verhogen, "增加")
 *
 * if 有等待者: 唤醒队列中第一个
 * else: value++
 */
void sem_post(sem_t *sem) {
    if (sem->wait_count > 0) {
        /* 唤醒等待队列中的第一个进程 */
        int waking_pid = sem->wait_queue[0];

        /* 队列前移 */
        for (int i = 0; i < sem->wait_count - 1; i++) {
            sem->wait_queue[i] = sem->wait_queue[i + 1];
        }
        sem->wait_count--;

        printf("[sem] post: waking pid=%d (wait_count=%d)\n",
               waking_pid, sem->wait_count);

        sched_wakeup(waking_pid);
    } else {
        sem->value++;
        printf("[sem] post: value=%d (no waiters)\n", sem->value);
    }
}

int sem_get_value(sem_t *sem) {
    return sem->value;
}


/* ═══════════════════════════════════════════════════════════════
 *  互斥锁 (Mutex)
 * ═══════════════════════════════════════════════════════════════ */

void mutex_init(mutex_t *m) {
    m->locked     = 0;
    m->owner_pid  = -1;
    m->wait_count = 0;
    memset(m->wait_queue, 0, sizeof(m->wait_queue));
}

/**
 * mutex_lock — 获取互斥锁
 *
 * 如果已锁且持有者就是当前进程 → 死锁检测（拒绝）
 * 如果未锁 → 加锁, 记录所有者
 * 如果已锁 → BLOCK
 */
void mutex_lock(mutex_t *m) {
    process_t *cur = process_get_current();
    if (cur == NULL) return;

    /* 死锁检测：不能重复加锁 */
    if (m->locked && m->owner_pid == cur->pid) {
        printf("[mutex] DEADLOCK: pid=%d tried to lock mutex "
               "it already owns!\n", cur->pid);
        return;
    }

    if (!m->locked) {
        m->locked    = 1;
        m->owner_pid = cur->pid;
        printf("[mutex] lock: acquired by pid=%d\n", cur->pid);
    } else {
        /* 已被别人持有 → 阻塞 */
        if (m->wait_count < MAX_PROCESSES) {
            m->wait_queue[m->wait_count++] = cur->pid;
        }
        printf("[mutex] lock: held by pid=%d, pid=%d BLOCKED\n",
               m->owner_pid, cur->pid);
        process_block(cur->pid);
        schedule();
        /* 被唤醒后拥有锁 */
        m->locked    = 1;
        m->owner_pid = cur->pid;
    }
}

/**
 * mutex_unlock — 释放互斥锁
 *
 * 检查调用者是否是持有者 → 不是则拒绝
 * 如果有等待者 → 唤醒第一个
 * 否则 → 解锁
 */
void mutex_unlock(mutex_t *m) {
    process_t *cur = process_get_current();
    if (cur == NULL) return;

    /* 所有权检查 */
    if (m->owner_pid != cur->pid) {
        printf("[mutex] unlock: pid=%d does NOT own this mutex "
               "(owner=%d)\n", cur->pid, m->owner_pid);
        return;
    }

    if (m->wait_count > 0) {
        /* 直接转移给等待者（不需要先解锁再加锁） */
        int next_pid = m->wait_queue[0];
        for (int i = 0; i < m->wait_count - 1; i++) {
            m->wait_queue[i] = m->wait_queue[i + 1];
        }
        m->wait_count--;
        m->owner_pid = next_pid;

        printf("[mutex] unlock: transferred to pid=%d "
               "(wait_count=%d)\n", next_pid, m->wait_count);

        sched_wakeup(next_pid);
    } else {
        m->locked    = 0;
        m->owner_pid = -1;
        printf("[mutex] unlock: released by pid=%d\n", cur->pid);
    }
}

/**
 * mutex_trylock — 非阻塞尝试加锁
 *
 * 成功返回 1，失败返回 0（不阻塞）。
 */
int mutex_trylock(mutex_t *m) {
    process_t *cur = process_get_current();
    if (cur == NULL) return 0;

    if (!m->locked) {
        m->locked    = 1;
        m->owner_pid = cur->pid;
        printf("[mutex] trylock: acquired by pid=%d\n", cur->pid);
        return 1;
    }

    printf("[mutex] trylock: failed (held by pid=%d)\n",
           m->owner_pid);
    return 0;
}
