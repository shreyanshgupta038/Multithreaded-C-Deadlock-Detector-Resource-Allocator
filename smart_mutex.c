#include "smart_mutex.h"
#include "rag.h"
#include <stdio.h>

void smart_mutex_init(smart_mutex_t *mutex, int id) {
    mutex->id = id;
    mutex->owner = -1; // Initially free
}

// Request and acquire a lock with OS-level waiting & safety policy enforcement
int smart_mutex_lock(smart_mutex_t *mutex, int thread_id) {
    EnterCriticalSection(&scheduler_cs);

    // Register lock request in the RAG
    rag.request[thread_id][mutex->id] = 1;
    thread_states[thread_id] = THREAD_STATE_WAITING;

    printf("[REQUEST] Thread P%d requested Lock R%d\n", thread_id, mutex->id);

    while (1) {
        // Cooperative preemption check (Did background detector choose this thread as a victim?)
        if (preempt_flags[thread_id]) {
            // Clean up request edge
            rag.request[thread_id][mutex->id] = 0;
            thread_states[thread_id] = THREAD_STATE_PREEMPTED;
            
            printf("[PREEMPT] Thread P%d was preempted while waiting for Lock R%d. Rolling back...\n", thread_id, mutex->id);
            LeaveCriticalSection(&scheduler_cs);
            return SMART_MUTEX_PREEMPTED;
        }

        // Check if resource is currently free
        if (mutex->owner == -1) {
            // If Banker's Mode is active, check if allocating this lock preserves safety
            if (current_policy == POLICY_BANKER) {
                if (check_banker_safety(thread_id, mutex->id)) {
                    // Safe! Grant allocation
                    rag.request[thread_id][mutex->id] = 0;
                    rag.allocation[thread_id][mutex->id] = 1;
                    rag.available[mutex->id]--;
                    rag.need[thread_id][mutex->id]--;
                    mutex->owner = thread_id;
                    thread_states[thread_id] = THREAD_STATE_RUNNING;
                    
                    printf("[ALLOCATE] [BANKER] Granted Lock R%d to Thread P%d (Safe Sequence verified)\n", mutex->id, thread_id);
                    LeaveCriticalSection(&scheduler_cs);
                    return SMART_MUTEX_SUCCESS;
                } else {
                    // Unsafe! Thread must defer and block
                    // Wait silently, event will wake us up to try again
                }
            } else {
                // Naïve Mode: Grant lock immediately
                rag.request[thread_id][mutex->id] = 0;
                rag.allocation[thread_id][mutex->id] = 1;
                rag.available[mutex->id]--;
                rag.need[thread_id][mutex->id]--;
                mutex->owner = thread_id;
                thread_states[thread_id] = THREAD_STATE_RUNNING;
                
                printf("[ALLOCATE] [NAIVE] Granted Lock R%d to Thread P%d\n", mutex->id, thread_id);
                LeaveCriticalSection(&scheduler_cs);
                return SMART_MUTEX_SUCCESS;
            }
        }

        // Release monitor critical section and block on the thread-specific event
        LeaveCriticalSection(&scheduler_cs);
        WaitForSingleObject(thread_events[thread_id], INFINITE);
        EnterCriticalSection(&scheduler_cs); // Re-acquire critical section to check conditions
    }
}

// Release ownership of the lock
void smart_mutex_unlock(smart_mutex_t *mutex, int thread_id) {
    EnterCriticalSection(&scheduler_cs);

    if (mutex->owner == thread_id) {
        // Release allocation in the RAG
        rag.allocation[thread_id][mutex->id] = 0;
        rag.available[mutex->id]++;
        rag.need[thread_id][mutex->id]++;
        mutex->owner = -1;

        printf("[RELEASE] Thread P%d released Lock R%d\n", thread_id, mutex->id);

        // Wake up all threads currently waiting for resources to re-verify their conditions
        for (int p = 0; p < N_THREADS; p++) {
            if (thread_states[p] == THREAD_STATE_WAITING) {
                SetEvent(thread_events[p]);
            }
        }
    }

    LeaveCriticalSection(&scheduler_cs);
}
