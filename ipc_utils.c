/* ipc_utils.c */
#include "header.h"

// Keys for SHM and semaphore set
#define SHM_KEY 0x6001
#define SEM_KEY 0x6002


// Internal IDs
static int   ipc_shmid = -1;
static int   ipc_semid = -1;
static AllRegions *ipc_state = NULL;

// Union for semctl initialization
union semun { int val; struct semid_ds *buf; unsigned short *array; };

// Initialize shared memory and semaphore set
AllRegions *ipc_init(void) {
    //
    int created_shm = 0;
    // Try to create SHM exclusively 
    ipc_shmid = shmget(SHM_KEY, sizeof(AllRegions), IPC_CREAT | IPC_EXCL | 0666);
    if (ipc_shmid >= 0) {
        created_shm = 1;
    } else if (errno == EEXIST) {
        ipc_shmid = shmget(SHM_KEY, sizeof(AllRegions), 0666);
    } else {
        perror("shmget");
        exit(EXIT_FAILURE);
    }

    // Attach to shared memory
    ipc_state = (AllRegions *)shmat(ipc_shmid, NULL, 0); // attach it into this process’s address 
    if (ipc_state == (void *)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    // Zero out memory only on first creation
    if (created_shm) {
        memset(ipc_state, 0, sizeof(AllRegions));
    }

    // Create or get semaphore set, similarly
    int created_sem = 0;
    ipc_semid = semget(SEM_KEY, RGN_COUNT, IPC_CREAT | IPC_EXCL | 0666);
    if (ipc_semid >= 0) {
        created_sem = 1;
    } else if (errno == EEXIST) {
        ipc_semid = semget(SEM_KEY, RGN_COUNT, 0); // get/create one set of RGN_COUNT semaphores 
    } else {
        perror("semget");
        exit(EXIT_FAILURE);
    }

    // Initialize semaphores to 1 only on creation
    if (created_sem) {
        union semun arg;
        unsigned short *init_vals = calloc(RGN_COUNT, sizeof(unsigned short));
        if (!init_vals) { perror("calloc"); exit(EXIT_FAILURE); }
        for (int i = 0; i < RGN_COUNT; ++i) init_vals[i] = 1;
        arg.array = init_vals;
        if (semctl(ipc_semid, 0, SETALL, arg) < 0) {  //initialize each semaphore to 1           */

            perror("semctl SETALL");
            exit(EXIT_FAILURE);
        }
        free(init_vals);
    }

    return ipc_state;
}

// Lock a specific region
void ipc_lock(region_t r) {
    struct sembuf op = { .sem_num = r, .sem_op = -1, .sem_flg = SEM_UNDO };
    if (semop(ipc_semid, &op, 1) < 0) {
        perror("ipc_lock semop");
        exit(EXIT_FAILURE);
    }
}

// Unlock a specific region
void ipc_unlock(region_t r) {
    struct sembuf op = { .sem_num = r, .sem_op = +1, .sem_flg = SEM_UNDO };
    if (semop(ipc_semid, &op, 1) < 0) {
        perror("ipc_unlock semop");
        exit(EXIT_FAILURE);
    }
}

// Cleanup shared memory and semaphores
void ipc_cleanup(void) {
    if (ipc_state) {
        shmdt(ipc_state);
        ipc_state = NULL;
    }
    if (ipc_shmid >= 0) {
        shmctl(ipc_shmid, IPC_RMID, NULL);
        ipc_shmid = -1;
    }
    if (ipc_semid >= 0) {
        semctl(ipc_semid, 0, IPC_RMID);
        ipc_semid = -1;
    }
}
