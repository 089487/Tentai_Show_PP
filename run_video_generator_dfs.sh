#!/bin/bash

# Default values
INPUT_FILE="data/10x10/01.in"
FPS=600
HOLD_DURATION=5

# --- Script Usage ---
usage() {
    echo "Usage: $0 [-f <input_file>] [-r <framerate>] [-d <hold_seconds>]"
    echo "  -f: Path to the puzzle input file (default: ${INPUT_FILE})"
    echo "  -r: Framerate for the output video (default: ${FPS})"
    echo "  -d: Seconds to hold the final frame (default: ${HOLD_DURATION})"
    exit 1
}

# --- Parse Command-Line Arguments ---
while getopts ":f:r:d:" opt; do
    case ${opt} in
        f)
            INPUT_FILE=${OPTARG}
            ;;
        r)
            FPS=${OPTARG}
            ;;
        d)
            HOLD_DURATION=${OPTARG}
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

HISTORY_FILE="history_dfs_${SIZE_DIR}_${BASENAME}.txt"
VIDEO_FILE="solution_dfs_${SIZE_DIR}_${BASENAME}.mp4"
FRAMES_DIR="frames_dfs_${SIZE_DIR}_${BASENAME}"
VISUALIZER_EXE="./solver/visualizer_dfs" # Use the DFS visualizer
PYTHON_SCRIPT="visualizer.py"

# Temporary files for concatenation
MAIN_VIDEO_TMP="main_video_tmp.mp4"
HOLD_VIDEO_TMP="hold_video_tmp.mp4"
CONCAT_LIST_TMP="concat_list.txt"


# --- Check for ffmpeg ---
if ! command -v ffmpeg &> /dev/null; then
    echo "Error: ffmpeg is not installed. Please install it to continue."
    exit 1
fi

# --- Main Execution ---
echo "Configuration:"
echo "  Input file:     ${INPUT_FILE}"
echo "  History file:   ${HISTORY_FILE}"
echo "  Frames dir:     ${FRAMES_DIR}"
echo "  Video file:     ${VIDEO_FILE}"
echo "  Framerate:      ${FPS}"
echo "  Final Hold (s): ${HOLD_DURATION}"
echo "-------------------------------------"

# 1. Compile the visualizer solver
echo "Building DFS visualizer..."
make -C solver visualizer_dfs
if [ $? -ne 0 ]; then
    echo "Error: Failed to build the visualizer. Aborting."
    exit 1
fi
echo "Build complete."

# 2. Run the solver to generate the history file
echo "Running DFS solver to generate search history..."
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

# Generate main video
ffmpeg -framerate "${FPS}" -i "${FRAMES_DIR}/frame_%05d.png" -c:v libx264 -pix_fmt yuv420p -y "${MAIN_VIDEO_TMP}" >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "Error: ffmpeg failed to create main video part."
    exit 1
fi

# Find last frame
LAST_FRAME=$(ls -v "${FRAMES_DIR}"/frame_*.png | tail -n 1)
if [ -z "${LAST_FRAME}" ]; then
    echo "Warning: Could not find last frame to generate hold. Skipping."
    mv "${MAIN_VIDEO_TMP}" "${VIDEO_FILE}"
else
    # Generate hold video
    ffmpeg -loop 1 -framerate "${FPS}" -i "${LAST_FRAME}" -t "${HOLD_DURATION}" -c:v libx264 -pix_fmt yuv420p -y "${HOLD_VIDEO_TMP}" >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo "Error: ffmpeg failed to create hold video part."
        exit 1
    fi

    # Create concatenation list
    echo "file '${MAIN_VIDEO_TMP}'" > "${CONCAT_LIST_TMP}"
    echo "file '${HOLD_VIDEO_TMP}'" >> "${CONCAT_LIST_TMP}"

    # Concatenate videos
    ffmpeg -f concat -safe 0 -i "${CONCAT_LIST_TMP}" -c copy -y "${VIDEO_FILE}" >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo "Error: ffmpeg failed to concatenate video parts."
        exit 1
    fi
fi
echo "Video encoding complete."

# 6. Clean up temporary files
echo "Cleaning up temporary files..."
rm -rf "${FRAMES_DIR}"
rm -f "${HISTORY_FILE}"
rm -f "${MAIN_VIDEO_TMP}" "${HOLD_VIDEO_TMP}" "${CONCAT_LIST_TMP}"

echo "-------------------------------------"
echo "✅ Video generation complete! Output saved to ${VIDEO_FILE}"
