#!/bin/bash

# Check for correct number of arguments
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <grid_size> <count>"
    echo "Example: $0 5x5 10"
    exit 1
fi

SIZE=$1
COUNT=$2
OUTPUT_DIR="data/$SIZE"

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Compile the generator if it doesn't exist or is older than source
if [ ! -f generator/simple_generator ] || [ generator/simple_generator.c -nt generator/simple_generator ]; then
    echo "Compiling generator..."
    gcc -o generator/simple_generator generator/simple_generator.c
    if [ $? -ne 0 ]; then
        echo "Compilation failed."
        exit 1
    fi
fi

echo "Generating $COUNT puzzles of size $SIZE..."

for ((i=0; i<COUNT; i++)); do
    # Format index with leading zero (2 digits)
    idx=$(printf "%02d" $i)
    
    # Run generator for 1 puzzle
    ./generator/simple_generator --size=$SIZE --count=1 --seed=$RANDOM > temp_puzzle.txt
    
    # Write the second line (Game ID) to .out
    # User instruction: "write the second line (the answer map) to 00.out"
    sed -n '2p' temp_puzzle.txt > "$OUTPUT_DIR/$idx.in"
    
    # Write the grid (starting from line 4) to .in
    # This serves as the input set
    tail -n +4 temp_puzzle.txt > "$OUTPUT_DIR/$idx.out"
    
    # Optional: Print progress
    # echo "Generated $idx"
done

# Clean up temporary file
rm temp_puzzle.txt

echo "Done. Generated files in $OUTPUT_DIR"
