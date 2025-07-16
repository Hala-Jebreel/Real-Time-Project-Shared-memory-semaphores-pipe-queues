/* seller.c – unified SHM + 4 semaphores, multi‐item OrderCart API */


#include "header.h"   // ipc_init, ipc_lock, ipc_unlock
// #include "struct.h"   // AllRegions, FreshlyBakedGoods, OrderCart, NUM_ITEMS, OrderStatus
// #include "config.h"   // Config, load_config()

static volatile sig_atomic_t running = 1;
static void handle_sigint(int sig) { (void)sig; running = 0; }

int main(void) {
    signal(SIGINT, handle_sigint);  // CTRL-C handler

    // 1) Init shared memory & semaphores
    AllRegions *B = ipc_init();

    //2) Load config
    if (load_config("config.txt") < 0) {
        fprintf(stderr, "seller: failed to load config.txt\n");
        shmdt(B);
        return EXIT_FAILURE;
    }

    // 3) Open/create the message queue
    key_t key = ftok(MSG_PATH, MSG_PROJID);
    if (key == (key_t)-1) {
        perror("seller: ftok");
        shmdt(B);
        return EXIT_FAILURE;
    }
    int msqid = msgget(key, IPC_CREAT | 0666);
    if (msqid < 0) {
        perror("seller: msgget");
        shmdt(B);
        return EXIT_FAILURE;
    }

    printf("🛒 Seller[%d] running…\n", getpid());

    // Main loop
    while (running) {
        // Snapshot & display inventory
        ipc_lock(RGN_FIN);
          FreshlyBakedGoods inv = B->fin;
        ipc_unlock(RGN_FIN);

        printf("🔔 Inventory: "
               "bread=%u, sandwiches=%u, sweets=%u, "
               "savory_pastries=%u, sweet_pastries=%u, cakes=%u\n",
               inv.bread,
               inv.sandwiches,
               inv.sweets,
               inv.savory_pastries,
               inv.sweet_pastries,
               inv.cakes);

        // Drain the queue
        OrderCart cart;
        while (msgrcv(msqid, &cart,
                      sizeof(cart) - sizeof(long),
                      0, IPC_NOWAIT) >= 0)
        {


          bool can_serve = true;
            // A) Handle complaints
            if (cart.status == ORDER_COMPLAINED) {
                long refund = 0;
                for (int i = 0; i < NUM_ITEMS; ++i) {
                    int price = 0;
                    switch (i) {
                      case 0: price = cfg.price_bread;           break;
                      case 1: price = cfg.price_sandwich;        break;
                      case 2: price = cfg.price_sweet;           break;
                      case 3: price = cfg.price_savory_pastries; break;
                      case 4: price = cfg.price_sweet_pastries;  break;
                      case 5: price = cfg.price_cake;            break;
                    }
                    refund += (long)cart.qty[i] * price;
                }
                ipc_lock(RGN_STATS);
                  B->stats.profit -= refund;
                  printf("💸 Seller[%d]: complaint from %d, refunded %ld, profit now %ld\n",
                         getpid(), cart.pid, refund, B->stats.profit);
                ipc_unlock(RGN_STATS);
                continue;
            }

            // B) Check & deduct under one lock
//////////////// -------------------------------------- MIDEDIT ------------------------- ////////////////////
   ipc_lock(RGN_STATS);
     for(int i=0;i<NUM_ITEMS;++i)
       B->stats.requested[i] += cart.qty[i];
   ipc_unlock(RGN_STATS);
//////////////// -------------------------------------- MIDEDIT ------------------------- ////////////////////

            long revenue = 0;
            ipc_lock(RGN_FIN);
              // availability check
              for (int i = 0; i < NUM_ITEMS; ++i) {
                  unsigned *stock = NULL;
                  int price = 0;
                  switch (i) {
                    case 0: stock = &B->fin.bread;           price = cfg.price_bread;           break;
                    case 1: stock = &B->fin.sandwiches;      price = cfg.price_sandwich;        break;
                    case 2: stock = &B->fin.sweets;          price = cfg.price_sweet;           break;
                    case 3: stock = &B->fin.savory_pastries; price = cfg.price_savory_pastries; break;
                    case 4: stock = &B->fin.sweet_pastries; price = cfg.price_sweet_pastries;  break;
                    case 5: stock = &B->fin.cakes;           price = cfg.price_cake;            break;
                    default: continue;
                  }
                  if (cart.qty[i] > *stock) {
                      can_serve = false;
                      break;
                  }
              }
              // commit if possible
              if (can_serve) {
//////////////// -------------------------------------- MIDEDIT ------------------------- ////////////////////
               // — record what we actually gave them —
               ipc_lock(RGN_STATS);
                 for(int i=0; i<NUM_ITEMS; ++i)
                   B->stats.served[i] += cart.qty[i];
               ipc_unlock(RGN_STATS);
//////////////// -------------------------------------- MIDEDIT ------------------------- ////////////////////

                  for (int i = 0; i < NUM_ITEMS; ++i) {
                      unsigned *stock = NULL;
                      int price = 0;
                      switch (i) {
                        case 0: stock = &B->fin.bread;           price = cfg.price_bread;           break;
                        case 1: stock = &B->fin.sandwiches;      price = cfg.price_sandwich;        break;
                        case 2: stock = &B->fin.sweets;          price = cfg.price_sweet;           break;
                        case 3: stock = &B->fin.savory_pastries; price = cfg.price_savory_pastries; break;
                        case 4: stock = &B->fin.sweet_pastries; price = cfg.price_sweet_pastries;  break;
                        case 5: stock = &B->fin.cakes;           price = cfg.price_cake;            break;
                        default: continue;
                      }
                      unsigned before = *stock;
                      *stock -= cart.qty[i];
                      if (*stock > before)  // underflow clamp
                        *stock = 0;
                      revenue += (long)cart.qty[i] * price;
                  }
                  printf("🛍 Seller[%d]: served %d, revenue now %ld\n",
                         getpid(), cart.pid, revenue);
              }
            ipc_unlock(RGN_FIN);

            // C) Stats & status
            ipc_lock(RGN_STATS);
              if (can_serve) {
                  B->stats.profit += revenue;
                  cart.status      = ORDER_SERVED;
                  printf("💰 Seller[%d]: profit now %ld\n", getpid(), B->stats.profit);
              } else {
                  cart.status      = ORDER_REJECTED;
              }
            ipc_unlock(RGN_STATS);

            // D) Reply to customer
            cart.mtype = cart.pid;
            if (msgsnd(msqid, &cart, sizeof(cart)-sizeof(long), 0) < 0) {
                perror("seller: msgsnd reply");
            }
            OrderCart cart;
        }

        usleep(200000);
    }

    printf("👋 Seller[%d] exiting.\n", getpid());
    shmdt(B);
    return EXIT_SUCCESS;
}