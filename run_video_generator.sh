#!/bin/bash

# Default values
INPUT_FILE="data/5x5/00.in"
FPS=30

# --- Script Usage ---
usage() {
    echo "Usage: $0 [-f <input_file>] [-r <framerate>]"
    echo "  -f: Path to the puzzle input file (default: ${INPUT_FILE})"
    echo "  -r: Framerate for the output video (default: ${FPS})"
    exit 1
}

# --- Parse Command-Line Arguments ---
while getopts ":f:r:" opt; do
    case ${opt} in
        f)
            INPUT_FILE=${OPTARG}
            ;;
        r)
            FPS=${OPTARG}
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
BASENAME=$(basename "${INPUT_FILE}" .in)
DIRNAME=$(dirname "${INPUT_FILE}")
SIZE_DIR=$(basename "${DIRNAME}")

HISTORY_FILE="history_${SIZE_DIR}_${BASENAME}.txt"
VIDEO_FILE="solution_${SIZE_DIR}_${BASENAME}.mp4"
FRAMES_DIR="frames_${SIZE_DIR}_${BASENAME}"
VISUALIZER_EXE="./solver/visualizer"
PYTHON_SCRIPT="visualizer.py"

# --- Check for ffmpeg ---
if ! command -v ffmpeg &> /dev/null; then
    echo "Error: ffmpeg is not installed. Please install it to continue."
    exit 1
fi

# --- Main Execution ---
echo "Configuration:"
echo "  Input file:   ${INPUT_FILE}"
echo "  History file: ${HISTORY_FILE}"
echo "  Frames dir:   ${FRAMES_DIR}"
echo "  Video file:   ${VIDEO_FILE}"
echo "  Framerate:    ${FPS}"
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
    exit 1
fi
echo "Solver finished."

# 3. Create frames directory
rm -rf "${FRAMES_DIR}"
mkdir -p "${FRAMES_DIR}"

# 4. Run the Python script to generate frames
echo "Running Python script to generate frames..."
uv run python3 "${PYTHON_SCRIPT}" "${HISTORY_FILE}" "${FRAMES_DIR}"
if [ $? -ne 0 ]; then
    echo "Error: Python script failed to generate frames."
    exit 1
fi
echo "Frame generation complete."

# 5. Run ffmpeg to create the video
echo "Running ffmpeg to encode video..."
ffmpeg -framerate "${FPS}" -i "${FRAMES_DIR}/frame_%05d.png" -c:v libx264 -pix_fmt yuv420p -y "${VIDEO_FILE}" >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "Error: ffmpeg failed to create video."
    exit 1
fi
echo "Video encoding complete."

# 6. Clean up frames and history file
echo "Cleaning up temporary files..."
rm -rf "${FRAMES_DIR}"
rm -f "${HISTORY_FILE}"

echo "-------------------------------------"
echo "✅ Video generation complete! Output saved to ${VIDEO_FILE}"
