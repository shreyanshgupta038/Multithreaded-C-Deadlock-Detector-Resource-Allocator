#include "rag.h"
#include <stdio.h>
#include <string.h>

RAG_t rag;
CRITICAL_SECTION scheduler_cs;
HANDLE thread_events[N_THREADS];
int preempt_flags[N_THREADS];
int thread_states[N_THREADS];
int current_policy = POLICY_NAIVE;

// Initialize the Resource Allocation Graph and OS locks
void rag_init(void) {
    InitializeCriticalSection(&scheduler_cs);

    memset(&rag, 0, sizeof(RAG_t));
    
    // Set resource capacities (1 unit per lock type - representing standard mutex locks)
    for (int r = 0; r < N_RESOURCES; r++) {
        rag.total_units[r] = 1;
        rag.available[r] = 1;
    }

    // Set thread default states
    for (int p = 0; p < N_THREADS; p++) {
        thread_states[p] = THREAD_STATE_RUNNING;
        preempt_flags[p] = 0;
        
        // Create manual-reset events for each thread
        thread_events[p] = CreateEvent(
            NULL,               // default security attributes
            FALSE,              // auto-reset event (switches to non-signaled automatically)
            FALSE,              // initial state is non-signaled
            NULL                // object name
        );
    }
}

// Banker's Algorithm safety check
// Returns 1 if safe, 0 if unsafe
int check_banker_safety(int thread_id, int res_id) {
    // 1. Hypothetically allocate the resource
    rag.available[res_id]--;
    rag.allocation[thread_id][res_id]++;
    rag.need[thread_id][res_id]--;

    // 2. Initialize Work = Available, Finish = false
    int work[N_RESOURCES];
    for (int r = 0; r < N_RESOURCES; r++) {
        work[r] = rag.available[r];
    }

    int finish[N_THREADS];
    for (int p = 0; p < N_THREADS; p++) {
        finish[p] = 0;
    }

    // 3. Find an unfinished process whose needs can be met by Work
    int finished_count = 0;
    int found = 1;
    while (found) {
        found = 0;
        for (int p = 0; p < N_THREADS; p++) {
            if (!finish[p]) {
                // Check if Need_p <= Work
                int can_finish = 1;
                for (int r = 0; r < N_RESOURCES; r++) {
                    if (rag.need[p][r] > work[r]) {
                        can_finish = 0;
                        break;
                    }
                }

                if (can_finish) {
                    // Work = Work + Allocation_p
                    for (int r = 0; r < N_RESOURCES; r++) {
                        work[r] += rag.allocation[p][r];
                    }
                    finish[p] = 1;
                    finished_count++;
                    found = 1;
                    break; // Restart loop to check dependencies
                }
            }
        }
    }

    // 4. Restore actual RAG state
    rag.available[res_id]++;
    rag.allocation[thread_id][res_id]--;
    rag.need[thread_id][res_id]++;

    // If all processes finished, state is safe
    return (finished_count == N_THREADS);
}

