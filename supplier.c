/* supplier.c — unified SHM with selective refills and dynamic thresholds */

#include "header.h"      // AllRegions

#define NUM_INGS 9
#define MIN_REFILL 5    // bring at least 5 units each time

static volatile sig_atomic_t refill_requested = 0;
static void on_sigusr1(int sig) {
    (void)sig;
    refill_requested = 1;
    printf("[supplier %d] ⏰ got refill signal\n", getpid());
    signal(SIGUSR1, on_sigusr1);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <supplier_index>\n", argv[0]);
        return EXIT_FAILURE;
    }
    bool is_leader = (atoi(argv[1]) == 0);
    AllRegions *B = ipc_init();
    unsigned *warehouse = &B->warehouse.wheat;
    unsigned *raw       = &B->raw.wheat;

    int threshold[NUM_INGS];

    srand((unsigned)time(NULL) ^ getpid());
    signal(SIGUSR1, on_sigusr1);

    // 1) Initial seeding (leader) or pick up thresholds
    if (is_leader) {
        ipc_lock(RGN_WAREHOUSE);
        ipc_lock(RGN_RAW);
        for (int i = 0; i < NUM_INGS; ++i) {
            unsigned avail = warehouse[i];
            int take = (avail > 0) ? (rand() % avail) + 1 : 0;
            warehouse[i] -= take;
            raw[i]       = take;
            threshold[i] = take / 2;
            printf("[supplier] deposit[%d]=%d, threshold[%d]=%d, warehouse_left[%d]=%u\n",
                   i, take, i, threshold[i], i, warehouse[i]);
        }
        ipc_unlock(RGN_RAW);
        ipc_unlock(RGN_WAREHOUSE);
        printf("🚚[supplier] initial shared stock set\n");
    } else {
        ipc_lock(RGN_RAW);
        for (int i = 0; i < NUM_INGS; ++i) {
            threshold[i] = raw[i] / 2;
        }
        ipc_unlock(RGN_RAW);
    }

    // 2) Refill loop
    while (1) {
        pause();
        if (!refill_requested) continue;
        refill_requested = 0;
        printf("🚚[supplier] refill requested\n");

        // reload thresholds from raw[]
        int current[NUM_INGS];
        ipc_lock(RGN_RAW);
          for (int i = 0; i < NUM_INGS; ++i)
            current[i] = raw[i];
        ipc_unlock(RGN_RAW);

        for (int i = 0; i < NUM_INGS; ++i) {
            int th = threshold[i];
            if (current[i] > th) continue;

            // compute how much to bring (min MIN_REFILL)
            ipc_lock(RGN_WAREHOUSE);
              unsigned avail = warehouse[i];
              int need = MIN_REFILL;
              int give = (avail < (unsigned)need) ? avail : need;
              warehouse[i] -= give;
              unsigned left = warehouse[i];
            ipc_unlock(RGN_WAREHOUSE);

            if (give > 0) {
                ipc_lock(RGN_RAW);
                  raw[i] += give;
                  unsigned now = raw[i];
                ipc_unlock(RGN_RAW);

                // update threshold to half the new raw[]
                threshold[i] = now / 2;

                printf("[supplier] refilled[%d]+%d → raw=%u, new_thresh=%d, warehouse_left=%u\n",
                       i, give, now, threshold[i], left);
            } else {
                printf("[supplier] EMPTY[%d]: no warehouse stock (raw=%d)\n", i, current[i]);
            }
        }

        // 3) Check if warehouse is fully empty
        bool all_empty = true;
        ipc_lock(RGN_WAREHOUSE);
          for (int i = 0; i < NUM_INGS; ++i) {
              if (warehouse[i] > 0) { all_empty = false; break; }
          }
        ipc_unlock(RGN_WAREHOUSE);

        if (all_empty) {
            printf("[supplier] warehouse fully empty — broadcasting SIGURG\n");
            kill(0, SIGURG);
            while (1) pause();
        }
    }

    shmdt(B);
    return EXIT_SUCCESS;
}