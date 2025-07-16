#include "header.h"
// Trim leading and trailing whitespace (spaces, tabs, newlines) in-place
static void trim(char *s) {
    char *p = s, *end;

    // Skip leading
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);

    // Trim trailing
    end = s + strlen(s) - 1;
    while (end >= s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end-- = '\0';
    }
}
Config cfg;

int load_config(const char *filename) {
    // Zero out everything first
    //memset(cfg, 0, sizeof(*cfg));

    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("Failed to open config file");
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        // Strip comments
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';

        // Split key/value
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim(key);
        trim(val);
        if (*key == '\0' || *val == '\0') continue;

        // —— Product counts ——
        if (strcmp(key, "bread_categories") == 0) {
            cfg.bread_categories = atoi(val);
            continue;
        }
        if (strcmp(key, "sandwich_types") == 0) {
            cfg.sandwich_types = atoi(val);
            continue;
        }
        if (strcmp(key, "cake_flavors") == 0) {
            cfg.cake_flavors = atoi(val);
            continue;
        }
        if (strcmp(key, "sweet_flavors") == 0) {
            cfg.sweet_flavors = atoi(val);
            continue;
        }
        if (strcmp(key, "sweet_patisseries") == 0) {
            cfg.sweet_patisseries = atoi(val);
            continue;
        }
        if (strcmp(key, "savory_patisseries") == 0) {
            cfg.savory_patisseries = atoi(val);
            continue;
        }

        // —— Staff counts ——
        if (strcmp(key, "chefs") == 0) {
            cfg.chefs = atoi(val);
            continue;
        }
        if (strcmp(key, "bakers") == 0) {
            cfg.bakers = atoi(val);
            continue;
        }
        if (strcmp(key, "sellers") == 0) {
            cfg.sellers = atoi(val);
            continue;
        }
        if (strcmp(key, "supply_chain") == 0) {
            cfg.supply_chain = atoi(val);
            continue;
        }

        // —— Ingredient ranges (min:max) ——
        {
            int a, b;
            if (sscanf(val, "%d:%d", &a, &b) == 2) {
                if (strcmp(key, "wheat") == 0) {
                    cfg.min_wheat = a;       cfg.max_wheat = b;
                } else if (strcmp(key, "yeast") == 0) {
                    cfg.min_yeast = a;       cfg.max_yeast = b;
                } else if (strcmp(key, "butter") == 0) {
                    cfg.min_butter = a;      cfg.max_butter = b;
                } else if (strcmp(key, "milk") == 0) {
                    cfg.min_milk = a;        cfg.max_milk = b;
                } else if (strcmp(key, "sugar") == 0) {
                    cfg.min_sugar = a;  cfg.max_sugar = b;
                } else if (strcmp(key, "salt") == 0) {
                    cfg.min_salt  = a;  cfg.max_salt  = b;
                } else if (strcmp(key, "sweet_items") == 0) {
                    cfg.min_sweet_items = a; cfg.max_sweet_items = b;
                } else if (strcmp(key, "cheese") == 0) { 
                    cfg.min_cheese = a; cfg.max_cheese = b; 
                } else if (strcmp(key, "salami") == 0) { 
                    cfg.min_salami = a; cfg.max_salami = b; 
                }
                continue;
            }
        }

        // —— Prices ——
        if (strcmp(key, "price_bread") == 0) {
            cfg.price_bread = atoi(val);
            continue;
        }
        if (strcmp(key, "price_sandwich") == 0) {
            cfg.price_sandwich = atoi(val);
            continue;
        }
        if (strcmp(key, "price_cake") == 0) {
            cfg.price_cake = atoi(val);
            continue;
        }
        if (strcmp(key, "price_sweet") == 0) {
            cfg.price_sweet = atoi(val);
            continue;
        }
        if (strcmp(key, "price_sweet_patisserie") == 0) {
            cfg.price_sweet_pastries  = atoi(val);
            continue;
        }
        if (strcmp(key, "price_savory_patisserie") == 0) {
            cfg.price_savory_pastries  = atoi(val);
            continue;
        }

        // —— Simulation thresholds ——
        if (strcmp(key, "max_frustrated_customers") == 0) {
            cfg.max_frustrated_customers = atoi(val);
            continue;
        }
        if (strcmp(key, "max_complaints") == 0) {
            cfg.max_complaints = atoi(val);
            continue;
        }
        if (strcmp(key, "max_missing_requests") == 0) {
            cfg.max_missing_requests = atoi(val);
            continue;
        }
        if (strcmp(key, "max_profit") == 0) {
            cfg.max_profit = atoi(val);
            continue;
        }
        if (strcmp(key, "max_time_minutes") == 0) {
            cfg.max_time_minutes = atoi(val);
            continue;
        }
        // Arrival probability of new customers (0.0 - 1.0)
        if (strcmp(key, "arrival_prob") == 0) {
            cfg.arrival_prob = strtod(val, NULL);
            continue;
        }

        // any other keys? ignore
    }

    fclose(f);
    return 0;
}
