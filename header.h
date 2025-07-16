// header.h - Updated for single SHM design
#ifndef HEADER_H
#define HEADER_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <sys/msg.h>  //customer.c
#include <sys/time.h> //gui
#include <math.h>

#include <signal.h>
#include <errno.h>
#include "config.h"      // for Config type
#include "ipc_utils.h"   // for AllRegions, region_t, ipc_init, ipc_lock, ipc_unlock, ipc_cleanup

#include <stdbool.h>      //CHEF
#include <time.h>        // chef baker
#include <stddef.h>     // For NULL cnfig.c
#include <string.h>    // For strlen, strcpy, strchr, strcmp, memmove, memset //hconfig.c

// Forking worker processes
void fork_chefs(int chef_count);
void fork_bakers(int baker_count);

// Cleanup all IPC and child processes
void cleanup_resources(void);

// Signal handler for SIGINT
void sigint_handler(int sig);

// Utility: random integer in [lo, hi]
int rand_between(int lo, int hi);

// Load simulation parameters from config file
//int load_config(const char *filename, Config *cfg);

// Initialize IPC (shared memory + semaphores)
// Returns pointer to global AllRegions state
AllRegions *ipc_init(void);

// Lock and unlock a region
void ipc_lock(region_t r);
void ipc_unlock(region_t r);

// Clean up shared memory and semaphores
void ipc_cleanup(void);

#endif // HEADER_H
