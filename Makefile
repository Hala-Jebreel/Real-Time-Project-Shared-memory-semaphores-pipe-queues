CC = gcc
CFLAGS = -Wall -g

PROGS = bakery supplier chef baker seller customer bakery_gui

all: $(PROGS)

# Manager (orchestrator)
bakery: manager.o config.o ipc_utils.o 
	$(CC) $(CFLAGS) -o bakery manager.o config.o ipc_utils.o

# Supplier process
supplier: supplier.o ipc_utils.o config.o
	$(CC) $(CFLAGS) -o supplier supplier.o ipc_utils.o config.o

# Chef process
chef: chef.o ipc_utils.o config.o
	$(CC) $(CFLAGS) -o chef chef.o ipc_utils.o config.o -lm

# Baker process
baker: baker.o ipc_utils.o config.o
	$(CC) $(CFLAGS) -o baker baker.o ipc_utils.o config.o

# Seller process
seller: seller.o ipc_utils.o config.o
	$(CC) $(CFLAGS) -o seller seller.o ipc_utils.o config.o

# Customer generator
customer: customer.o ipc_utils.o config.o
	$(CC) $(CFLAGS) -o customer customer.o ipc_utils.o config.o

# Bakery GUI (requires OpenGL)
bakery_gui: bakery_gui.o ipc_utils.o config.o
	$(CC) $(CFLAGS) -o bakery_gui bakery_gui.o ipc_utils.o config.o -lGL -lGLU -lglut -lm

# Generic rule for .o files from .c
%.o: %.c struct.h config.h ipc_utils.h
	$(CC) $(CFLAGS) -c $<

# Run everything together
.PHONY: run
run: all
	@echo "→ Starting bakery manager in background…"
	@./bakery &                              # fork manager
	# @sleep 1                                 # give it a moment to set up shared mem
	# @echo "→ Launching bakery_gui…"
	# @./bakery_gui                           # start the GUI

# Clean up
clean:
	rm -f *.o $(PROGS)