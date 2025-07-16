#ifndef CONFIG_H
#define CONFIG_H

// Configuration parameters for the simulation
typedef struct {
    // Product counts
    int bread_categories;
    int sandwich_types;
    int cake_flavors;
    int sweet_flavors;
    int sweet_patisseries;
    int savory_patisseries;

    // Staff counts
    int chefs;
    int bakers;
    int sellers;
    int supply_chain;

    // Ingredient purchase ranges
    int min_wheat;    int max_wheat;
    int min_yeast;    int max_yeast;
    int min_butter;   int max_butter;
    int min_milk;     int max_milk;
    int min_sugar;    int max_sugar;
    int min_salt;     int max_salt;
    int min_sweet_items; int max_sweet_items;
    int min_cheese;   int max_cheese;    // newly added
    int min_salami;   int max_salami;    // newly added

    // Prices per item
    int price_bread;
    int price_sandwich;
    int price_cake;
    int price_sweet;
    int price_sweet_pastries;
    int price_savory_pastries;
    int price_cakes;

    // Simulation thresholds
    int max_frustrated_customers;
    int max_complaints;
    int max_missing_requests;
    int max_profit;
    int max_time_minutes;
    double arrival_prob;      // probability of new customer per minute
} Config;

// declare the one-and-only global
extern Config cfg;
// your loader function
int load_config(const char *filename);

#endif // CONFIG_H
