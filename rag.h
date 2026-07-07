#ifndef RAG_H
#define RAG_H

#include <windows.h>

#define N_THREADS 4
#define N_RESOURCES 3

// Thread States
#define THREAD_STATE_RUNNING 0
#define THREAD_STATE_WAITING 1
#define THREAD_STATE_PREEMPTED 2

// Allocation Policies
#define POLICY_NAIVE 0
#define POLICY_BANKER 1

// Resource Allocation Graph Structure
typedef struct {
    int allocation[N_THREADS][N_RESOURCES];
    int request[N_THREADS][N_RESOURCES];
    int max_need[N_THREADS][N_RESOURCES];
    int need[N_THREADS][N_RESOURCES];
    int available[N_RESOURCES];
    int total_units[N_RESOURCES];
} RAG_t;

// Global Synchronization Variables
extern RAG_t rag;
extern CRITICAL_SECTION scheduler_cs;
extern HANDLE thread_events[N_THREADS];
extern int preempt_flags[N_THREADS];
extern int thread_states[N_THREADS];
extern int current_policy;

// API Prototypes
void rag_init(void);
int check_banker_safety(int thread_id, int res_id);
int detect_deadlock_cycles(int *cycle_path, int max_path_len);
void print_rag_state(void);
void release_all_held_resources(int thread_id);

#endif // RAG_H
