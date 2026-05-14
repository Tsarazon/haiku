Integration Notes: KosmOS Robust Mutex
=======================================

Changes required outside of kosm_mutex.cpp to integrate the new primitive.


1. Thread struct (headers/private/kernel/thread_types.h)
--------------------------------------------------------

Add forward declaration (near existing forward declarations like
`struct io_context` and `struct user_mutex_context`):

    struct kosm_mutex_entry;

Add fields to the Thread struct (namespace BKernel):

    struct Thread {
        ...
        // KosmOS robust mutex: list of mutexes held by this thread.
        // Managed by kosm_mutex.cpp via held_list_add/held_list_remove.
        struct kosm_mutex_entry*    first_held_kosm_mutex;

        // Priority inheritance state.
        // kosm_base_priority stores the thread's own priority before
        // any PI boost. pi_recalculate_locked() restores it when all
        // PI mutexes are released or waiters depart.
        int32                       kosm_base_priority;
        bool                        kosm_pi_boosted;
        ...
    };

Initialize in Thread::Thread() constructor
(src/system/kernel/thread.cpp), in the initializer list:

    first_held_kosm_mutex(NULL),
    kosm_base_priority(0),
    kosm_pi_boosted(false)


2. Team struct (headers/private/kernel/team.h or thread_types.h)
----------------------------------------------------------------

Add field for team-owned mutex tracking (alongside existing
sem_list and port_list):

    struct Team {
        ...
        struct list     kosm_mutex_list;
        ...
    };

Initialize in Team::Team() constructor (src/system/kernel/team.cpp),
following the same pattern as port_list initialization:

    list_init_etc(&kosm_mutex_list,
        offsetof(kosm_mutex_entry, u.used.team_link));

Note: fork_team() creates a new Team via Team::Create(), which calls
the constructor. The child team gets an empty kosm_mutex_list
automatically. Mutexes are not inherited across fork, which is correct
since kernel-managed IDs are bound to the creating team.


3. Thread death handler (src/system/kernel/thread.cpp)
------------------------------------------------------

In thread_exit(), call kosm_mutex_release_owned() while the thread
struct is still valid and the thread still belongs to its original team.

Place the call inside the `if (team != kernelTeam)` block, after
DeleteUserTimers() but before the thread is moved to the kernel team:

    #include <kosm_mutex.h>

    // In thread_exit(), inside if (team != kernelTeam):
    thread->DeleteUserTimers(false);
    kosm_mutex_release_owned(thread);   // <-- here
    // ... user_thread detach, etc. follow ...

This is critical because:
 - The thread struct must still be valid (it is).
 - The thread must still belong to its original team (it does).
 - No more user code will execute (thread is exiting).
 - Held mutexes will be marked OWNER_DEAD and waiters will be woken
   with KOSM_MUTEX_OWNER_DEAD status.
 - For PI mutexes, ownership (and boost) is transferred to the first
   blocked waiter. The dead thread's own priority does not need
   recalculation since it is exiting.


4. Team death handler (src/system/kernel/team.cpp)
--------------------------------------------------

In Team::~Team() destructor, call kosm_mutex_delete_owned() to
destroy all mutexes owned by the team. Place near the existing
sem_delete_owned_sems() and delete_owned_ports() calls:

    #include <kosm_mutex.h>

    // In Team::~Team():
    delete_owned_ports(this);
    sem_delete_owned_sems(this);
    kosm_mutex_delete_owned(this);      // <-- here

By this point all threads in the team have already exited, so
kosm_mutex_release_owned() has already been called for each.
This step destroys the mutex objects themselves and frees their slots.


5. exec() cleanup (src/system/kernel/team.cpp)
----------------------------------------------

In exec_team(), the address space is replaced and all team resources
are cleaned up. Kosm mutexes must also be deleted here, since shared
memory mappings are lost and any SHARED mutexes become unreachable.

Place near the existing sem and port cleanup in exec_team():

    // In exec_team():
    delete_owned_ports(team);
    sem_delete_owned_sems(team);
    kosm_mutex_delete_owned(team);      // <-- here
    remove_images(team);
    // ...


6. Kernel init (src/system/kernel/main.cpp)
-------------------------------------------

In the kernel init sequence, after user_mutex_init():

    #include <kosm_mutex.h>

    TRACE("init kosm mutex\n");
    kosm_mutex_init(&sKernelArgs);


7. Syscall registration
-----------------------

Haiku uses gensyscalls to auto-generate syscall dispatching code.
The process works as follows:

