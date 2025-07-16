/* customer.c – places a multi-item cart order, handles stats, and can complain */

#include "header.h" // ipc_init, ipc_lock, ipc_unlock
/* parameters */
static const double want_prob[NUM_ITEMS] = {0.5, 0.3, 0.35, 0.02, 0.01, 0.1};
static const int max_qty[NUM_ITEMS] = {2, 2, 2, 2, 1, 2};
static double complained_prob = 0.40;   // 10% chance to complain about food
static unsigned int max_wait_time = 10; // secs before frustration
static double leave_growth_rate = 0.10; // +10% leave‐chance per sec
unsigned int current_complaints = 0;
static double leave_prob = 0.01; // leave probability
const double leave_step = 0.1;   /* how much leave_prob increases per new complaint */

/* shared‐memory pointer */
static AllRegions *B = NULL;

/* flags */
static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t timed_out = 0;

/* signal handlers */
static void handle_sigint(int sig)
{
    (void)sig;
    running = 0;
}
static void handle_timeout(int sig)
{
    (void)sig;
    timed_out = 1;
}

/* Build random cart */
static void build_random_order(unsigned int qty[NUM_ITEMS])
{
    for (int i = 0; i < NUM_ITEMS; ++i)
    {
        double r = (double)rand() / RAND_MAX;
        qty[i] = (r < want_prob[i]) ? (rand() % max_qty[i] + 1) : 0;
    }
}

