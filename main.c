#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <windows.h>
#include <time.h>
#include "rag.h"
#include "smart_mutex.h"

// Global locks corresponding to Resources
smart_mutex_t locks[N_RESOURCES];
HANDLE worker_threads[N_THREADS];
HANDLE detector_thread;
int keep_running = 1;

// Helper: Shuffle an array of resource indices
void shuffle_resources(int *arr, int len) {
    for (int i = len - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = arr[i];
        arr[i] = arr[j];
        arr[j] = temp;
    }
}

// Release all resources allocated to a specific thread and reset lock ownerships
void release_all_held_resources(int thread_id) {
    for (int r = 0; r < N_RESOURCES; r++) {
        if (rag.allocation[thread_id][r] > 0) {
            rag.available[r] += rag.allocation[thread_id][r];
            rag.allocation[thread_id][r] = 0;
            locks[r].owner = -1; // Reset lock owner!
            printf("[RECOVERY] Revoked resource R%d from Thread P%d\n", r, thread_id);
        }
    }
}

// Worker Thread Function (simulating concurrent OS processes P0-P3)
DWORD WINAPI WorkerThreadFunc(LPVOID lpParam) {
    int thread_id = (int)(intptr_t)lpParam;
    srand((unsigned int)(time(NULL) + thread_id * 13));

    int res_indices[N_RESOURCES];
    for (int r = 0; r < N_RESOURCES; r++) {
        res_indices[r] = r;
    }

    while (keep_running) {
        // --- 1. ENTER TRANSACTION & DECLARE MAX CLAIMS ---
        EnterCriticalSection(&scheduler_cs);
        
        // Randomize maximum claims for resources (0 or 1 instances)
        int has_claims = 0;
        for (int r = 0; r < N_RESOURCES; r++) {
            // 70% chance to claim resource r, 30% chance not to
            rag.max_need[thread_id][r] = (rand() % 100 < 70) ? 1 : 0;
            if (rag.max_need[thread_id][r] > 0) {
                has_claims = 1;
            }
            rag.need[thread_id][r] = rag.max_need[thread_id][r];
            rag.allocation[thread_id][r] = 0;
            rag.request[thread_id][r] = 0;
        }

        // Guarantee at least one claim to make it compete
        if (!has_claims) {
            int lucky_r = rand() % N_RESOURCES;
            rag.max_need[thread_id][lucky_r] = 1;
            rag.need[thread_id][lucky_r] = 1;
        }

        thread_states[thread_id] = THREAD_STATE_RUNNING;
        preempt_flags[thread_id] = 0;

        LeaveCriticalSection(&scheduler_cs);

        // --- 2. ACQUIRE LOCKS IN RANDOM SEQUENCE ---
        int preempted = 0;
        shuffle_resources(res_indices, N_RESOURCES);

        for (int i = 0; i < N_RESOURCES; i++) {
            int r = res_indices[i];
            
            // Only request if we declared a max claim for it
            if (rag.max_need[thread_id][r] > 0) {
                int status = smart_mutex_lock(&locks[r], thread_id);
                
                if (status == SMART_MUTEX_PREEMPTED) {
                    preempted = 1;
                    break;
                }
                
                // Simulate quick computing/holding lock before requesting next resource
                Sleep(150 + rand() % 150);
            }
        }

        // --- 3. HANDLE PREEMPTION / ROLLBACK RECOVERY ---
        if (preempted) {
            // Cooperative recovery: release all locks already held to break the circular wait
            release_all_held_resources(thread_id);
            
            EnterCriticalSection(&scheduler_cs);
            preempt_flags[thread_id] = 0;
            thread_states[thread_id] = THREAD_STATE_RUNNING;
            LeaveCriticalSection(&scheduler_cs);
            
            printf("[ROLLBACK] Thread P%d fully rolled back and sleeping to prevent starvation.\n", thread_id);
            // Sleep for a randomized backoff period (500ms - 1500ms) before retrying transaction
            Sleep(500 + rand() % 1000);
            continue; // Loop back and restart transaction
        }

        // --- 4. EXECUTE CRITICAL SECTION ---
        printf("[SUCCESS] Thread P%d successfully acquired all declared locks! Executing critical section...\n", thread_id);
        Sleep(400 + rand() % 300); // Simulate critical section work

        // --- 5. RELEASE ALL LOCKS ---
        for (int r = 0; r < N_RESOURCES; r++) {
            if (rag.max_need[thread_id][r] > 0) {
                smart_mutex_unlock(&locks[r], thread_id);
            }
        }

        // Reset state between transactions
        EnterCriticalSection(&scheduler_cs);
        for (int r = 0; r < N_RESOURCES; r++) {
            rag.max_need[thread_id][r] = 0;
            rag.need[thread_id][r] = 0;
        }
        LeaveCriticalSection(&scheduler_cs);

        // Sleep before beginning next transaction
        Sleep(600 + rand() % 600);
    }

    return 0;
}