a) Add declarations to headers/private/system/syscalls.h
   between `#pragma syscalls begin` and `#pragma syscalls end`.
   Use the `_kern_` prefix (public API naming convention):

    extern kosm_mutex_id  _kern_kosm_create_mutex(const char* name,
                              uint32 flags);
    extern status_t       _kern_kosm_delete_mutex(kosm_mutex_id id);
    extern kosm_mutex_id  _kern_kosm_find_mutex(const char* name);
    extern status_t       _kern_kosm_acquire_mutex(kosm_mutex_id id);
    extern status_t       _kern_kosm_try_acquire_mutex(kosm_mutex_id id);
    extern status_t       _kern_kosm_acquire_mutex_etc(kosm_mutex_id id,
                              uint32 flags, bigtime_t timeout);
    extern status_t       _kern_kosm_release_mutex(kosm_mutex_id id);
    extern status_t       _kern_kosm_mark_mutex_consistent(kosm_mutex_id id);
    extern status_t       _kern_kosm_get_mutex_info(kosm_mutex_id id,
                              kosm_mutex_info* info, size_t size);

b) Implement kernel-side functions in kosm_mutex.cpp with `_user_` prefix.
   gensyscalls automatically transforms _kern_* -> _user_* for kernel names:

    _user_kosm_create_mutex()
    _user_kosm_delete_mutex()
    _user_kosm_find_mutex()
    _user_kosm_acquire_mutex()
    _user_kosm_try_acquire_mutex()
    _user_kosm_acquire_mutex_etc()
    _user_kosm_release_mutex()
    _user_kosm_mark_mutex_consistent()
    _user_kosm_get_mutex_info()

c) gensyscalls parses syscalls.h and generates:
   - syscall_dispatcher.h (case statements for syscall_dispatcher())
   - syscall_table.h (kSyscallInfos array)
   - syscall_numbers.h (SYSCALL_* defines)

No manual changes to syscalls.cpp are needed.


8. Build system
---------------

Add kosm_mutex.cpp to the kernel build (src/system/kernel/Jamfile),
in the main kernel_core.o sources list alongside sem.cpp and port.cpp
(other ID-based kernel primitives with team ownership):

    port.cpp
    real_time_clock.cpp
    sem.cpp
    kosm_mutex.cpp      // <-- add here
    shutdown.cpp


9. Public header (headers/os/kernel/KosmOS.h)
----------------------------------------------

Public API (types, flags, error codes, functions) lives in KosmOS.h.

Types required for kosm_mutex:

    typedef int32 kosm_mutex_id;

    typedef struct kosm_mutex_info {
        kosm_mutex_id   mutex;
        team_id         team;
        char            name[B_OS_NAME_LENGTH];
        thread_id       holder;
        int32           recursion;
        uint32          flags;
    } kosm_mutex_info;

Creation flags:

    // Mutex is shared across teams (cross-process).
    #define KOSM_MUTEX_SHARED           0x01

    // Mutex supports recursive locking by the same thread.
    #define KOSM_MUTEX_RECURSIVE        0x02

    // Enable single-level priority inheritance.
    // When a high-priority thread blocks on this mutex, the holder's
    // effective priority is raised to prevent priority inversion.
    // Recalculated on every waiter departure and ownership transfer.
    #define KOSM_MUTEX_PRIO_INHERIT     0x04

Error/status codes:

    // Returned by acquire when the previous holder died.
    // The mutex is in needs_recovery state; the new holder must
    // either call kosm_mark_mutex_consistent() + release, or
    // just release (which makes the mutex not recoverable).
    #define KOSM_MUTEX_OWNER_DEAD       0x4B01

    // Returned when the mutex is permanently broken (holder died
    // and the recovery holder released without mark_consistent).
    #define KOSM_MUTEX_NOT_RECOVERABLE  0x4B02

    // Returned by release if the calling thread is not the holder.
    #define KOSM_MUTEX_NOT_OWNER        0x4B03

    // Returned by acquire on a non-recursive mutex when the caller
    // already holds it.
    #define KOSM_MUTEX_DEADLOCK         0x4B04

Public C API (declared in KosmOS.h, implemented as syscalls):

    kosm_mutex_id   kosm_create_mutex(const char* name, uint32 flags);
    status_t        kosm_delete_mutex(kosm_mutex_id id);
    kosm_mutex_id   kosm_find_mutex(const char* name);
    status_t        kosm_acquire_mutex(kosm_mutex_id id);
    status_t        kosm_try_acquire_mutex(kosm_mutex_id id);
    status_t        kosm_acquire_mutex_etc(kosm_mutex_id id,
                        uint32 flags, bigtime_t timeout);
    status_t        kosm_release_mutex(kosm_mutex_id id);
    status_t        kosm_mark_mutex_consistent(kosm_mutex_id id);
    status_t        kosm_get_mutex_info(kosm_mutex_id id,
                        kosm_mutex_info* info, size_t size);

