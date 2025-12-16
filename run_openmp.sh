# Set number of threads first
export OMP_NUM_THREADS=4
python3 grader.py ./solver/openmp_solver ./data/5x5 120.0
python3 grader.py ./solver/openmp_solver ./data/7x7 120.0