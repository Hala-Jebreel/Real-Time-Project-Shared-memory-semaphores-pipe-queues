/* chef.c – unified SHM (AllRegions) + dynamic pivoting + supplier signaling */
#include "header.h"      // ipc_init, ipc_lock, ipc_unlock, ipc_cleanup
// #include "struct.h"      // AllRegions, RawIng, PreparedItems
// #include "config.h"      // Config, load_config()

#define NUM_ITEMS      6
#define NUM_INGS       9
#define MAX_INVENTORY  7
#define MIN_PASTE_BUF  1   // for patisserie teams
#define MIN_BREAD_BUF  2   // for sandwich team

// item indices
enum {
    ITEM_PASTE = 0,
    ITEM_CAKE,
    ITEM_SANDWICH,
    ITEM_SWEET,
    ITEM_SWEET_PAT,
    ITEM_SAVORY_PAT
};
// Ingredient indices
enum {
  I_WHEAT = 0, I_YEAST, I_BUTTER,
  I_MILK, I_SUGAR, I_SALT,
  I_SWEET_ITEMS, I_CHEESE, I_SALAMI
};

// per-item raw recipes
static const int recipe[NUM_ITEMS][NUM_INGS] = {
    //W, Y, Bu, Mi, Su, Sa, SI, Ch, Sal/
    {5,1,0,0,0,0,0,0,0},  // paste
    {0,0,2,2,8,1,5,0,0},  // cake
    {2,1,1,1,0,1,0,2,2},  // sandwich
    {0,0,1,1,5,1,5,0,0},  // sweet
    {0,0,2,1,2,0,3,0,0},  // sweet patisserie
    {0,0,2,0,0,2,0,3,3},  // savory patisserie
};

static const char *item_name[NUM_ITEMS] = {
    "paste","cake","sandwich","sweet","sweet patisserie","savory patisserie"
};

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t no_more_refills = 0;

