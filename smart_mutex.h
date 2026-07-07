#ifndef SMART_MUTEX_H
#define SMART_MUTEX_H

#define SMART_MUTEX_SUCCESS 0
#define SMART_MUTEX_PREEMPTED 1

typedef struct {
    int id;         // Resource ID (0 to N_RESOURCES-1)
    int owner;      // Process ID currently holding the lock (-1 if free)
} smart_mutex_t;

// API Prototypes
void smart_mutex_init(smart_mutex_t *mutex, int id);
int smart_mutex_lock(smart_mutex_t *mutex, int thread_id);
void smart_mutex_unlock(smart_mutex_t *mutex, int thread_id);

#endif // SMART_MUTEX_H
