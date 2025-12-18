# Compiler
CC = gcc

# Compiler flags
CFLAGS = -O3 -Wall
OMPFLAGS = -fopenmp

# C++23 refactor target (non-intrusive)
CXX = g++
CXXFLAGS = -O3 -Wall -std=c++23

# Targets
all: openmp_solver

openmp_solver: solver/openmp_solver.cpp
	$(CXX) $(CXXFLAGS) $(OMPFLAGS) -o solver/openmp_solver_cpp solver/openmp_solver.cpp

seq_solver: solver/seq_solver.cpp
	$(CXX) $(CXXFLAGS) -o solver/seq_solver_cpp solver/seq_solver.cpp

clean:
	rm -f solver/openmp_solver solver/seq_solver solver/seq_solver_cpp solver/openmp_solver_cpp