// Recursive DFS helper to find a cycle in the bipartite RAG
static int dfs(int u, int *visited, int *stack, int *stack_ptr, int *cycle_path, int max_path_len) {
    visited[u] = 1; // GRAY (currently visiting)
    stack[(*stack_ptr)++] = u;

    if (u < N_THREADS) {
        // u is a Process. Neighbors are Resources it is waiting on (Request edges P -> R)
        int p = u;
        for (int r = 0; r < N_RESOURCES; r++) {
            if (rag.request[p][r] > 0) {
                int v = N_THREADS + r; // Resource node index
                if (visited[v] == 1) { // Found a back edge!
                    // Extract cycle from recursion stack
                    int start_idx = 0;
                    while (start_idx < *stack_ptr && stack[start_idx] != v) {
                        start_idx++;
                    }
                    int len = *stack_ptr - start_idx;
                    for (int k = 0; k < len && k < max_path_len; k++) {
                        cycle_path[k] = stack[start_idx + k];
                    }
                    return len;
                } else if (visited[v] == 0) {
                    int len = dfs(v, visited, stack, stack_ptr, cycle_path, max_path_len);
                    if (len > 0) return len;
                }
            }
        }
    } else {
        // u is a Resource. Neighbors are Processes holding it (Allocation edges R -> P)
        int r = u - N_THREADS;
        for (int p = 0; p < N_THREADS; p++) {
            if (rag.allocation[p][r] > 0) {
                int v = p; // Process node index
                if (visited[v] == 1) { // Found a back edge!
                    int start_idx = 0;
                    while (start_idx < *stack_ptr && stack[start_idx] != v) {
                        start_idx++;
                    }
                    int len = *stack_ptr - start_idx;
                    for (int k = 0; k < len && k < max_path_len; k++) {
                        cycle_path[k] = stack[start_idx + k];
                    }
                    return len;
                } else if (visited[v] == 0) {
                    int len = dfs(v, visited, stack, stack_ptr, cycle_path, max_path_len);
                    if (len > 0) return len;
                }
            }
        }
    }

    // Backtrack
    (*stack_ptr)--;
    visited[u] = 2; // BLACK (fully processed)
    return 0;
}

// Detect cycles in the RAG
// Returns length of the cycle path, and populates cycle_path array.
// Returns 0 if acyclic.
int detect_deadlock_cycles(int *cycle_path, int max_path_len) {
    int total_nodes = N_THREADS + N_RESOURCES;
    int visited[N_THREADS + N_RESOURCES];
    memset(visited, 0, sizeof(visited));

    int stack[N_THREADS + N_RESOURCES];
    int stack_ptr = 0;

    for (int i = 0; i < total_nodes; i++) {
        if (visited[i] == 0) {
            int len = dfs(i, visited, stack, &stack_ptr, cycle_path, max_path_len);
            if (len > 0) return len;
        }
    }
    return 0;
}



// Print active matrices in a clean dashboard style
void print_rag_state(void) {
    printf("\n======================= SYSTEM STATUS MATRIX =======================\n");
    printf("Policy: %s\n", current_policy == POLICY_BANKER ? "BANKER'S AVOIDANCE" : "NAIVE SCHEDULING");
    printf("--------------------------------------------------------------------\n");
    printf("Thread |   State    | Allocation |    Need    |    Max     | Request \n");
    printf("-------+------------+------------+------------+------------+---------\n");
    
    for (int p = 0; p < N_THREADS; p++) {
        const char *state_str = "RUNNING";
        if (thread_states[p] == THREAD_STATE_WAITING) state_str = "WAITING";
        if (thread_states[p] == THREAD_STATE_PREEMPTED) state_str = "PREEMPTED";
        
        printf("  P%d   | %-10s | ", p, state_str);
        
        // Allocation vector
        printf("(");
        for (int r = 0; r < N_RESOURCES; r++) {
            printf("%d%s", rag.allocation[p][r], r == N_RESOURCES-1 ? "" : ", ");
        }
        printf(")      | (");

        // Need vector
        for (int r = 0; r < N_RESOURCES; r++) {
            printf("%d%s", rag.need[p][r], r == N_RESOURCES-1 ? "" : ", ");
        }
        printf(")      | (");

        // Max vector
        for (int r = 0; r < N_RESOURCES; r++) {
            printf("%d%s", rag.max_need[p][r], r == N_RESOURCES-1 ? "" : ", ");
        }
        printf(")      | (");

        // Active request vector
        for (int r = 0; r < N_RESOURCES; r++) {
            printf("%d%s", rag.request[p][r], r == N_RESOURCES-1 ? "" : ", ");
        }
        printf(")\n");
    }
    printf("-------+------------+------------+------------+------------+---------\n");
    printf("Available Resource Vector: (");
    for (int r = 0; r < N_RESOURCES; r++) {
        printf("%d%s", rag.available[r], r == N_RESOURCES-1 ? "" : ", ");
    }
    printf(")\n");
    printf("====================================================================\n\n");
}
