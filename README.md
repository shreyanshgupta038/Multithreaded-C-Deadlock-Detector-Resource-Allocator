# Multithreaded C Deadlock Detector & Resource Allocator

A low-level operating systems programming implementation of a deadlock detection, prevention, and recovery engine in C. This project simulates concurrent processes competing for shared resources (mutex locks) in real-time, managed by a custom monitor scheduler and a background deadlock daemon thread.

This project is designed as an advanced OS systems programming project demonstrating thread safety, race conditions, cycle detection, Banker's safety check sequence calculation, and transactional rollback recovery.

---

## Architecture & Implementation Details

```
                    +------------------------+
                    |    Worker Thread 0     |
                    +-----------+------------+
                                |
                                | calls smart_mutex_lock()
                                v
                    +------------------------+
                    |    smart_mutex.h/c     |
                    +-----------+------------+
                                |
                                | checks Banker's Safety
                                v
+-------------+     +------------------------+     +------------------------+
| Background  |     |        rag.h/c         |     |      Thread-Specific   |
| Detector    |---->| (Resource Allocation   |<--->|      Event Handles     |
| Daemon      |     |     Graph Matrix)      |     |    (thread_events[N])  |
+------+------+     +------------------------+     +------------------------+
       |
       | Deadlock detected
       v
+------+-------------------------------------+
| Cooperative Preemption & Rollback Recovery |
+--------------------------------------------+
```

### 1. Smart Mutex Monitor (`smart_mutex.h` / `smart_mutex.c`)
Standard mutexes block threads implicitly at the OS kernel level, hiding wait-states. This project implements a custom monitor-style synchronization lock wrapper (`smart_mutex_t`):
- Tracks lock ownership and pending requests inside a central memory graph.
- Threads wait on thread-specific Event handles (`thread_events[N]`) inside a monitor loop.
- Intercepts requests and enforces safety policies: immediately in **Naïve Mode**, or only when system safety is mathematically guaranteed in **Banker's Mode**.

### 2. Safety & Cycle Engine (`rag.h` / `rag.c`)
- **Resource Allocation Graph (RAG)**: Formed of process nodes (circles) and resource nodes (rectangles) with request and allocation edges.
- **DFS Cycle Detection**: Runs a bipartite directed cycle-finding algorithm using backtracking DFS with color-mark graph traversal to isolate deadlocks.
- **Banker's Algorithm Safety Check**: Hypothetically attempts to grant requests and runs Banker's safety checks to verify the existence of a safe sequence where all threads can complete.

### 3. Background Daemon & Recovery (`main.c`)
- Spawns a background **Detector Daemon Thread** that periodically lock-scans the active RAG for cycles and prints out system matrices.
- **Cooperative Preemption & Transaction Rollback**: When a deadlock is detected in Naïve Mode, the daemon selects a victim in the cycle, flags it, and signals its event. The victim thread wakes up, cleans its request edge, releases all other locks it already held (preventing orphaned locks and leaks), sleeps for a backoff duration, and restarts its transaction.

---

## File Structure

- `rag.h`: Graph matrices and thread state structures.
- `rag.c`: Safety checking and cycle detection algorithms.
- `smart_mutex.h`: Smart lock Monitor interfaces.
- `smart_mutex.c`: Custom lock request and wait loop handlers.
- `main.c`: Spawns worker threads (P0-P3), background detector daemon, and runs console menus.
- `build.bat`: GCC compiler shell batch file.

---

## How to Run

### Prerequisite
A C compiler (e.g. GCC / MinGW) added to your system PATH.

### 1. Compile the Project
Double click or run `build.bat` in your terminal:
```powershell
.\build.bat
```
This compiles the C modules and outputs `main.exe`.

### 2. Run in Interactive Mode
```powershell
.\main.exe
```
This launches a live interactive console. You can switch policies in real-time by entering:
- `n`: Set Allocation Policy to **Naïve Mode** (allows deadlocks, triggers background preemption rollbacks).
- `b`: Set Allocation Policy to **Banker's Mode** (actively prevents deadlocks).
- `q`: Quit simulation safely.

### 3. Run in Automated Test Verification
```powershell
.\main.exe --test
```
Runs a 24-second verification cycle (12 seconds in Naïve Mode showing deadlock cycles and recoveries, and 12 seconds in Banker's Mode showing safety checks and lock deferrals).
