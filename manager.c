/* manager.c */
#include "header.h"

#define SHM_KEY 0x6001
#define SEM_KEY 0x6002

static const int team_to_item[6] = { 0, 5, 1, 2, 4, 3 };
#define MIN_FIN_STOCK    2
#define MAX_INVENTORY    7

static volatile sig_atomic_t running = 1;
static pid_t *supplier_pids, *chef_pids, *baker_pids, *seller_pids;
static pid_t supplier_leader = 0;
static int supplier_count, chef_count, baker_count, seller_count;
pid_t gui_pid = 0;

static void sigterm_handler(int sig)
{
    (void)sig;
    puts("[manager] terminating, cleaning up IPC…");
    // kill_all_children();    // shut down your workers
    ipc_cleanup();
    _exit(EXIT_SUCCESS);
}

int rand_between(int lo, int hi)
{
    return lo + rand() % (hi - lo + 1);
}

int main(void)
{
    // Handle Ctrl-C and all kill signals
    signal(SIGINT, sigterm_handler);
    signal(SIGTERM, sigterm_handler);
    signal(SIGQUIT, sigterm_handler);
    signal(SIGALRM, sigterm_handler);
    srand((unsigned)time(NULL) ^ getpid());

    //////  ----------------                solve invalid semp error , but tried removing it didnt apear again so keep it for (27tyat)
    // int semid = semget(SEM_KEY, 0, 0);
    // if (semid >= 0) semctl(semid, 0, IPC_RMID);

    // int shmid = shmget(SHM_KEY, 0, 0);
    // if (shmid >= 0) shmctl(shmid, IPC_RMID, NULL);

    // Load configuration
    if (load_config("config.txt") < 0)
    {
        fprintf(stderr, "Failed to load config.txt\n");
        return EXIT_FAILURE;
    }
    printf("Config: suppliers=%d chefs=%d bakers=%d sellers=%d time=%dmin arrival=%.2f\n",
           cfg.supply_chain, cfg.chefs, cfg.bakers, cfg.sellers,
           cfg.max_time_minutes, cfg.arrival_prob);

    gui_pid = fork();
    if (gui_pid < 0)
    {
        perror("fork GUI");
        exit(EXIT_FAILURE);
    }
    else if (gui_pid == 0)
    {
        execl("./bakery_gui", "GUI", NULL);
        perror("execl GUI");
        exit(EXIT_FAILURE);
    }
    sleep(3);

    // Initialize unified SHM & semaphores
    alarm(cfg.max_time_minutes * 60);
    AllRegions *B = ipc_init();
    ////////------------------------------------------------- EditedMIDN      -------------------------------/////////////////

    // — initialize the stats counters & give every chef team a 1/6 “focus weight” —
    ipc_lock(RGN_STATS);
    memset(&B->stats, 0, sizeof(B->stats));
    for (int i = 0; i < NUM_ITEMS; ++i)
    {
        B->stats.requested[i] = 0;
        B->stats.served[i] = 0;
    }
    for (int t = 0; t < 6; ++t)
    {
        B->stats.chef_work_rate[t] = 1.0 / 6.0;
    }
    ipc_unlock(RGN_STATS);
    ////////------------------------------------------------- EditedMIDN      -------------------------------/////////////////

    // atexit(ipc_cleanup);
    ////        ************************ Cust-Seller  ********************** ////
    key_t key = ftok(MSG_PATH, MSG_PROJID); // System V key
    if (key == (key_t)-1)
    {
        perror("manager: ftok");
        // shmdt(B);
        return EXIT_FAILURE;
    }
    int msqid = msgget(key, IPC_CREAT | 0666); // get/create the queue
    if (msqid < 0)
    {
        perror("manager: msgget");
        // shmdt(B);
        return EXIT_FAILURE;
    }
    key_t key_gui = ftok(MSG_PATH, MSG_PROJID+1); // System V key
    if (key_gui == (key_t)-1)
    {
        perror("manager: ftok");
        // shmdt(B);
        return EXIT_FAILURE;
    }
    int msqid_gui = msgget(key_gui, IPC_CREAT | 0666); // get/create the gui queue
    if (msqid_gui < 0)
    {
        perror("manager: msgget");
        // shmdt(B);
        return EXIT_FAILURE;
    }
    printf("[manager] customer↔seller queue ready (msqid=%d)\n", msqid);

    ////        ************************ Cust-Seller  ********************** ///
    printf("💰 Current Prices:\n");
    printf("  bread:            %d\n", cfg.price_bread);
    printf("  sandwich:         %d\n", cfg.price_sandwich);
    printf("  sweets:           %d\n", cfg.price_sweet);
    printf("  savory_pastry:    %d\n", cfg.price_savory_pastries);
    printf("  sweet_pastry:     %d\n", cfg.price_sweet_pastries);
    printf("  cake:             %d\n", cfg.price_cake);
    putchar('\n');

    // --- Initialize supplier warehouse (new) ---
    srand((unsigned)time(NULL));
    ipc_lock(RGN_WAREHOUSE);
    B->warehouse.wheat = rand_between(cfg.min_wheat, cfg.max_wheat);
    B->warehouse.yeast = rand_between(cfg.min_yeast, cfg.max_yeast);
    B->warehouse.butter = rand_between(cfg.min_butter, cfg.max_butter);
    B->warehouse.milk = rand_between(cfg.min_milk, cfg.max_milk);
    B->warehouse.sugar = rand_between(cfg.min_sugar, cfg.max_sugar);
    B->warehouse.salt = rand_between(cfg.min_salt, cfg.max_salt);
    B->warehouse.sweet_items = rand_between(cfg.min_sweet_items, cfg.max_sweet_items);
    B->warehouse.cheese = rand_between(cfg.min_cheese, cfg.max_cheese);
    B->warehouse.salami = rand_between(cfg.min_salami, cfg.max_salami);
    ipc_unlock(RGN_WAREHOUSE);
    printf("[manager] warehouse initialized: "
           "[%u %u %u %u %u %u %u %u %u]\n",
           B->warehouse.wheat, B->warehouse.yeast, B->warehouse.butter,
           B->warehouse.milk, B->warehouse.sugar, B->warehouse.salt,
           B->warehouse.sweet_items, B->warehouse.cheese, B->warehouse.salami);

    ////////------------------------------------------------- EditedMIDN      -------------------------------/////////////////
    // --- Clear chef-facing stock initially ---
    ipc_lock(RGN_RAW);
    B->raw.wheat = cfg.max_wheat;
    B->raw.yeast = cfg.max_yeast;
    B->raw.butter = cfg.max_butter;
    B->raw.milk = cfg.max_milk;
    B->raw.sugar = cfg.max_sugar;
    B->raw.salt = cfg.max_salt;
    B->raw.sweet_items = cfg.max_sweet_items;
    B->raw.cheese = cfg.max_cheese;
    B->raw.salami = cfg.max_salami;
    ipc_unlock(RGN_RAW);
    // --- Start finished inventory at zero! ---
    ipc_lock(RGN_FIN);
    memset(&B->fin, 0, sizeof(B->fin));
    ipc_unlock(RGN_FIN);

    ////////------------------------------------------------- EditedMIDN      -------------------------------/////////////////
    ipc_lock(RGN_STATS);
    memset(&B->stats, 0, sizeof(B->stats));
    for (int i = 0; i < NUM_ITEMS; ++i)
    {
        B->stats.requested[i] = 0;
        B->stats.served[i] = 0;
    }
    for (int t = 0; t < 6; ++t)
        B->stats.chef_work_rate[t] = 1.0 / 6.0;
    ipc_unlock(RGN_STATS);
    ////////------------------------------------------------- EditedMIDN      -------------------------------/////////////////

    // Spawn suppliers
    // Spawn suppliers
    supplier_count = cfg.supply_chain;
    supplier_pids = calloc(supplier_count, sizeof(pid_t));
    for (int i = 0; i < supplier_count; ++i)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork supplier");
            exit(EXIT_FAILURE);
        }
        else if (pid == 0)
        {
            char idx[4];
            snprintf(idx, sizeof idx, "%d", i);
            execl("./supplier", "supplier", idx, NULL);
            perror("execl supplier");
            exit(EXIT_FAILURE);
        }
        supplier_pids[i] = pid;
        if (i == 0)
            supplier_leader = pid;
        printf("Supplier[%d] PID=%d%s\n", i, pid,
               i == 0 ? " (leader)" : "");
    }

    // Spawn chefs (6 fixed teams: 0–5)
    chef_count = cfg.chefs;
    chef_pids = calloc(chef_count, sizeof(pid_t));
    for (int i = 0; i < chef_count; ++i)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork chef");
            exit(EXIT_FAILURE);
        }
        else if (pid == 0)
        {
            char tid[8], sup[16];
            // Always modulo by 6, not cfg.chefs
            snprintf(tid, sizeof(tid), "%d", i % 6);
            // If your chef needs to signal suppliers, pass the leader
            snprintf(sup, sizeof(sup), "%d", supplier_leader);
            execl("./chef", "chef", tid, sup, NULL);
            perror("execl chef");
            exit(EXIT_FAILURE);
        }
        chef_pids[i] = pid;
        printf("Chef[%d] PID=%d (team %d)\n", i, pid, i % 6);
    }

    // Spawn bakers with team_id
    baker_count = cfg.bakers;
    baker_pids = calloc(baker_count, sizeof(pid_t));
    for (int i = 0; i < baker_count; ++i)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork baker");
            exit(EXIT_FAILURE);
        }
        else if (pid == 0)
        {
            char arg[8];
            snprintf(arg, sizeof(arg), "%d", i % 3);
            execl("./baker", "baker", arg, NULL);
            perror("execl baker");
            exit(EXIT_FAILURE);
        }
        baker_pids[i] = pid;
        printf("Baker[%d] PID=%d (team %d)\n", i, pid, i % 3);
    }

    // Spawn sellers
    seller_count = cfg.sellers;
    seller_pids = calloc(seller_count, sizeof(pid_t));
    for (int i = 0; i < seller_count; ++i)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork seller");
            exit(EXIT_FAILURE);
        }
        else if (pid == 0)
        {
            execl("./seller", "seller", NULL);
            perror("execl seller");
            exit(EXIT_FAILURE);
        }
        seller_pids[i] = pid;
        printf("Seller[%d] PID=%d\n", i, pid);
    }
    ////////------------------------------------------------- EditedMIDN      -------------------------------/////////////////

    // ── BLOCK until we have at least 1 of every product ──────────
    printf("[manager] waiting for initial stock of every product…\n");
    while (1)
    {
        FreshlyBakedGoods inv;
        ipc_lock(RGN_FIN);
        inv = B->fin;
        ipc_unlock(RGN_FIN);
        if (inv.bread >=  0&&
            inv.sandwiches >= 0 &&
            inv.sweets >= 0 &&
            inv.savory_pastries >= 0 &&
            inv.sweet_pastries >=  0 &&
            inv.cakes >= 0)
        {
            break;
        }
        sleep(1);
    }
    printf("[manager] initial buffer ready — now opening to customers\n");
    ////////------------------------------------------------- EditedMIDN      -------------------------------/////////////////

    sleep(7);
    // Main simulation loop: customers + stats check
    int total_sec = cfg.max_time_minutes * 60;

    pid_t *customer_pids = calloc(total_sec, sizeof(pid_t));
    if (!customer_pids)
    {
        perror("calloc customers");
        exit(EXIT_FAILURE);
    }
    int customer_count = 0;

    for (int t = 0; running && t < total_sec; ++t)
    {
        if ((double)rand() / RAND_MAX < cfg.arrival_prob)
        {
            pid_t cpid = fork();
            if (cpid < 0)
            {
                perror("fork customer");
            }
            else if (cpid == 0)
            {
                execl("./customer", "customer", NULL);
                perror("execl customer");
                exit(EXIT_FAILURE);
            }
            else
            {
                // Parent
                printf("[t=%3d] spawned customer PID=%d\n", t, cpid);
                customer_pids[customer_count++] = cpid;
            }
        }

        // reap any dead children (customers or others)
        while (waitpid(-1, NULL, WNOHANG) > 0)
        {
        }

        long profit;
        unsigned comp, frust, missing, customer_num;
        ipc_lock(RGN_STATS);
        profit = B->stats.profit;
        comp = B->stats.complained;
        frust = B->stats.frustrated;
        missing = B->stats.missing;
        customer_num = B->stats.current_customers;
        ipc_unlock(RGN_STATS);
    ////////------------------------------------------------- EditedMIDN      -------------------------------/////////////////

        ipc_lock(RGN_STATS);
        unsigned req[NUM_ITEMS], serv[NUM_ITEMS];
        for (int i = 0; i < NUM_ITEMS; ++i)
        {
            req[i] = B->stats.requested[i];
            serv[i] = B->stats.served[i];
        }
        ipc_unlock(RGN_STATS);

 // right after you read req[i] and serv[i]…
       // 1) Snapshot request/served counts
       ipc_lock(RGN_STATS);
         for (int i = 0; i < NUM_ITEMS; ++i) {
           req[i]  = B->stats.requested[i];
           serv[i] = B->stats.served[i];
         }
       ipc_unlock(RGN_STATS);

  // — snapshot current finished inventory —
// snapshot finished‐goods
FreshlyBakedGoods inv;
ipc_lock(RGN_FIN);
  inv = B->fin;
ipc_unlock(RGN_FIN);

// determine which teams have “room” (inventory < MAX_INVENTORY)
bool active[6];
int active_count = 0;
for (int t = 0; t < 6; ++t) {
    unsigned on_shelf;
    switch (team_to_item[t]) {
      case 0: on_shelf = inv.bread;           break;
      case 1: on_shelf = inv.sandwiches;      break;
      case 2: on_shelf = inv.sweets;          break;
      case 3: on_shelf = inv.savory_pastries; break;
      case 4: on_shelf = inv.sweet_pastries;  break;
      case 5: on_shelf = inv.cakes;           break;
      default: on_shelf = 7;      break;
    }
    active[t] = (on_shelf < 7);
    if (active[t]) active_count++;
}

// never let active_count drop to zero
if (active_count == 0) {
  for (int t = 0; t < 6; ++t) active[t] = true;
  active_count = 6;
}

// equal share among active teams
double base_rate = 1.0 / (double)active_count;

// apply and debug
// ipc_lock(RGN_STATS);
// printf("⚙️  [Debug] Allocating chef rates (MAX_INV=%d)\n", MAX_INVENTORY);
// for (int t = 0; t < 6; ++t) {
//     double rate = active[t] ? base_rate : 0.0;
//     B->stats.chef_work_rate[t] = rate;
//     printf("   Team %d (%s): %s cap(%u) → rate=%.2f\n",
//            t,
//            (t==0?"Paste":t==1?"Cakes":t==2?"Sandwich":t==3?"Sweets":
//             t==4?"SweetPat":"SavoryPat"),
//            active[t] ? "below" : "at/above",
//            (unsigned)(
//              (team_to_item[t]==0)?inv.bread:
//              (team_to_item[t]==1)?inv.sandwiches:
//              (team_to_item[t]==2)?inv.sweets:
//              (team_to_item[t]==3)?inv.savory_pastries:
//              (team_to_item[t]==4)?inv.sweet_pastries:
//                                   inv.cakes
//            ),
//            rate);
// }
// ipc_unlock(RGN_STATS);




    ////////------------------------------------------------- EditedMIDN      -------------------------------/////////////////

        if (profit >= cfg.max_profit ||
            comp >= cfg.max_complaints ||
            frust >= cfg.max_frustrated_customers ||
            missing >= cfg.max_missing_requests)
        {
            printf("Termination threshold reached at t=%d\n", t);
            break;
        }

        // print interim stats
        printf("=== Interim Stats at t=%d ===\n", t);
        printf("Profit: %ld\n", profit);
        printf("Complaints: %u\n", comp);
        printf("Frustrated: %u\n", frust);
        printf("Missing: %u\n", missing);
        printf("Current customers: %u\n", customer_num);

        sleep(1);
    }

    // Shutdown all processes
    running = 0;

    // existing workers
    for (int i = 0; i < supplier_count; ++i)
        kill(supplier_pids[i], SIGINT);
    for (int i = 0; i < chef_count; ++i)
        kill(chef_pids[i], SIGINT);
    for (int i = 0; i < baker_count; ++i)
        kill(baker_pids[i], SIGINT);
    for (int i = 0; i < seller_count; ++i)
        kill(seller_pids[i], SIGINT);
    // new: customers
    for (int i = 0; i < customer_count; ++i)
        kill(customer_pids[i], SIGINT);
    kill(gui_pid, SIGINT);
    // reap everybody
    while (waitpid(-1, NULL, 0) > 0)
    {
    }
    ////////------------------------------------------------- EditedMIDN      -------------------------------/////////////////
    // ── apply unsold‐inventory penalty ──────────────────────────
    
        FreshlyBakedGoods inv;
        long penalty = 0;
        ipc_lock(RGN_FIN);
        inv = B->fin;
        ipc_unlock(RGN_FIN);
        penalty += (long)inv.bread * cfg.price_bread;
        penalty += (long)inv.sandwiches * cfg.price_sandwich;
        penalty += (long)inv.sweets * cfg.price_sweet;
        penalty += (long)inv.savory_pastries * cfg.price_savory_pastries;
        penalty += (long)inv.sweet_pastries * cfg.price_sweet_pastries;
        penalty += (long)inv.cakes * cfg.price_cake;
        ipc_lock(RGN_STATS);
        B->stats.profit -= penalty;
        ipc_unlock(RGN_STATS);
        printf("[manager] unsold‐inventory penalty: -%ld \n",
               penalty);
    
    ////////------------------------------------------------- EditedMIDN      -------------------------------/////////////////

    // Final stats
    printf("=== Final Stats ===\n");
    printf("Profit: %ld\n", B->stats.profit);
    printf("Complaints: %u\n", B->stats.complained);
    printf("Frustrated: %u\n", B->stats.frustrated);
    printf("Left: %u\n", B->stats.left);
    printf("Missing requests: %u\n", B->stats.missing);
    printf("Current customers: %u\n", B->stats.current_customers);
// *Print remaining warehouse stock*
printf("\n=== Warehouse Stock Remaining ===\n");
printf("  wheat=%u  yeast=%u  butter=%u  milk=%u  sugar=%u  salt=%u\n"
       "  sweet_items=%u  cheese=%u  salami=%u\n",
       B->warehouse.wheat, B->warehouse.yeast,
       B->warehouse.butter, B->warehouse.milk,
       B->warehouse.sugar, B->warehouse.salt,
       B->warehouse.sweet_items, B->warehouse.cheese,
       B->warehouse.salami);
    // cleanup
    ipc_cleanup();
    if (msgctl(msqid, IPC_RMID, NULL) == -1) {
        perror("destroy_message_queue: msgctl(IPC_RMID) failed");
    }
    if (msgctl(msqid_gui, IPC_RMID, NULL) == -1) {
        perror("destroy_message_queue: msgctl(IPC_RMID) failed");
    }
    free(customer_pids);
    free(supplier_pids);
    free(chef_pids);
    free(baker_pids);
    free(seller_pids);
    return EXIT_SUCCESS;
}