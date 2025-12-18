#!/bin/bash
# Set number of threads first
export OMP_NUM_THREADS=${OMP_NUM_THREADS:-4}
echo "Running with ${OMP_NUM_THREADS} threads"

echo "Running on 5x5 puzzles"
python3 grader.py ./solver/openmp_solver_cpp ./data/5x5 120.0

echo "Running on 7x7 puzzles"
python3 grader.py ./solver/openmp_solver_cpp ./data/7x7 120.0

echo "Running on 10x10 puzzles"
python3 grader.py ./solver/openmp_solver_cpp ./data/10x10 120.0

echo "Running on 20x20 puzzles"
python3 grader.py ./solver/openmp_solver_cpp ./data/20x20 300.0