int main(void)
{
    //-------------------------------------------------------------------------------
    signal(SIGINT, handle_sigint);
    signal(SIGALRM, handle_timeout);
    srand((unsigned)time(NULL) ^ getpid());

    /* 1) Init SHM & bump current_customers */
    B = ipc_init();
    ipc_lock(RGN_STATS);
    B->stats.current_customers++;
    current_complaints = B->stats.complained; // the initial complaines when the process initializes
    ipc_unlock(RGN_STATS);

    /* 2) Open seller’s queue */
    key_t key = ftok(MSG_PATH, MSG_PROJID);
    if (key == (key_t)-1)
    {
        perror("ftok");
        exit(EXIT_FAILURE);
    }
    int msqid = msgget(key, 0666);
    if (msqid < 0)
    {
        perror("msgget");
        exit(EXIT_FAILURE);
    }
    // Added: open GUI queue
    key_t gui_key = ftok(MSG_PATH, MSG_PROJID + 1); /* use distinct projid */
    if (gui_key == (key_t)-1)
    {
        perror("ftok gui");
        exit(EXIT_FAILURE);
    }
    int gui_qid = msgget(gui_key, 0666);
    if (gui_qid < 0)
    {
        perror("msgget gui");
        exit(EXIT_FAILURE);
    }
    //---------------------------------------------------------------------------------

    // /* 3) Build & send initial order */   Previous One
    // OrderCart order = {
    //     .mtype = 1,
    //     .pid = getpid(),
    //     .status = ORDER_WAITING};
    // build_random_order(order.qty);
    ////////------------------------------------------------- EditedMIDN      -------------------------------/////////////////
    /* 3) Build & send initial order (never allow 0-only carts) */
    OrderCart order = {
        .mtype = 1,
        .pid = getpid(),
        .status = ORDER_WAITING};
    do
    {
        build_random_order(order.qty);
        unsigned sum = 0;
        for (int i = 0; i < NUM_ITEMS; ++i)
            sum += order.qty[i];
        if (sum > 0)
            break;
        /* all zero → retry */
    } while (1);
    ////////------------------------------------------------- EditedMIDN      -------------------------------/////////////////

    /* 4) Start frustration timer */

    alarm(max_wait_time);

    /* 5) Receive loop: handle waiting, frustration/leave */
    OrderCart reply;
    time_t leave_start = 0;
    // ------------------------------------------------------------------------------------------------

    while (running)
    {
        //--------------------------------------------------------- send order
        printf("📤 Customer[%d] placing cart:", getpid());
        for (int i = 0; i < NUM_ITEMS; ++i)
            printf(" %u", order.qty[i]);
        printf("\n");

        if (msgsnd(msqid, &order, sizeof(order) - sizeof(long), 0) < 0)
        {
            perror("msgsnd");
            exit(EXIT_FAILURE);
        }
        // Added: notify GUI of new order
        if (msgsnd(gui_qid, &order, sizeof(order) - sizeof(long), 0) < 0)
        {
            perror("msgsnd gui new order");
        }

        //--------------------------------------------------------- block wait receive reply
        ssize_t got = msgrcv(msqid, &reply,
                             sizeof(reply) - sizeof(long),
                             getpid(), 0);
        // ----------------------------------------------------------handel reply
        if (reply.status == ORDER_SERVED)
        {
            printf("💰 Customer[%d]: served!\n", getpid());
            /* possibly complain */
            if ((double)rand() / RAND_MAX < complained_prob)
            {
                ipc_lock(RGN_STATS);
                B->stats.complained++;
                printf("[cust %d] ++complained → %u\n",
                       getpid(), B->stats.complained);
                ipc_unlock(RGN_STATS);

                /* send a cashback request */
                order.status = ORDER_COMPLAINED;
                if (msgsnd(msqid, &order, sizeof(order) - sizeof(long), 0) < 0)
                {
                    perror("msgsnd complaint");
                }
                else
                {
                    printf("📣 Customer[%d]: sent ORDER_COMPLAINED\n", getpid());
                }
                // Added: notify GUI of served or complaint
                if (msgsnd(gui_qid, &order, sizeof(order) - sizeof(long), 0) < 0)
                {
                    perror("msgsnd gui complaint");
                }
                shmdt(B);
                printf("👋 Customer[%d] exiting.\n", getpid());
                return EXIT_SUCCESS;
            }
            else
            {
                // Added: notify GUI of served or complaint
                if (msgsnd(gui_qid, &reply, sizeof(reply) - sizeof(long), 0) < 0)
                {
                    perror("msgsnd gui served");
                }
                shmdt(B);
                printf("👋 Customer[%d] exiting.\n", getpid());
                return EXIT_SUCCESS;
            }
            break; // exit loop
        }
        else if (reply.status == ORDER_REJECTED)
        {
            printf("❌ Customer[%d]: rejected -> wait and retry\n", getpid());
            ipc_lock(RGN_STATS);
            B->stats.missing++;
            ipc_unlock(RGN_STATS);
            int status = ORDER_WAITING;
            if (timed_out)
            {
                status = ORDER_FRUSTATED;
            }
            OrderCart retry_msg = {
                .mtype = 1,
                .pid = getpid(),
                .status = status};
            memcpy(retry_msg.qty, order.qty, sizeof(order.qty));
            // Added: notify GUI of rejected reply
            if (msgsnd(gui_qid, &retry_msg, sizeof(reply) - sizeof(long), 0) < 0)
            {
                perror("msgsnd gui rejected");
            }
            sleep(2); // wait a bit before retrying
        }

        // check other customers new complains
        ipc_lock(RGN_STATS);
        if (B->stats.complained > current_complaints)
        {
            unsigned int delta = B->stats.complained - current_complaints;
            leave_prob += leave_step * delta; /* bump leave_prob */
            if (leave_prob > 1.0)
                leave_prob = 1.0; /* cap at 1.0 */
            current_complaints = B->stats.complained;

            printf("[cust %d] complaints ↑%u, leave_prob=%.2f\n",
                   getpid(), delta, leave_prob);
        }
        ipc_unlock(RGN_STATS);
        double r = (double)rand() / RAND_MAX;
        if (r < leave_prob)
        {
            ipc_lock(RGN_STATS);
            B->stats.left++;
            if (B->stats.current_customers > 0)
                B->stats.current_customers--;
            printf("[cust %d] → left (rand=%.2f < leave_prob=%.2f)\n",
                   getpid(), r, leave_prob);
            ipc_unlock(RGN_STATS);
            shmdt(B);
            return EXIT_FAILURE;
        }

        /* frustration → leave logic */
        if (timed_out)
        {
            if (leave_start == 0)
            {
                leave_start = time(NULL);
                ipc_lock(RGN_STATS);
                B->stats.frustrated++;
                printf("[cust %d] ++frustrated → %u\n",
                       getpid(), B->stats.frustrated);
                ipc_unlock(RGN_STATS);
                // Build a frustration message to send to gui
                OrderCart frustr_msg = {
                    .mtype = 1,
                    .pid = getpid(),
                    .status = ORDER_FRUSTATED};
                // copy the order qty to the frustration message
                memcpy(frustr_msg.qty, order.qty, sizeof(order.qty));
                if (msgsnd(gui_qid, &frustr_msg,
                           sizeof(frustr_msg) - sizeof(long), 0) < 0)
                {
                    perror("msgsnd gui frustrated");
                }
            }
            double elapsed = difftime(time(NULL), leave_start);
            double p_leave = leave_growth_rate * elapsed;
            if (p_leave > 1.0)
                p_leave = 1.0;
            if ((double)rand() / RAND_MAX < p_leave)
            {
                ipc_lock(RGN_STATS);
                B->stats.left++;
                if (B->stats.current_customers > 0)
                    B->stats.current_customers--;
                printf("[customer %d] → left after %ds (p=%.2f)\n",
                       getpid(), (int)elapsed, p_leave);
                ipc_unlock(RGN_STATS);

                // send message to gui that customer left
                OrderCart left_msg = {
                    .mtype = 1,
                    .pid = getpid(),
                    .status = ORDER_LEFT};
                if (msgsnd(gui_qid, &left_msg,
                           sizeof(left_msg) - sizeof(long), 0) < 0)
                {
                    perror("msgsnd gui left (early)");
                }
                shmdt(B);
                return EXIT_FAILURE;
            }
        }
        usleep(200000);
    }

    alarm(0); // cancel timeout

    /* 7) Decrement current_customers and detach */
    ipc_lock(RGN_STATS);
    if (B->stats.current_customers > 0)
        B->stats.current_customers--;
    printf("[cust %d] --current_customers → %u\n",
           getpid(), B->stats.current_customers);
    ipc_unlock(RGN_STATS);

    // send message to gui that customer left
    OrderCart left_msg = {
        .mtype = 1,
        .pid = getpid(),
        .status = ORDER_LEFT};
    if (msgsnd(gui_qid, &left_msg,
               sizeof(left_msg) - sizeof(long), 0) < 0)
    {
        perror("msgsnd gui left (early)");
    }

    shmdt(B);
    printf("👋 Customer[%d] exiting.\n", getpid());
    return EXIT_SUCCESS;
}