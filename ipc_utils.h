#ifndef IPC_UTILS_H
#define IPC_UTILS_H

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include "struct.h"  // AllRegions definition
// Enumeration for shared-memory regions

/// ---------------------- Seller Customer 
#ifndef MSG_PATH
#define MSG_PATH  "/tmp"
#endif
#ifndef MSG_PROJID
#define MSG_PROJID 'Q'
#endif

typedef enum {
    RGN_RAW = 0,    // Region for RawIng (supplier ↔ chef)
    RGN_PROG,       // Region for PreparedItems (chef ↔ baker)
    RGN_FIN,        // Region for FreshlyBakedGoods (baker ↔ seller)
    RGN_STATS,      // Region for Stats (seller ↔ customer)
    RGN_OVENS,      // Baker’s Ovens
    RGN_WAREHOUSE,       // Region for RawIng warehouse (manager ↔ suppliers)
    RGN_COUNT       // Total number of regions
} region_t;

// Initialize single shared-memory segment and semaphore set
// Returns pointer to the mapped AllRegions
AllRegions *ipc_init(void);

// Lock and unlock a specific region
void ipc_lock(region_t region);
void ipc_unlock(region_t region);

// Cleanup shared memory and semaphores
void ipc_cleanup(void);

#endif // IPC_UTILS_H
