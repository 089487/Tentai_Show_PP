# Compiler
CC = gcc

# Compiler flags
CFLAGS = -O2 -Wall
OMPFLAGS = -fopenmp

# Targets
all: openmp_solver seq_solver

openmp_solver: openmp_solver.c
	$(CC) $(CFLAGS) $(OMPFLAGS) -o openmp_solver openmp_solver.c

seq_solver: seq_solver.c
	$(CC) $(CFLAGS) -o seq_solver seq_solver.c

clean:
	rm -f openmp_solver seq_solver
