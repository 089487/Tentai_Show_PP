# Compiler
CC = gcc

# Compiler flags
CFLAGS = -O3 -Wall
OMPFLAGS = -fopenmp

# C++23 refactor target (non-intrusive)
CXX = g++
CXXFLAGS = -O3 -Wall -std=c++23

# Targets
all: openmp_solver_cpp

openmp_solver: openmp_solver.c
	$(CC) $(CFLAGS) $(OMPFLAGS) -o openmp_solver openmp_solver.c

openmp_solver_cpp: openmp_solver.cpp
	$(CXX) $(CXXFLAGS) $(OMPFLAGS) -o openmp_solver_cpp openmp_solver.cpp

seq_solver: seq_solver.cpp
	$(CXX) $(CXXFLAGS) -o seq_solver_cpp seq_solver.cpp

clean:
	rm -f openmp_solver seq_solver seq_solver_cpp openmp_solver_cpp
