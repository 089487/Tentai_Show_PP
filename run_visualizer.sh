#!/bin/bash

# Default values
INPUT_FILE="data/5x5/00.in"
SPEED_MS=100

# --- Script Usage ---
usage() {
    echo "Usage: $0 [-f <input_file>] [-s <speed_ms>]"
    echo "  -f: Path to the puzzle input file (default: ${INPUT_FILE})"
    echo "  -s: Speed for the GIF in milliseconds per frame (default: ${SPEED_MS})"
    exit 1
}

# --- Parse Command-Line Arguments ---
while getopts ":f:s:" opt; do
    case ${opt} in
        f)
            INPUT_FILE=${OPTARG}
            ;;
        s)
            SPEED_MS=${OPTARG}
            ;;
        \?)
            echo "Invalid option: -${OPTARG}" >&2
            usage
            ;;
        :)
            echo "Option -${OPTARG} requires an argument." >&2
            usage
            ;;
    esac
done
shift $((OPTIND -1))

# --- Configuration ---
# Extract the base name of the input file to use for outputs
BASENAME=$(basename "${INPUT_FILE}" .in)
DIRNAME=$(dirname "${INPUT_FILE}")
SIZE_DIR=$(basename "${DIRNAME}")

# Output files
HISTORY_FILE="history_${SIZE_DIR}_${BASENAME}.txt"
GIF_FILE="solution_${SIZE_DIR}_${BASENAME}.gif"
VISUALIZER_EXE="./solver/visualizer"
PYTHON_SCRIPT="visualizer.py"

# --- Main Execution ---
echo "Configuration:"
echo "  Input file:   ${INPUT_FILE}"
echo "  History file: ${HISTORY_FILE}"
echo "  GIF file:     ${GIF_FILE}"
echo "  GIF speed:    ${SPEED_MS} ms/frame"
echo "-------------------------------------"

# 1. Compile the visualizer solver
echo "Building visualizer..."
make -C solver visualizer
if [ $? -ne 0 ]; then
    echo "Error: Failed to build the visualizer. Aborting."
    exit 1
fi
echo "Build complete."

# 2. Run the solver to generate the history file
echo "Running solver to generate search history..."
${VISUALIZER_EXE} "${INPUT_FILE}" --history "${HISTORY_FILE}"
if [ $? -ne 0 ]; then
    echo "Error: The solver failed. No history file generated."
    # No need to exit; Python script will handle missing file
fi
echo "Solver finished."

# 3. Run the Python script to create the GIF
if [ ! -f "${HISTORY_FILE}" ]; then
    echo "Warning: History file not found. Cannot generate GIF."
    exit 1
fi

echo "Running Python script to generate GIF..."
uv run "${PYTHON_SCRIPT}" "${HISTORY_FILE}" "${GIF_FILE}" "${SPEED_MS}"
if [ $? -ne 0 ]; then
    echo "Error: Python script failed to generate GIF."
    exit 1
fi

echo "-------------------------------------"
echo "✅ Visualization complete! Output saved to ${GIF_FILE}"

# Optional: Clean up the large history file
# read -p "Do you want to remove the history file '${HISTORY_FILE}'? [y/N] " -n 1 -r
# echo
# if [[ $REPLY =~ ^[Yy]$ ]]; then
#     rm "${HISTORY_FILE}"
#     echo "Removed ${HISTORY_FILE}."
# fi
