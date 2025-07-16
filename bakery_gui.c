#define _POSIX_C_SOURCE 199309L
#include <GL/glut.h> // ← GLUT and OpenGL functions/macros
#include <GL/gl.h>   // ← OpenGL core
#include <math.h>
#include <stdio.h>
// #include <time.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define OVEN_CAPACITY 5
#include "ipc_utils.h"
#include "struct.h"
#include "config.h"
#include <time.h>
#include <sys/time.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static float current_time_seconds(void);

AllRegions *B = NULL; // Global pointer to shared memory
// linear interpolate a→b by t∈[0,1]
static float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}
// clamp x into [0,1]
static float clamp01(float x)
{
    return x < 0 ? 0 : (x > 1 ? 1 : x);
}

time_t sim_start_time;
//......................................................................
// ▶ Added: maximum number of simultaneous customers you expect
#define MAX_CUSTOMERS 128
static int next_customer_id = 1; // ▶ Added: unique ID generator
// ▶ Added: define a simple struct to track each customer
typedef struct
{
    pid_t pid;
    int status;              // ORDER_WAITING, ORDER_SERVED, etc.
    unsigned qty[NUM_ITEMS]; // last seen quantities
    int active;              // 1 if currently present, 0 otherwise
    float color[3];          // ▶ Added: RGB for this customer
    int id;                  // ▶ Added: simple 0…MAX_CUSTOMERS-1 index
} CustomerRecord;
// ▶ Added: array of customer records and count
static CustomerRecord customers[MAX_CUSTOMERS];
//.......................................................................
static int gui_qid; // Message queue ID for GUI

const int window_width = 1300;
const int window_height = 900;
// ✅ متغيرات global للعدادات من config.txt
int chef_count;
int baker_count;
int seller_count;
int supply_chain_units;
int customer_count = 20; // max number of drawn customers
int oven_count;          // ثابت حالياً، أو من config.txt

void draw_sky_and_celestial()
{
    // compute raw elapsed fraction in [0,1]
    time_t now = time(NULL);
    int elapsed = (int)difftime(now, sim_start_time);
    int total = cfg.max_time_minutes * 60;
    float t_raw = clamp01((float)elapsed / total);
    float t_fast   = clamp01(t_raw * 4.0f);

    // 1) Accelerated sky color: morning→midnight over 1/4 the game time
    //    morning = (1.0,0.95,0.85), night = (0.05,0.05,0.20)
// إلى قيم أغمق قليلاً:
    float r = lerp(1.0f, 0.25f, t_fast);
    float g = lerp(0.95f, 0.30f, t_fast);
    float b = lerp(0.85f, 0.50f, t_fast);

    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    glVertex2f(0, 0);
    glVertex2f(window_width, 0);
    glVertex2f(window_width, window_height);
    glVertex2f(0, window_height);
    glEnd();

    // 2) Celestial body in top-right
    const float cx = 1180, cy = 820;
    const float core_r = 30, halo_r = 60;

    if (t_raw < 0.5f)
    {
        // SUN: fade from yellow→white in first 12.5% of game time
        float sun_fade = clamp01(t_raw * 8.0f);
        // core
        float cr = lerp(1.0f, 1.0f, sun_fade);
        float cg = lerp(1.0f, 1.0f, sun_fade);
        float cb = lerp(0.0f, 1.0f, sun_fade);
        glColor3f(cr, cg, cb);
        glBegin(GL_POLYGON);
        for (int i = 0; i < 32; ++i)
        {
            float θ = 2 * M_PI * i / 32;
            glVertex2f(cx + cosf(θ) * core_r,
                       cy + sinf(θ) * core_r);
        }
        glEnd();
        // halo
        float halo_a = lerp(0.6f, 0.0f, sun_fade);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1, 1, 0, halo_a);
        glBegin(GL_POLYGON);
        for (int i = 0; i < 32; ++i)
        {
            float θ = 2 * M_PI * i / 32;
            glVertex2f(cx + cosf(θ) * halo_r,
                       cy + sinf(θ) * halo_r);
        }
        glEnd();
        glDisable(GL_BLEND);
    }
    else
    {
        // MOON: always white
        glColor3f(1, 1, 1);
        glBegin(GL_POLYGON);
        for (int i = 0; i < 32; ++i)
        {
            float θ = 2 * M_PI * i / 32;
            glVertex2f(cx + cosf(θ) * core_r,
                       cy + sinf(θ) * core_r);
        }
        glEnd();
    }
}

