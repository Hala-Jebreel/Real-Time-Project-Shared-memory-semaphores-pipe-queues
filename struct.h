// struct.h
#ifndef STRUCT_H
#define STRUCT_H
#include <sys/types.h>

#define NUM_ITEMS 6
#define NUM_CHEF_TEAMS   6

// Raw ingredient stock (supplier ↔ chef)
typedef struct {
    unsigned int wheat;        // units of wheat flour
    unsigned int yeast;        // units of yeast
    unsigned int butter;       // units of butter
    unsigned int milk;         // units of milk
    unsigned int sugar;   // combined units of sugar 
    unsigned int salt;
    unsigned int sweet_items;  // generic sweet ingredient units
    unsigned int cheese;       // units of cheese
    unsigned int salami;       // units of salami
} RawIng;

// Items prepared by chefs (chef ↔ baker)
typedef struct {
    unsigned int paste;
    unsigned int bread_prepared;
    unsigned int cake_prepared;
    unsigned int sweet_prepared;
    unsigned int sandwich_prepared;
    unsigned int sweet_patisserie_prepared;
    unsigned int savory_patisserie_prepared
} PreparedItems;

// Freshly baked goods (chef + baker ↔ seller)
typedef struct {
    unsigned int bread;
    unsigned int sandwiches;
    unsigned int sweets;
    unsigned int savory_pastries;
    unsigned int sweet_pastries;
    unsigned int cakes;
} FreshlyBakedGoods;

// Simulation statistics (seller ↔ customer)
typedef struct {
    long          profit;
    unsigned int  complained;
    unsigned int  frustrated;
    unsigned int  left;
    unsigned int  missing;
    unsigned       current_customers;
    // ← new demand & service counters per item
    unsigned int   requested[NUM_ITEMS];
    unsigned int   served   [NUM_ITEMS];
    // ← per-team “work rate” or focus factor
    double         chef_work_rate[6];
} Stats;


////                -----OVEN----       /////

// New: how many of each oven‐type are in use
typedef struct {
    unsigned bread_slots;
    unsigned cake_slots;
    unsigned patisserie_slots;
} OvenRegion;

// Combined shared‐memory layout
typedef struct {
    RawIng            raw;     // RGN_RAW
    RawIng            warehouse; // RGN_WAREHOUSE: supplier store
    PreparedItems     prog;    // RGN_PROG
    FreshlyBakedGoods fin;     // RGN_FIN
    Stats             stats;   // RGN_STATS
    OvenRegion        ovens;   // RGN_OVENS   ← new!
} AllRegions;

////                -----OVEN----       /////

// Customer → seller order message
///    -------------------------- NEEW ONES ---- /////

typedef enum {
    ORDER_WAITING  = 1,
    ORDER_SERVED   = 2,
    ORDER_REJECTED = 3,
    ORDER_COMPLAINED = 4,
    ORDER_FRUSTATED = 5,
    ORDER_LEFT = 6,
}OrderStatus;

typedef struct {
    long          mtype;         // request mtype (use 1)
    pid_t         pid;           // customer PID for reply
    unsigned int  qty[NUM_ITEMS]; // requested qty per item
    OrderStatus   status;        // reply status
} OrderCart;
///    -------------------------- NEEW ONES ---- /////


// Chef → supplier request message
typedef struct {
    long          mtype;      // use e.g. 1 for generic
    unsigned int  wheat_req;
    unsigned int  yeast_req;
    unsigned int  butter_req;
} SupplyReqMsg;

// // Configuration parameters for the simulation
// typedef struct {
//     // Product counts
//     int bread_categories;
//     int sandwich_types;
//     int cake_flavors;
//     int sweet_flavors;
//     int sweet_patisseries;
//     int savory_patisseries;

//     // Staff counts
//     int chefs;
//     int bakers;
//     int sellers;
//     int supply_chain;

//     // Ingredient purchase ranges
//     int min_wheat;    int max_wheat;
//     int min_yeast;    int max_yeast;
//     int min_butter;   int max_butter;
//     int min_milk;     int max_milk;
//     int min_sugar;    int max_sugar;
//     int min_salt;     int max_salt;
//     int min_sweet_items; int max_sweet_items;
//     int min_cheese;   int max_cheese;    // newly added
//     int min_salami;   int max_salami;    // newly added

//     // Prices per item
//     int price_bread;
//     int price_sandwich;
//     int price_cake;
//     int price_sweet;
//     int price_sweet_pastries;
//     int price_savory_pastries;
//     int price_cakes;

//     // Simulation thresholds
//     int max_frustrated_customers;
//     int max_complaints;
//     int max_missing_requests;
//     int max_profit;
//     int max_time_minutes;
//     double arrival_prob;      // probability of new customer per minute
// } Config;

#endif // STRUCT_H