// Background Deadlock Detector Daemon Thread
DWORD WINAPI DetectorThreadFunc(LPVOID lpParam) {
    int cycle_path[N_THREADS + N_RESOURCES];

    while (keep_running) {
        // Sleep for periodic checks
        Sleep(2000);

        EnterCriticalSection(&scheduler_cs);

        // Print Allocation & Request Matrix
        print_rag_state();

        // Run cycle detection (DFS strongly connected components check)
        int cycle_len = detect_deadlock_cycles(cycle_path, N_THREADS + N_RESOURCES);
        
        if (cycle_len > 0) {
            printf("[DEADLOCK] DEADLOCK DETECTED! Circular Wait Cycle: ");
            for (int i = 0; i < cycle_len; i++) {
                int node = cycle_path[i];
                if (node < N_THREADS) {
                    printf("P%d", node);
                } else {
                    printf("R%d", node - N_THREADS);
                }
                printf("%s", i == cycle_len - 1 ? "" : " -> ");
            }
            printf(" -> ");
            // Print the wrapper node to complete loop representation
            int first_node = cycle_path[0];
            if (first_node < N_THREADS) printf("P%d\n", first_node);
            else printf("R%d\n", first_node - N_THREADS);

            if (current_policy == POLICY_NAIVE) {
                // Preemption Recovery
                // Pick the first process in the cycle path to preempt
                int victim_thread = -1;
                for (int i = 0; i < cycle_len; i++) {
                    if (cycle_path[i] < N_THREADS) {
                        victim_thread = cycle_path[i];
                        break;
                    }
                }

                if (victim_thread != -1) {
                    printf("[RECOVERY] Policy is NAIVE. Initiating cooperative preemption on victim Thread P%d.\n", victim_thread);
                    preempt_flags[victim_thread] = 1;
                    
                    // Signal the event in case the thread is currently sleeping/blocked inside smart_mutex_lock
                    SetEvent(thread_events[victim_thread]);
                }
            } else {
                printf("[WARNING] Deadlock occurred while in BANKER'S Mode. This indicates safety engine boundary error!\n");
            }
        } else {
            printf("[DETECTOR] Deadlock Scan: Clean (No cycles found).\n");
        }

        LeaveCriticalSection(&scheduler_cs);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    printf("====================================================================\n");
    printf("        MULTITHREADED C DEADLOCK DETECTOR & RESOURCE ALLOCATOR\n");
    printf("====================================================================\n");
    
    // Seed randomizer
    srand((unsigned int)time(NULL));

    // Initialize locks and tracking structures
    rag_init();

    for (int r = 0; r < N_RESOURCES; r++) {
        smart_mutex_init(&locks[r], r);
    }

    // Default start policy
    current_policy = POLICY_NAIVE;

    // Check command line arguments for non-interactive test mode
    int test_mode = 0;
    if (argc > 1 && strcmp(argv[1], "--test") == 0) {
        test_mode = 1;
        printf("[SYSTEM] Running in AUTOMATED TEST MODE...\n");
    }

    // Spawn Background Deadlock Detector Daemon Thread
    detector_thread = CreateThread(NULL, 0, DetectorThreadFunc, NULL, 0, NULL);

    // Spawn Worker Threads (Processes)
    for (int p = 0; p < N_THREADS; p++) {
        worker_threads[p] = CreateThread(NULL, 0, WorkerThreadFunc, (LPVOID)(intptr_t)p, 0, NULL);
    }

    if (test_mode) {
        // Run in automated test mode for 12 seconds, then toggle policy, run for another 12 seconds, and exit
        printf("[TEST] Running Naïve Mode for 12 seconds (observing deadlock & preemption cycles)...\n");
        Sleep(12000);

        EnterCriticalSection(&scheduler_cs);
        printf("\n[TEST] Swapping Policy to BANKER'S AVOIDANCE...\n");
        current_policy = POLICY_BANKER;
        // Release resources to let threads start clean
        for (int p = 0; p < N_THREADS; p++) {
            preempt_flags[p] = 1;
            SetEvent(thread_events[p]);
        }
        LeaveCriticalSection(&scheduler_cs);

        Sleep(12000);
        printf("[TEST] Testing complete. Exiting clean.\n");
        keep_running = 0;
    } else {
        // Interactive menu
        char choice;
        while (keep_running) {
            printf("\nConsole Controller Menu:\n");
            printf("  [n] Set Allocation Policy: NAÏVE MODE (Deadlock-prone)\n");
            printf("  [b] Set Allocation Policy: BANKER'S MODE (Avoidance)\n");
            printf("  [q] Quit Simulation\n");
            printf("Selection > ");
            
            // Read character safely
            if (scanf(" %c", &choice) != 1) {
                break;
            }

            if (choice == 'n' || choice == 'N') {
                EnterCriticalSection(&scheduler_cs);
                current_policy = POLICY_NAIVE;
                printf("\n[POLICY] Allocation policy changed to NAÏVE MODE.\n");
                LeaveCriticalSection(&scheduler_cs);
            } else if (choice == 'b' || choice == 'B') {
                EnterCriticalSection(&scheduler_cs);
                current_policy = POLICY_BANKER;
                printf("\n[POLICY] Allocation policy changed to BANKER'S MODE.\n");
                
                // Clear any deadlocks immediately by preempting active processes to establish a safe state
                for (int p = 0; p < N_THREADS; p++) {
                    preempt_flags[p] = 1;
                    SetEvent(thread_events[p]);
                }
                LeaveCriticalSection(&scheduler_cs);
            } else if (choice == 'q' || choice == 'Q') {
                printf("\nShutting down threads...\n");
                keep_running = 0;
                
                // Signal waiting threads to wake up and terminate
                for (int p = 0; p < N_THREADS; p++) {
                    SetEvent(thread_events[p]);
                }
            }
        }
    }

    // Wait for all threads to terminate clean
    WaitForMultipleObjects(N_THREADS, worker_threads, TRUE, 5000);
    WaitForSingleObject(detector_thread, 5000);

    // Clean up Win32 handles
    for (int p = 0; p < N_THREADS; p++) {
        CloseHandle(worker_threads[p]);
        CloseHandle(thread_events[p]);
    }
    CloseHandle(detector_thread);
    DeleteCriticalSection(&scheduler_cs);

    printf("====================================================================\n");
    printf("        SYSTEM SHUTDOWN COMPLETE. BYE!\n");
    printf("====================================================================\n");
    return 0;
}