void draw_text(float x, float y, const char *text, void *font)
{
    glRasterPos2f(x, y);
    while (*text)
        glutBitmapCharacter(font, *text++);
}

void draw_bold_label(float x, float y, const char *label)
{
    glColor3f(0.1, 0.1, 0.1); // darker black
    draw_text(x, y, label, GLUT_BITMAP_HELVETICA_18);
}

void draw_small_label(float x, float y, const char *text)
{
    glColor3f(0, 0, 0); // now black text for shelf/product labels
    draw_text(x, y, text, GLUT_BITMAP_HELVETICA_12);
}

void draw_circle(float cx, float cy, float r)
{
    glBegin(GL_POLYGON);
    for (int i = 0; i < 20; ++i)
    {
        float theta = 2.0f * 3.14159f * i / 20;
        glVertex2f(cx + r * cosf(theta), cy + r * sinf(theta));
    }
    glEnd();
}

void draw_hat(float x, float y, int is_chef)
{
    if (is_chef)
        glColor3f(1, 1, 1); // white hat
    else
        glColor3f(0, 0, 0); // black hat

    glBegin(GL_QUADS);
    glVertex2f(x - 10, y + 10);
    glVertex2f(x + 10, y + 10);
    glVertex2f(x + 8, y + 22);
    glVertex2f(x - 8, y + 22);
    glEnd();
}

void draw_block_figure(float x, float y, float r, float g, float b, int happy, int hat_type, const char *label)
{
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    glVertex2f(x - 15, y + 20);
    glVertex2f(x + 15, y + 20);
    glVertex2f(x + 15, y + 50);
    glVertex2f(x - 15, y + 50);
    glEnd();

    glColor3f(1.0, 0.8, 0.6);
    draw_circle(x, y + 65, 10);
    if (hat_type >= 0)
        draw_hat(x, y + 65, hat_type);

    glColor3f(0, 0, 0);
    draw_circle(x - 3, y + 68, 1);
    draw_circle(x + 3, y + 68, 1);
    if (happy)
    {
        glBegin(GL_LINE_STRIP);
        for (float i = -3; i <= 3; i += 0.5)
            glVertex2f(x + i, y + 59 - 0.1 * i * i);
        glEnd();
    }
    else
    {
        glBegin(GL_LINES);
        glVertex2f(x - 3, y + 60);
        glVertex2f(x + 3, y + 60);
        glEnd();
    }

    glBegin(GL_LINES);
    glVertex2f(x - 20, y + 35);
    glVertex2f(x + 20, y + 35);
    glEnd();
    glBegin(GL_LINES);
    glVertex2f(x, y + 20);
    glVertex2f(x - 10, y);
    glVertex2f(x, y + 20);
    glVertex2f(x + 10, y);
    glEnd();

    draw_bold_label(x - 30, y - 10, label);
}

void draw_shelf(float x, float y, const char *item)
{
    glColor3f(0.5, 0.3, 0.1);
    glBegin(GL_QUADS);
    glVertex2f(x - 30, y);
    glVertex2f(x + 30, y);
    glVertex2f(x + 30, y + 8);
    glVertex2f(x - 30, y + 8);
    glEnd();
    draw_small_label(x - 20, y + 10, item);
}

// helper to center small text at x
static void draw_centered_label(float cx, float y, const char *text)
{
    float tw = 8.0f * strlen(text);
    draw_small_label(cx - tw / 2, y, text);
}

void draw_oven(float x, float y)
{
    // 1) oven box
    glColor3f(0.4f, 0.4f, 0.4f);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + 40, y);
    glVertex2f(x + 40, y + 40);
    glVertex2f(x, y + 40);
    glEnd();

    // 2) little indicator triangle (as before)
    glColor3f(1.0f, 0.3f, 0.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(x + 20, y + 5);
    glVertex2f(x + 15, y + 15);
    glVertex2f(x + 25, y + 15);
    glEnd();

    // 3) flickering fire inside oven
    float t = current_time_seconds() * 6.0f; // 6 Hz flicker
    // base orange → bright yellow
    float flame_r = lerp(0.8f, 1.0f, (sinf(t) * 0.5f + 0.5f));
    float flame_g = lerp(0.3f, 0.8f, (sinf(t + 1.0f) * 0.5f + 0.5f));
    glColor3f(flame_r, flame_g, 0.0f);

    // draw 3 overlapping flames as triangles
    for (int i = 0; i < 3; ++i)
    {
        float xi = x + 10 + i * 10;
        float baseY = y + 10;
        float peakY = baseY + 15 + 3 * sinf(t + i);
        glBegin(GL_TRIANGLES);
        glVertex2f(xi - 5, baseY);
        glVertex2f(xi + 5, baseY);
        glVertex2f(xi, peakY);
        glEnd();
    }

    // 4) label
    draw_bold_label(x + 5, y + 45, "Oven");
}

