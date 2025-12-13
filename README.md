# Tentai Show Puzzle Generator & Solver

## Data Generation

Use the `generate_data.sh` script to generate puzzle instances.

### Usage

```bash
./generate_data.sh <grid_size> <count>
```

- `<grid_size>`: The dimensions of the puzzle grid (e.g., `5x5`, `10x10`).
- `<count>`: The number of puzzles to generate.

### Example

To generate 10 puzzles of size 5x5:

```bash
./generate_data.sh 5x5 10
```

### Output Structure

The script creates a `data/` directory with the following structure:

```
data/
  └── <grid_size>/       # e.g., data/5x5/
      ├── 00.in          # Puzzle input (grid visualization)
      ├── 00.out         # Puzzle solution (Game ID string)
      ├── 01.in
      ├── 01.out
      └── ...
```

- `*.in`: Contains the visual representation of the puzzle grid.
- `*.out`: Contains the "Game ID" string which encodes the solution map.