Header inclusion:

    headers/os/kernel/KosmOS.h           <- public, includes OS.h
    headers/private/kernel/kosm_mutex.h  <- kernel-internal only

User code:

    #include <KosmOS.h>

Kernel code:

    #include <kosm_mutex.h>

As new KOSM primitives are added, their public API goes directly
into KosmOS.h (new section), and their kernel-internal functions
get a dedicated private header.


10. Kernel debugger integration
-------------------------------

a) Register KDL commands in kosm_mutex_init()
   (already implemented in kosm_mutex.cpp):

    add_debugger_command("kosm_mutexes", &dump_kosm_mutex_list,
        "list kosm_mutexes [team <id>] [name <pattern>]");
    add_debugger_command("kosm_mutex", &dump_kosm_mutex_info,
        "info about a kosm_mutex");

   The list command shows [PI] suffix for PI-enabled mutexes.
   The info command additionally shows max_wpri (highest waiter
   priority) for PI mutexes, and per-waiter priorities in the queue.

b) Add THREAD_BLOCK_TYPE_KOSM_MUTEX case to _dump_thread_info()
   in src/system/kernel/thread.cpp. This enables KDL to show what
   a blocked thread is waiting on:

    case THREAD_BLOCK_TYPE_KOSM_MUTEX:
        kprintf("kosm_mtx  %p   ", thread->wait.object);
        break;

c) Add THREAD_BLOCK_TYPE_KOSM_MUTEX to the enum in
   headers/private/system/thread_defs.h:

    enum {
        THREAD_BLOCK_TYPE_SEMAPHORE             = 0,
        THREAD_BLOCK_TYPE_CONDITION_VARIABLE    = 1,
        ...
        THREAD_BLOCK_TYPE_USER                  = 6,
        THREAD_BLOCK_TYPE_KOSM_MUTEX            = 7,  // <-- add here
        THREAD_BLOCK_TYPE_OTHER_OBJECT          = 9998,
        THREAD_BLOCK_TYPE_OTHER                 = 9999,
    };

   Alternatively, use a distinct value like 0x4B4D ('KM') to avoid
   renumbering if future types are added upstream.


11. Usage in SurfaceRegistry
-----------------------------

Replace sem_id in KosmSurfaceRegistryHeader with kosm_mutex_id:

    struct KosmSurfaceRegistryHeader {
        kosm_mutex_id   lock;       // was: sem_id lock;
        int32           entryCount;
        int32           tombstoneCount;
        uint32          _reserved[5];
    };

Registry init (use PRIO_INHERIT since compositor is high-priority):

    fHeader->lock = kosm_create_mutex("kosm_surface_registry",
        KOSM_MUTEX_SHARED | KOSM_MUTEX_PRIO_INHERIT);

Lock/unlock pattern:

    status_t status = kosm_acquire_mutex(fHeader->lock);
    if (status == KOSM_MUTEX_OWNER_DEAD) {
        _ValidateAndRepairRegistry();
        kosm_mark_mutex_consistent(fHeader->lock);
    } else if (status != B_OK) {
        return status;
    }
    // ... critical section ...
    kosm_release_mutex(fHeader->lock);

The _ValidateAndRepairRegistry() function should walk all entries
and verify consistency (e.g. check that entryCount matches actual
non-empty/non-tombstone entries, verify planeCount bounds, etc).

PI ensures that when the compositor (high priority) blocks on the
registry lock held by a background app (low priority), the app is
boosted to the compositor's priority for the duration of the hold.
This prevents jank from priority inversion on registry access.


12. Scheduler integration (edge case)
--------------------------------------

If set_thread_priority() is called on a PI-boosted thread from
outside kosm_mutex.cpp, kosm_base_priority becomes stale. This is
an edge case (normal applications don't change thread priorities
while holding cross-process mutexes), but for correctness a hook
can be added later in scheduler_set_thread_priority():

    // In scheduler_set_thread_priority() or equivalent:
    if (thread->kosm_pi_boosted) {
        thread->kosm_base_priority = newPriority;
        // If new base is higher than current boosted, update:
        if (newPriority > thread->priority)
            thread->priority = newPriority;
    }

This is not required for the first iteration. Document and defer.