// helper to get a smoothly‐increasing time value
static float current_time_seconds()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9f;
}
void draw_fancy_counter(float x, float y, const char *label)
{
    // Counter base
    glColor3f(0.6, 0.3, 0.1); // rich wood color
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + 80, y);
    glVertex2f(x + 80, y + 40);
    glVertex2f(x, y + 40);
    glEnd();

    // Dark top stripe for 3D look
    glColor3f(0.3, 0.15, 0.05);
    glBegin(GL_QUADS);
    glVertex2f(x, y + 35);
    glVertex2f(x + 80, y + 35);
    glVertex2f(x + 80, y + 40);
    glVertex2f(x, y + 40);
    glEnd();

    // Label
    glColor3f(1, 1, 1); // white text
    draw_small_label(x + 10, y + 15, label);
}

void draw_menu_board(float x, float y, const char *title)
{
    glColor3f(0.2, 0.2, 0.5);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + 120, y);
    glVertex2f(x + 120, y + 40);
    glVertex2f(x, y + 40);
    glEnd();
    draw_small_label(x + 10, y + 15, title);
}

void draw_timer()
{
    // elapsed seconds since sim_start_time
    time_t now = time(NULL);
    int elapsed = (int)difftime(now, sim_start_time);

    // total allowed seconds from config
    int total_sec = cfg.max_time_minutes * 60;
    int rem = total_sec - elapsed;
    if (rem < 0)
        rem = 0;

    int mins = rem / 60;
    int secs = rem % 60;

    char buf[32];
    snprintf(buf, sizeof(buf), "Time Left: %02d:%02d", mins, secs);

    glColor3f(0, 0, 0);
    draw_text(1120, 870, buf, GLUT_BITMAP_HELVETICA_18);
}

