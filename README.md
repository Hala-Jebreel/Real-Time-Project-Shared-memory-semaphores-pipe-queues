# 🍞 Real-Time Bakery Management Simulation (Project 2)

## 📌 Overview

This project simulates a full-day operation of a bakery using **multi-processing** and various **Linux IPC mechanisms** such as semaphores, message queues, shared memory, and pipes. It is designed for **ENCS4330: Real-Time Applications & Embedded Systems** at **Birzeit University**.

The simulation models interactions between chefs, bakers, sellers, supply-chain workers, and customers. It features a graphical user interface (GUI) using **OpenGL** that visualizes real-time activity in the bakery.

---

## 🧩 Key Features

- 🧑‍🍳 Multiple chef teams prepare:
  - Paste
  - Cakes
  - Sandwiches
  - Sweets
  - Sweet & savory patisseries

- 🧁 Baker teams:
  - Bake cakes and sweets
  - Bake sweet and savory patisseries
  - Bake bread

- 🔄 Supply-chain workers:
  - Purchase ingredients like wheat, yeast, milk, butter, cheese, etc.
  - Trigger production halts if ingredients run out

- 🛍️ Customers:
  - Request bakery items with assigned prices
  - May complain or leave if unsatisfied or delayed

- 📉 Customer frustration and complaints may end the simulation
- 💰 Profit tracking with thresholds for simulation success

---

## 📂 Project Structure

.
├── src/ # Source files for each process (chefs, bakers, sellers, etc.)
├── gui/ # OpenGL-based bakery visualization
├── config.txt # User-defined parameters (thresholds, timings, ingredients)
├── Makefile # Build all executables
├── README.md # This file
└── project2_ipc_202502.pdf # Original project description



---

## ⚙️ Configuration (config.txt)

All dynamic parameters are defined in `config.txt`. This includes:

- Number of chefs, bakers, sellers, supply-chain workers
- Ingredient ranges
- Customer behavior thresholds (complaints, frustration)
- Time limits and profit targets

This approach avoids hardcoding and enables easy tuning of simulation settings.

---

## 🚀 Running the Simulation

### 🧱 Requirements

- Linux OS
- GCC compiler
- OpenGL development libraries (`sudo apt install freeglut3-dev`)
- POSIX IPC support

### 🔧 Build

```bash
make
▶️ Run

./main config.txt
This will initialize all bakery processes and begin the simulation.

🖼️ Launch the GUI

./bakery_gui
The GUI displays real-time status: customers, chefs, ovens, ingredients, shelves, and profit.

🔗 IPC Mechanisms Used
Shared Memory: For ingredient stock and bakery status

Message Queues: For reporting customer satisfaction and complaints

Semaphores: For resource synchronization (ovens, shared shelves)

Signals: For process coordination

Pipes/FIFOs: For chef-to-baker and seller-to-customer communication

🎯 Simulation End Conditions
The simulation stops when:

💢 Too many customers complain

🕓 Simulation time exceeds limit

😡 Too many frustrated customers

❌ Missing item requests exceed threshold

✅ Profit exceeds target