static void handle_sigint(int _) {
    running = 0;
}
static void handle_no_more(int _) {
    no_more_refills = 1;
    printf("ℹ Chef[%d] end-of-warehouse signal\n", getpid());
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr,"Usage: %s <team_id> <supplier_pid>\n", argv[0]);
        return EXIT_FAILURE;
    }
    int team = atoi(argv[1]);
    pid_t supplier = (pid_t)atoi(argv[2]);

    signal(SIGINT,  handle_sigint);
    signal(SIGUSR2, handle_no_more);
    srand(time(NULL) ^ getpid());

    AllRegions *B = ipc_init();
    printf("✅ Chef[%d] team=%d started\n", getpid(), team);

    bool signaled = false;

    // seed paste for patisserie teams
    if (team == ITEM_PASTE) {
      ipc_lock(RGN_RAW);
        B->raw.wheat -= recipe[ITEM_PASTE][I_WHEAT];
        B->raw.yeast -= recipe[ITEM_PASTE][I_YEAST];
      ipc_unlock(RGN_RAW);
      ipc_lock(RGN_PROG);
        B->prog.paste++;
      ipc_unlock(RGN_PROG);
      sleep(1);
    }

    while (running) {
        // — demand‐throttle —
        ipc_lock(RGN_STATS);
          unsigned tot_req = 0;
          for (int i = 0; i < NUM_ITEMS; ++i)
            tot_req += B->stats.requested[i];
        ipc_unlock(RGN_STATS);
        if (tot_req == 0) {
            sleep(1);
        } else {
            ipc_lock(RGN_STATS);
              double rate = B->stats.chef_work_rate[team];
            ipc_unlock(RGN_STATS);
            if (rate < 0.05) { sleep(2); continue; }
            sleep((int)ceil(1.0/rate));
        }

        // ——— PIVOT LOGIC ———
        unsigned req[NUM_ITEMS], serv[NUM_ITEMS];
        ipc_lock(RGN_STATS);
          for (int i = 0; i < NUM_ITEMS; ++i) {
            req[i]  = B->stats.requested[i];
            serv[i] = B->stats.served[i];
          }
        ipc_unlock(RGN_STATS);

        long demand[NUM_ITEMS];
        for (int i = 0; i < NUM_ITEMS; ++i)
          demand[i] = (long)req[i] - (long)serv[i];

        int my_item = team;
        unsigned my_stock = 0;
        ipc_lock(RGN_FIN);
          switch (my_item) {
            case ITEM_PASTE:      my_stock = B->fin.bread;           break;
            case ITEM_CAKE:       my_stock = B->fin.cakes;           break;
            case ITEM_SANDWICH:   my_stock = B->fin.sandwiches;      break;
            case ITEM_SWEET:      my_stock = B->fin.sweets;          break;
            case ITEM_SWEET_PAT:  my_stock = B->fin.sweet_pastries;  break;
            case ITEM_SAVORY_PAT: my_stock = B->fin.savory_pastries; break;
          }
        ipc_unlock(RGN_FIN);

        if (my_stock >= MAX_INVENTORY) {
          long best = 0; int best_idx = -1;
          for (int i = 0; i < NUM_ITEMS; ++i) {
            if (demand[i] > best) {
              best = demand[i];
              best_idx = i;
            }
          }
          if (best_idx >= 0) {
            my_item = best_idx;
            printf("🔄 Chef[%d] pivot → %s (demand=%ld)\n",
                   getpid(), item_name[my_item], best);
          }
        }
        // — end pivot ——

        // — patisserie wait on paste —
        if ((my_item == ITEM_SWEET_PAT || my_item == ITEM_SAVORY_PAT) && !no_more_refills) {
            bool got = false;
            while (running && !got && !no_more_refills) {
                ipc_lock(RGN_PROG);
                  if (B->prog.paste > MIN_PASTE_BUF) {
                    B->prog.paste--;
                    got = true;
                  }
                ipc_unlock(RGN_PROG);
                if (!got) sleep(1);
            }
            if (!got && no_more_refills) break;
        }

        // — sandwich wait on bread —
        if (my_item == ITEM_SANDWICH) {
            unsigned bread;
            ipc_lock(RGN_FIN); bread = B->fin.bread; ipc_unlock(RGN_FIN);
            if (bread <= MIN_BREAD_BUF) { sleep(1); continue; }
            ipc_lock(RGN_FIN); B->fin.bread--; ipc_unlock(RGN_FIN);
        }

        // — check raw stock vs recipe[my_item] —
        int have[NUM_INGS];
        ipc_lock(RGN_RAW);
          for (int i = 0; i < NUM_INGS; ++i)
            have[i] = ((unsigned *)&B->raw)[i];
        ipc_unlock(RGN_RAW);

        bool can = true;
        for (int i = 0; i < NUM_INGS; ++i) {
            if (have[i] < recipe[my_item][i]) {
                can = false;
                break;
            }
        }
        if (!can) {
            if (!signaled) {
                kill(supplier, SIGUSR1);
                signaled = true;
            }
            sleep(1);
            continue;
        }
        signaled = false;

        // — consume raw —
        ipc_lock(RGN_RAW);
          for (int i = 0; i < NUM_INGS; ++i)
            ((unsigned *)&B->raw)[i] -= recipe[my_item][i];
        ipc_unlock(RGN_RAW);

        // — deposit result —
        if (my_item == ITEM_SANDWICH) {
            ipc_lock(RGN_FIN);
              B->fin.bread--;
              B->fin.sandwiches++;
              printf("🥪 Chef[%d] sandwich → %u\n", getpid(), B->fin.sandwiches);
            ipc_unlock(RGN_FIN);
        } else {
            ipc_lock(RGN_PROG);
              switch (my_item) {
                case ITEM_PASTE:     B->prog.paste++;  
                      printf("🥣 Chef[%d] prepared paste (total %u)\n", getpid(), B->prog.paste);     
                  break;
                case ITEM_CAKE:       B->prog.cake_prepared++;
                      printf("🍰 Chef[%d] prepared cake (total %u)\n", getpid(), B->prog.cake_prepared);    
                  break;
                case ITEM_SWEET:      B->prog.sweet_prepared++; 
                    printf("🧁 Chef[%d] prepared sweet (total %u)\n", getpid(), B->prog.sweet_prepared);   
                  break;
                case ITEM_SWEET_PAT:  B->prog.sweet_patisserie_prepared++;
                    printf("🍥 Chef[%d] prepared sweet patisserie (total %u)\n", getpid(), B->prog.sweet_patisserie_prepared);  
                  break;
                case ITEM_SAVORY_PAT: B->prog.savory_patisserie_prepared++;  
                      printf("🥧 Chef[%d] prepared savory patisserie (total %u)\n", getpid(), B->prog.savory_patisserie_prepared); 
                  break;
              }
              printf("📦 Chef[%d] %s done\n", getpid(), item_name[my_item]);
            ipc_unlock(RGN_PROG);
        }
    }

    shmdt(B);
    printf("👋 Chef[%d] exiting\n", getpid());
    return EXIT_SUCCESS;
}