void draw_layout()
{
    // 1) Clear screen and draw background
    // 1) sky that darkens over time + sun→moon
    glClear(GL_COLOR_BUFFER_BIT);
    draw_sky_and_celestial();

    // 2) Draw separators
    glColor3f(0.2f, 0.2f, 0.2f);
    glLineWidth(3);
    glBegin(GL_LINES);
    glVertex2f(0, 450);
    glVertex2f(window_width, 450);
    glVertex2f(600, 450);
    glVertex2f(600, window_height);
    glVertex2f(500, 0);
    glVertex2f(500, 450);
    glEnd();
    glLineWidth(1);

    // 3) Section headers and clock
    draw_bold_label(250, 870, "Chef Area");
    draw_bold_label(900, 870, "Bakery Area");
    draw_timer();

    // ─────────────────────────── Chef-Prepared Items ───────────────────────────
    {
        const char *icons[6] = {
            "icons/dough.png", "icons/sandwich.png", "icons/croissant.png",
            "icons/pizza.png", "icons/cake.png", "icons/donut.png"};
        const char *names[6] = {
            "Paste", "Sandwich", "Sweet Pastries",
            "Savory Pastries", "Cakes", "Sweet"};
        const float startX = 80, iconY = 800, spacing = 90;
        unsigned counts[6];
        ipc_lock(RGN_PROG);
        counts[0] = B->prog.paste;
        counts[1] = B->prog.sandwich_prepared;
        counts[2] = B->prog.sweet_patisserie_prepared;
        counts[3] = B->prog.savory_patisserie_prepared;
        counts[4] = B->prog.cake_prepared;
        counts[5] = B->prog.sweet_prepared;
        ipc_unlock(RGN_PROG);

        stbi_set_flip_vertically_on_load(1);
        for (int i = 0; i < 6; ++i)
        {
            int w, h, ch;
            unsigned char *img = stbi_load(icons[i], &w, &h, &ch, STBI_rgb_alpha);
            if (!img)
                continue;
            glPixelZoom(64.0f / w, 64.0f / h);
            glRasterPos2f(startX + i * spacing, iconY);
            glDrawPixels(w, h, GL_RGBA, GL_UNSIGNED_BYTE, img);
            glPixelZoom(1, 1);
            stbi_image_free(img);

            char label[32];
            sprintf(label, "%s: %u", names[i], counts[i]);
            draw_shelf(startX + i * spacing, iconY - 70, "");
            draw_small_label(
                startX + i * spacing - (strlen(names[i]) > 8 ? 30 : 20),
                iconY - 60,
                label);
        }
    }

    // ─────────────────────────── Ingredients (raw stock) ───────────────────────────
    {
        const char *icons[9] = {
            "icons/cheese.png", "icons/peanut-butter.png", "icons/salt.png",
            "icons/sugar.png", "icons/milk.png", "icons/butter.png",
            "icons/wheat.png", "icons/yeast.png", "icons/salami.png"};
        const char *names[9] = {
            "Cheese", "Peanut Butter", "Salt",
            "Sugar", "Milk", "Butter",
            "Wheat", "Yeast", "Salami"};
        unsigned counts[9];
        ipc_lock(RGN_WAREHOUSE);
        counts[0] = B->warehouse.cheese;
        counts[1] = B->warehouse.sweet_items;
        counts[2] = B->warehouse.salt;
        counts[3] = B->warehouse.sugar;
        counts[4] = B->warehouse.milk;
        counts[5] = B->warehouse.butter;
        counts[6] = B->warehouse.wheat;
        counts[7] = B->warehouse.yeast;
        counts[8] = B->warehouse.salami;
        ipc_unlock(RGN_WAREHOUSE);

        const float sx = 90, sy = 280, dx = 130, dy = -100;
        stbi_set_flip_vertically_on_load(1);
        for (int i = 0; i < 9; ++i)
        {
            int w, h, ch;
            unsigned char *img = stbi_load(icons[i], &w, &h, &ch, STBI_rgb_alpha);
            if (!img)
                continue;
            int col = i % 3, row = i / 3;
            float x = sx + col * dx, y = sy + row * dy;
            glPixelZoom(64.0f / w, 64.0f / h);
            glRasterPos2f(x, y);
            glDrawPixels(w, h, GL_RGBA, GL_UNSIGNED_BYTE, img);
            glPixelZoom(1, 1);
            stbi_image_free(img);

            draw_small_label(x + 16, y - 10, names[i]);
            char buf[16];
            sprintf(buf, "%u", counts[i]);
            draw_small_label(x + 28, y - 25, buf);
        }
    }

    // ─────────────────────────── Chef avatars ───────────────────────────
    {
        // Define your 6 chef “teams”
        const char *chef_teams[6] = {
            "Paste Team", "Sandwich Team", "Sweet Pastries Team",
            "Savory Pastries Team", "Cakes Team", "Donut Team"};

        for (int i = 0; i < chef_count; ++i)
        {
            float x = 100 + (i % 5) * 120;
            float y = 630 - (i / 5) * 120; // lifted by +10px

            // Draw the chef avatar
            draw_block_figure(x, y, 1, 1, 1, 0, 1, "Chef");

            // Underneath, print “#i TeamName”
            char buf[32];
            int team_id = i % 6;
            snprintf(buf, sizeof buf, "#%d %s", i, chef_teams[team_id]);
            draw_centered_label(x, y - 25, buf);
        }
    }

    // ─────────────────────────── Ovens ───────────────────────────
    {
        const int ox[3] = {720, 840, 960}, oy = 780;
        unsigned b, c, p;
        ipc_lock(RGN_OVENS);
        b = B->ovens.bread_slots;
        c = B->ovens.cake_slots;
        p = B->ovens.patisserie_slots;
        ipc_unlock(RGN_OVENS);

        const char *olab[3] = {"Bread(2s)", "Cake/Sweet(4s)", "Savory Pastry(3s)"};

        for (int i = 0; i < 3; ++i)
        {
            // 1) draw the oven itself
            draw_oven(ox[i], oy);

            // 2) busy‐slot flame bars (as before)
            float slot_w = 40.0f / OVEN_CAPACITY;
            unsigned busy = (i == 0 ? b : (i == 1 ? c : p));
            glColor3f(1, 0.3f, 0);
            for (unsigned s = 0; s < busy; ++s)
            {
                float bx = ox[i] + 2 + s * slot_w, by = oy + 2;
                glBegin(GL_QUADS);
                glVertex2f(bx, by);
                glVertex2f(bx + slot_w - 1, by);
                glVertex2f(bx + slot_w - 1, by + 10);
                glVertex2f(bx, by + 10);
                glEnd();
            }

            // 3) label the oven type just above the shelf
            draw_small_label(ox[i] + 5, oy - 20, olab[i]);

            // 4) draw a shelf under the oven
            const float shelfY = oy - 30;
            draw_shelf(ox[i], shelfY, "");

            // 5) instead of “busy”, read the finished‐goods count
            ipc_lock(RGN_FIN);
            unsigned finished =
                (i == 0
                     ? B->fin.bread
                 : i == 1
                     ? B->fin.cakes + B->fin.sweets
                     : B->fin.sweet_pastries + B->fin.savory_pastries);
            ipc_unlock(RGN_FIN);

            // print “finished” centered on that shelf
            char buf[16];
            snprintf(buf, sizeof buf, "%u", finished);
            float textW = strlen(buf) * 6.0f; // ≈6px per digit
            draw_small_label(
                ox[i] - 30 + (60 - textW) / 2, // 60px shelf width
                shelfY - 10,
                buf);
        }
    }

    // ─────────────────────────── Baker grid (no shelves) ───────────────────────────
    {
        // Define your 3 baker “teams”
        const char *baker_teams[3] = {
            "Bread", "Cakes&Sweets", "Pastries"};

        const int maxCols = 5;
        const float startX = 700.0f, startY = 580.0f, dx = 110.0f, dy = -120.0f; // startY +10

        for (int i = 0; i < baker_count; ++i)
        {
            int col = i % maxCols, row = i / maxCols;
            float x = startX + col * dx;
            float y = startY + row * dy;
            draw_block_figure(x, y, 0, 0, 0, 0, 0, "Baker");
            char buf[32];
            snprintf(buf, sizeof buf, "#%d %s", i, baker_teams[i % 3]);
            draw_centered_label(x, y - 25, buf);
        }
    }

    // ─────────────────────────── Seller Area ───────────────────────────
    draw_bold_label(1100,350 ,"Seller Area");
    draw_fancy_counter(880, 290, "Cashier Desk");
 {
        char p[32]; sprintf(p,"Profit: $%ld", B->stats.profit);
        draw_small_label(880,270,p);
    }

// 1) Six fixed freshly-baked shelves, label + count in one
{
    const float sx = 700.0f, sy = 4.0f, dx = 100.0f;
    const char* base[6] = {
      "Bread", "Sandwich", "Sweet", "Savory", "Pastry", "Cake"
    };
    unsigned stock[6] = {
      B->fin.bread,
      B->fin.sandwiches,
      B->fin.sweets,
      B->fin.savory_pastries,
      B->fin.sweet_pastries,
      B->fin.cakes
    };

    for (int i = 0; i < 6; ++i) {
        float x = sx + i * dx, y = sy;
        // combine name + count into one string
        char labelWithCount[32];
        snprintf(labelWithCount, sizeof labelWithCount, "%s: %u", base[i], stock[i]);

        // draw the shelf and use the combined label
        draw_shelf(x, y, labelWithCount);
    }
}


    // 2) Seller avatars in rows below:
    {
        const int maxPerRow = 5;
        const float startX = 900.0f, startY = 340.0f, dx = 100.0f, dy = -120.0f;
        for (int i = 0; i < seller_count; ++i)
        {
            int col = i % maxPerRow, row = i / maxPerRow;
            float x = startX + col * dx, y = startY + row * dy;
            draw_block_figure(x, y, 1.0f, 0.5f, 0.3f, 0, -1, "Seller");
        }
    }
    // ─────────────────────────── Customers ───────────────────────────
    {
        static const char *itemLabels[NUM_ITEMS] = {
            "Bread", "Sandw", "Sweets",
            "SavPas", "SwPas", "Cake"};

        int drawn = 0, row = 0;
        for (int i = 0; i < MAX_CUSTOMERS; ++i)
        {
            if (customers[i].active == 0)
                continue;
            if (customers[i].active < 0)
                customers[i].active += 1; // mark as active
            int col = drawn % 4;
            row = drawn / 4;
            float x = 900.0f + col * 100.0f;
            float y = 100.0f + row * 120.0f;

            // draw the customer…
            int happy = (customers[i].status == ORDER_SERVED);
            draw_block_figure(
                x, y,
                customers[i].color[0],
                customers[i].color[1],
                customers[i].color[2],
                happy, -1,
                ({
                    static char buf[64];
                    snprintf(buf, sizeof buf, "[%d] %s",
                             customers[i].id,
                             customers[i].status == ORDER_WAITING ? "Waiting" : customers[i].status == ORDER_SERVED  ? "Served"
                                                                            : customers[i].status == ORDER_FRUSTATED ? "Frustrated":customers[i].status == ORDER_COMPLAINED?"Complained"
                                                                                                                     : "Left");
                    buf;
                }));

            // draw each non-zero qty in a vertical column under the block
            {
                const float startY = y - 20.0f; // 20px below the figure
                const float lineH = 12.0f;      // line height between entries
                char buf[16];
                int drawnQty = 0; // to compact the list

                for (int j = 0; j < NUM_ITEMS; ++j)
                {
                    if (customers[i].qty[j] == 0) // ▶ Skip zero
                        continue;

                    // e.g. "Bread:3"
                    snprintf(buf, sizeof buf, "%s:%u",
                             itemLabels[j], customers[i].qty[j]);

                    // shift downward by drawnQty * lineH
                    draw_small_label(
                        x - 20.0f,
                        startY - drawnQty * lineH,
                        buf);
                    drawnQty++;
                }
            }

            drawn++;
        }
    }

    // ─────────────────────────── Supply Chain ───────────────────────────
    draw_bold_label(280, 400, "Supply Chain");
    {
        const int maxPerRow = 5;
        const float startX = 90.0f;
        const float startY = 340.0f;
        const float dx = 80.0f;
        const float dy = -100.0f; // drop by 100px each new row

        for (int i = 0; i < supply_chain_units; ++i)
        {
            int col = i % maxPerRow;
            int row = i / maxPerRow;
            float x = startX + col * dx;
            float y = startY + row * dy;
            draw_block_figure(x, y, 0.1f, 0.8f, 0.1f, 0, -1, "");
        }
    }

    // ─────────────────────────── Menu ───────────────────────────
    {
        char labs[8][32];
        
        sprintf(labs[0],"Sandwich");
        sprintf(labs[1],"Cake");
        sprintf(labs[2],"Vanilla");
        sprintf(labs[3],"Chocolate:");
        sprintf(labs[4],"Strawberry");
        sprintf(labs[5],"Sweet pastries");
        sprintf(labs[6],"Salt pastries");
        sprintf(labs[7],"Bread");

        const char *icons[8] = {
            "icons/sandwich.png", "icons/cake.png", "icons/vanilla.png",
            "icons/chocolate_cake.png", "icons/strawbery.png",
            "icons/croissant.png", "icons/pizza.png", "icons/bread.png"};
        stbi_set_flip_vertically_on_load(1);
        for (int i = 0; i < 8; ++i)
        {
            int w, h, ch;
            unsigned char *img = stbi_load(icons[i], &w, &h, &ch, STBI_rgb_alpha);
            if (!img)
                continue;
            int x = 550, y = 410 - i * 55;
            glPixelZoom(32.0f / w, 32.0f / h);
            glRasterPos2f(x, y);
            glDrawPixels(w, h, GL_RGBA, GL_UNSIGNED_BYTE, img);
            glPixelZoom(1, 1);
            stbi_image_free(img);

            draw_small_label(x + 40, y + 10, labs[i]);
            char pr[16];
            int price = (i <= 4 ? cfg.price_cake : i == 5 ? cfg.price_sweet_pastries
                                               : i == 6   ? cfg.price_savory_pastries
                                                          : cfg.price_bread);
            sprintf(pr, "$%d", price);
            draw_small_label(x + 40, y - 10, pr);
        }
    }
}

