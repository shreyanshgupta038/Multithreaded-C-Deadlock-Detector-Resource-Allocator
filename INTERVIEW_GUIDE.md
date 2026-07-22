# Interview Preparation Guide: Multithreaded Deadlock Allocator & Detector

Use this comprehensive guide to prepare for technical interviews. It breaks down the **Why**, **What**, and **How** of the project, followed by key systems-programming concepts, architectural deep-dives, and potential interviewer questions.

---

## 1. THE "WHY" (The Core Engineering Problem)
In concurrent systems, multiple threads frequently need to acquire multiple shared resources (e.g., database records, I/O devices, file locks). 

* **The Greedy Lock Problem**: Standard synchronization primitives (like `pthread_mutex_lock` or Win32 `EnterCriticalSection`) are "greedy" and "blind." They grant a lock immediately if it is free, without looking at the bigger picture.
* **The Deadlock Condition**: If Thread A holds Lock 1 and requests Lock 2, while Thread B holds Lock 2 and requests Lock 1, both threads enter an infinite wait state. This is a **circular wait** (deadlock).
* **OS Limitations**: Standard OS kernels do not detect or resolve deadlocks for user applications. If your threads deadlock, the application freezes.
* **Why This Project was Made**: 
  - To build a custom **monitor abstraction layer** on top of OS locks that tracks allocations in real-time.
  - To evaluate and compare two classic deadlock strategies under real multithreaded contention: **Deadlock Detection & Recovery** (Naïve Mode) versus **Deadlock Avoidance** (Banker's Mode).
  - To implement **Cooperative Preemption & Transactional Rollback**, which resolves deadlocks cleanly without leaking memory or leaving locks orphaned.

---

## 2. THE "WHAT" (System Architecture)

The project is structured into three distinct layers running in C:

```
+--------------------------------------------------------+
| 1. WORKER THREADS (Processes P0 - P3)                 |
|    - Loop: declare max needs, shuffle request sequence |
+---------------------------+----------------------------+
                            |
                            | calls smart_mutex_lock()
                            v
+--------------------------------------------------------+
| 2. CUSTOM MONITOR SCHEDULER (smart_mutex_t)            |
|    - Intercepts requests, locks, and unlocks.          |
|    - Blocks threads on Win32 Events when unsafe/held.  |
+---------------------------+----------------------------+
                            |
                            | interacts with
                            v
+--------------------------------------------------------+
| 3. GRAPH & SAFETY ENGINES (rag.h/c)                   |
|    - Memory-space matrices (Alloc, Need, Max, Request) |
|    - Bipartite Directed DFS Cycle Finder               |
|    - Banker's Safety Sequence Calculator               |
+---------------------------+----------------------------+
                            |
                            | periodically scanned by
                            v
+--------------------------------------------------------+
| 4. BACKGROUND DETECTOR DAEMON THREAD                   |
|    - Runs cycle detection every 2 seconds.             |
|    - Signals preemption/rollback flags on victims.     |
+--------------------------------------------------------+
```

### The Two Simulation Modes
* **Naïve Mode**: Mutexes are allocated greedily. When a deadlock occurs, the background daemon thread runs a DFS cycle finder, selects a victim process, flags it for preemption, and wakes it up. The victim thread rolls back, releases all held locks, backs off, and retries.
* **Banker's Mode**: Locks are only allocated if a mathematical safety check passes. The allocator run the Banker's Safety Algorithm to verify that a safe sequence exists where all threads can finish. Unsafe requests are deferred, preventing deadlocks entirely.

---

## 3. THE "HOW" (Low-Level C Implementation)

### A. Thread Blocking & Decoupled Waiting
Standard lock wait-queues are hidden in the kernel. To inspect them, we decoupled lock wait states from the actual lock:
- We created a global monitor lock (`scheduler_cs` critical section) to make graph updates thread-safe.
- We created a native Win32 Event handle (`thread_events[N]`) for each thread.
- If a lock request is blocked or unsafe, the thread registers its wait state in the RAG matrix (`rag.request[P][R] = 1`), releases `scheduler_cs` to let other threads run, and blocks on its Event handle (`WaitForSingleObject(thread_events[P], INFINITE)`).

### B. Bipartite DFS Cycle Finder
To detect circular waits, we map active dependencies to a bipartite graph where nodes are processes (`0..N-1`) and resources (`N..N+M-1`).
- Edges:
  - Request edge (Process $\to$ Resource): `rag.request[P][R] == 1`
  - Allocation edge (Resource $\to$ Process): `rag.allocation[P][R] == 1`
- The cycle detector runs a Depth-First Search (DFS) with a **recursion stack** (coloring visited nodes Gray/Black). If it hits a node in the current path (Gray), a back-edge is found, and it extracts the exact circular wait path (e.g., `P1 -> R0 -> P2 -> R1 -> P1`).

### C. Cooperative Preemption (Clean Recovery)
Force-terminating a thread via `TerminateThread` is dangerous because it leaves mutexes locked and leaks resources. We implemented **Cooperative Preemption**:
1. The daemon thread flags the victim (`preempt_flags[victim] = 1`) and calls `SetEvent(thread_events[victim])` to wake it.
2. The victim thread wakes up inside `smart_mutex_lock()`, detects the preemption flag, cleans its request edge, and returns `SMART_MUTEX_PREEMPTED`.
3. In `main.c`, the thread catches `SMART_MUTEX_PREEMPTED`, calls `release_all_held_resources()`, resets its lock state (`locks[r].owner = -1`), sleeps with a randomized backoff (to prevent starvation), and restarts its transaction loop.

---

## 4. TYPICAL INTERVIEW QUESTIONS & MODEL ANSWERS

### Q1: What is the difference between deadlock prevention, avoidance, and detection/recovery?
* **Answer**:
  - *Prevention*: Eliminates one of the four Coffman conditions (e.g., eliminating circular wait by ordering lock acquisition globally).
  - *Avoidance*: Dynamically checks if granting a request could lead to a deadlock (e.g., **Banker's Algorithm**). It allows requests only if a safe sequence exists.
  - *Detection & Recovery*: Let deadlocks happen, scan for them using a background thread (**Cycle Detection**), and recover by preempting resources or rolling back threads.

### Q2: Why is the Banker's Algorithm rarely used in commercial OS kernels (like Windows/Linux)?
* **Answer**:
  - It requires processes to declare their **Maximum Resource Claims** in advance, which is impossible for general-purpose applications.
  - The number of resources and processes in a modern OS is highly dynamic, whereas Banker's assumes a fixed, static number.
  - The overhead of running safety checks on every lock request degrades system performance.
  - *Note*: It is, however, highly applicable in static embedded systems, safety-critical aerospace software, and database transaction managers where max claims are known.

### Q3: How did you handle race conditions when writing/reading the graph structure?
* **Answer**:
  - *"I implemented a monitor pattern. I created a global critical section `scheduler_cs`. Any thread requesting a lock, releasing a lock, or modifying allocation matrices must acquire this critical section. When the background daemon thread runs cycle detection, it also holds `scheduler_cs` to freeze the graph state, preventing race conditions or inconsistent reads."*

### Q4: What is Priority Inversion, and did your custom locks address it?
* **Answer**:
  - *Priority Inversion* occurs when a low-priority thread holds a lock needed by a high-priority thread, and a medium-priority thread preempts the low-priority thread, indirectly blocking the high-priority thread.
  - *"In my implementation, since threads block on Win32 event handles inside a user-space monitor loop rather than standard priority-aware scheduler queues, the OS scheduler is unaware of the lock dependency. In a production version, I would implement **Priority Inheritance** where a thread waiting for a `smart_mutex_t` temporarily donates its priority to the lock owner."* (This connects perfectly to your PintOS scheduler project!)

---

## 5. REHEARSAL CHECKLIST (Before your Interview)

- [ ] Explain **Cooperative Preemption** and why `TerminateThread` is a systems anti-pattern.
- [ ] Understand how a **Bipartite Graph** represents requests and allocations.
- [ ] Be able to sketch the **Banker's Safety Check loop** (Work and Finish arrays).
- [ ] Explain the role of the **Thread Event Handles** (`SetEvent` / `WaitForSingleObject`) as custom wait-queues.
