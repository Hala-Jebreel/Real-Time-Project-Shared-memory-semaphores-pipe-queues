/* baker.c – unified SHM (AllRegions) + ovens + correct deposit logic */

#include "header.h"    // ipc_init, ipc_lock, ipc_unlock
#include "struct.h"    // AllRegions, PreparedItems, FreshlyBakedGoods, OvenRegion

#define OVEN_CAPACITY 5
#define MAX_INVENTORY 7

// Product codes for deposit step (no more sandwiches here)
enum {
    PROD_BREAD = 0,
    PROD_CAKE,
    PROD_SWEET,
    PROD_SWEET_PAST,
    PROD_SAVORY_PAST
};

static volatile sig_atomic_t running = 1;
static void handle_sigint(int _) { running = 0; }

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <team_id>\n", argv[0]);
        return EXIT_FAILURE;
    }
    int team = atoi(argv[1]);  // 0: Bread, 1: Cakes/Sweets, 2: Patisseries
    const char *team_name[3] = {"Bread", "Cakes/Sweets", "Patisseries"};

    signal(SIGINT, handle_sigint);
    AllRegions *B = ipc_init();

    printf("👩‍🍳 Baker[%d] team=%s started\n", getpid(), team_name[team]);

    while (running) {
        // throttle production to avoid overfilling FIN
        ipc_lock(RGN_FIN);
        unsigned finbuf = 0;
        switch (team) {
          case 0: finbuf = B->fin.bread; break;
          case 1: finbuf = B->fin.cakes + B->fin.sweets; break;
          case 2: finbuf = B->fin.sweet_pastries + B->fin.savory_pastries; break;
        }
        ipc_unlock(RGN_FIN);
        if (finbuf >= MAX_INVENTORY) {
            sleep(1);
            continue;
        }

        // pull a prepared item
        int product = -1;
        unsigned remaining = 0;
        ipc_lock(RGN_PROG);
        switch (team) {
          case 0:
            if (B->prog.paste > 0) {
                B->prog.paste--;
                product = PROD_BREAD;
                remaining = B->prog.paste;
            }
            break;
          case 1:
            if (B->prog.cake_prepared > 0) {
                B->prog.cake_prepared--;
                product = PROD_CAKE;
                remaining = B->prog.cake_prepared;
            } else if (B->prog.sweet_prepared > 0) {
                B->prog.sweet_prepared--;
                product = PROD_SWEET;
                remaining = B->prog.sweet_prepared;
            }
            break;
          case 2:
            if (B->prog.sweet_patisserie_prepared > 0) {
                B->prog.sweet_patisserie_prepared--;
                product = PROD_SWEET_PAST;
                remaining = B->prog.sweet_patisserie_prepared;
            } else if (B->prog.savory_patisserie_prepared > 0) {
                B->prog.savory_patisserie_prepared--;
                product = PROD_SAVORY_PAST;
                remaining = B->prog.savory_patisserie_prepared;
            }
            break;
        }
        ipc_unlock(RGN_PROG);

        if (product < 0) {
            printf("⏳ Baker[%d] no prepared %s, waiting...\n", getpid(), team_name[team]);
            sleep(2);
            continue;
        }
        printf("👷 Baker[%d] consumed one %s, remaining prepared=%u\n", getpid(), team_name[team], remaining);

        // bake in oven
        bool inOven = false;
        while (running && !inOven) {
            ipc_lock(RGN_OVENS);
            switch (team) {
              case 0:
                if (B->ovens.bread_slots < OVEN_CAPACITY) {
                    B->ovens.bread_slots++;
                    inOven = true;
                }
                break;
              case 1:
                if (B->ovens.cake_slots < OVEN_CAPACITY) {
                    B->ovens.cake_slots++;
                    inOven = true;
                }
                break;
              case 2:
                if (B->ovens.patisserie_slots < OVEN_CAPACITY) {
                    B->ovens.patisserie_slots++;
                    inOven = true;
                }
                break;
            }
            ipc_unlock(RGN_OVENS);
            if (!inOven) {
                printf("🚫 Baker[%d] oven full for %s, waiting...\n", getpid(), team_name[team]);
                sleep(1);
            }
        }

        int bake_time = (team==0 ? 2 : team==1 ? 4 : 3);
        printf("🔥 Baker[%d] baking %s for %d seconds...\n", getpid(), team_name[team], bake_time);
        sleep(bake_time);

        // exit oven
        ipc_lock(RGN_OVENS);
        switch (team) {
          case 0: B->ovens.bread_slots--; break;
          case 1: B->ovens.cake_slots--; break;
          case 2: B->ovens.patisserie_slots--; break;
        }
        ipc_unlock(RGN_OVENS);

        // deposit into FIN
        ipc_lock(RGN_FIN);
        switch (product) {
          case PROD_BREAD:
            B->fin.bread++;
            printf("🍞 Baker[%d] deposited BREAD → FIN.bread=%u\n", getpid(), B->fin.bread);
            break;
          case PROD_CAKE:
            B->fin.cakes++;
            printf("🍰 Baker[%d] deposited CAKE → FIN.cakes=%u\n", getpid(), B->fin.cakes);
            break;
          case PROD_SWEET:
            B->fin.sweets++;
            printf("🍬 Baker[%d] deposited SWEET → FIN.sweets=%u\n", getpid(), B->fin.sweets);
            break;
          case PROD_SWEET_PAST:
            B->fin.sweet_pastries++;
            printf("🥐 Baker[%d] deposited SWEET_PAST → FIN.sweet_pastries=%u\n", getpid(), B->fin.sweet_pastries);
            break;
          case PROD_SAVORY_PAST:
            B->fin.savory_pastries++;
            printf("🥨 Baker[%d] deposited SAVORY_PAST → FIN.savory_pastries=%u\n", getpid(), B->fin.savory_pastries);
            break;
        }
        ipc_unlock(RGN_FIN);

        const char *prod_name;
        switch (product) {
          case PROD_BREAD:       prod_name = "bread"; break;
          case PROD_CAKE:        prod_name = "cake"; break;
          case PROD_SWEET:       prod_name = "sweet"; break;
          case PROD_SWEET_PAST:  prod_name = "sweet patisserie"; break;
          case PROD_SAVORY_PAST: prod_name = "savory patisserie"; break;
          default:               prod_name = "unknown"; break;
        }
        printf("✅ Baker[%d] finished %s\n", getpid(), prod_name);
    }

    shmdt(B);
    printf("👋 Baker[%d] exiting\n", getpid());
    return EXIT_SUCCESS;
}