void idle()
{
    OrderCart msg;
    while (msgrcv(gui_qid, &msg, sizeof(msg) - sizeof(long),
                  0, IPC_NOWAIT) > 0)
    {
        printf("[GUI] received message from customer %d and status %d\n",
               msg.pid, msg.status);
        // ▶ Added: find or allocate a record for this pid
        int idx = -1;
        for (int i = 0; i < MAX_CUSTOMERS; ++i)
        {
            if (customers[i].active && customers[i].pid == msg.pid)
            {
                idx = i;
                customers[i].active = 1;
                // ▶ Added: pick a color from a fixed palette
                static const float palette[][3] = {
                    {0.8f, 0.2f, 0.2f}, // red
                    {0.2f, 0.8f, 0.2f}, // green
                    {0.2f, 0.2f, 0.8f}, // blue
                    {0.8f, 0.8f, 0.2f}, // yellow
                    {0.8f, 0.2f, 0.8f}, // magenta
                    {0.2f, 0.8f, 0.8f}, // cyan
                };
                int pal_n = sizeof(palette) / sizeof(palette[0]);
                customers[i].color[0] = palette[i % pal_n][0];
                customers[i].color[1] = palette[i % pal_n][1];
                customers[i].color[2] = palette[i % pal_n][2];
                break;
            }
        }
        if (idx < 0)
        {
            for (int i = 0; i < MAX_CUSTOMERS; ++i)
            {
                if (!customers[i].active)
                {
                    idx = i;
                    customers[i].active = 1;
                    customers[i].pid = msg.pid;
                    customers[i].id = next_customer_id++;

                    // ▶ Assign a color when you first allocate the slot:
                    static const float palette[][3] = {
                        {0.8f, 0.2f, 0.2f},
                        {0.2f, 0.8f, 0.2f},
                        {0.2f, 0.2f, 0.8f},
                        {0.8f, 0.8f, 0.2f},
                        {0.8f, 0.2f, 0.8f},
                        {0.2f, 0.8f, 0.8f},
                    };
                    int pal_n = sizeof(palette) / sizeof(palette[0]);
                    customers[i].color[0] = palette[i % pal_n][0];
                    customers[i].color[1] = palette[i % pal_n][1];
                    customers[i].color[2] = palette[i % pal_n][2];
                    break;
                }
            }
        }
        if (idx < 0)
        {
            // exceeded MAX_CUSTOMERS
            continue;
        }

        // ▶ Added: update record
        customers[idx].status = msg.status;
        memcpy(customers[idx].qty, msg.qty, sizeof(msg.qty));

        // ▶ Added: if they left, mark inactive
        if (msg.status == ORDER_LEFT || msg.status == ORDER_SERVED|| msg.status == ORDER_COMPLAINED)
        {
            customers[idx].active = -5;
        }
    }

    // force continuous redraw
    glutPostRedisplay();
}

void display()
{
    glClear(GL_COLOR_BUFFER_BIT);
    draw_layout();
    glutSwapBuffers();
}

void init()
{

    glClearColor(1, 1, 1, 1);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, window_width, 0, window_height);
}

int main(int argc, char **argv)
{
    // Config C;
    load_config("config.txt"); //////////////////// DELTED THE &C

    chef_count = cfg.chefs;
    baker_count = cfg.bakers;
    seller_count = cfg.sellers;
    supply_chain_units = cfg.supply_chain;

    B = ipc_init();
    key_t gui_key = ftok(MSG_PATH, MSG_PROJID + 1); // must match customer’s projid+1
    if (gui_key == (key_t)-1)
    {
        perror("[GUI] ftok(gui)");
        exit(EXIT_FAILURE);
    }
    gui_qid = msgget(gui_key, 0666 | IPC_CREAT);
    if (gui_qid < 0)
    {
        perror("[GUI] msgget(gui)");
        exit(EXIT_FAILURE);
    }
    printf("[GUI] listening on queue %d for customer updates\n", gui_qid);

    if (B == NULL)
    {
        perror("[GUI] ipc_init() failed — is the simulation running?");
        exit(EXIT_FAILURE);
    }

    sim_start_time = time(NULL);
    // ▶ Added: initialize customer record array
    memset(customers, 0, sizeof(customers));

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(window_width, window_height);
    glutCreateWindow("Real-Time Bakery");

    init(); // 🔥 Ensure this is here!
    glutDisplayFunc(display);
    glutIdleFunc(display);
    glutIdleFunc(idle);

    glutMainLoop();
    return 0;
}