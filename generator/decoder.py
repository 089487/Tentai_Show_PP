#!/usr/bin/env python3
"""
Tentai Show Puzzle Decoder and Visualizer

Decodes puzzle strings from simple_generator and displays them.
"""

def decode_puzzle(game_id):
    """
    Decode a game ID string into grid dimensions and dot positions.
    
    Args:
        game_id: String like "7x7:MaMaMa..."
    
    Returns:
        tuple: (width, height, dots_list)
        dots_list contains (x, y, is_black) tuples
    """
    # Split dimensions and data
    dims, data = game_id.split(':')
    w, h = map(int, dims.split('x'))
    
    # Internal grid size (includes edges and vertices)
    sx, sy = 2*w+1, 2*h+1
    
    # Decode positions
    dots = []
    pos = 0
    
    for char in data:
        if char == 'M':
            # Regular dot (white/hollow)
            x, y = pos % sx, pos // sx
            dots.append((x, y, False))
            pos += 1
        elif char == 'B':
            # Black dot (filled)
            x, y = pos % sx, pos // sx
            dots.append((x, y, True))
            pos += 1
        elif 'a' <= char <= 'z':
            # Skip empty spaces
            skip_count = ord(char) - ord('a') + 1
            pos += skip_count
    
    return w, h, dots


def visualize_puzzle(w, h, dots):
    """
    Print a visual representation of the puzzle grid.
    
    Args:
        w, h: Grid dimensions (user-visible)
        dots: List of (x, y, is_black) tuples
    """
    sx, sy = 2*w+1, 2*h+1
    
    # Create grid
    grid = [[' ' for _ in range(sx)] for _ in range(sy)]
    
    # Fill in structure
    for y in range(sy):
        for x in range(sx):
            if x % 2 == 0 and y % 2 == 0:
                # Vertex
                grid[y][x] = '+'
            elif x % 2 == 0:
                # Vertical edge
                grid[y][x] = '│'
            elif y % 2 == 0:
                # Horizontal edge
                grid[y][x] = '─'
            else:
                # Tile (space for game play)
                grid[y][x] = ' '
    
    # Place dots
    for x, y, is_black in dots:
        if is_black:
            grid[y][x] = '●'  # Filled dot
        else:
            grid[y][x] = '○'  # Hollow dot
    
    # Print grid
    print(f"\nPuzzle Grid ({w}×{h}):")
    print("=" * (sx + 2))
    for row in grid:
        print(''.join(row))
    print("=" * (sx + 2))
    print(f"Total dots: {len(dots)}")


def export_to_csv(w, h, dots, filename):
    """
    Export dot positions to CSV file.
    
    Args:
        w, h: Grid dimensions
        dots: List of (x, y, is_black) tuples
        filename: Output CSV filename
    """
    with open(filename, 'w') as f:
        f.write("x,y,is_black\n")
        for x, y, is_black in dots:
            f.write(f"{x},{y},{1 if is_black else 0}\n")
    print(f"\nExported to {filename}")


def main():
    import sys
    
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python3 decoder.py <game_id>")
        print("  python3 decoder.py <game_id> --csv output.csv")
        print("\nExample:")
        print("  python3 decoder.py '7x7:MaMaMaMaMaMaMbMaM...'")
        return
    
    game_id = sys.argv[1]
    
    try:
        w, h, dots = decode_puzzle(game_id)
        
        print(f"\n{'='*50}")
        print(f"Puzzle: {w}×{h}")
        print(f"Internal grid: {2*w+1}×{2*h+1}")
        print(f"Number of dots: {len(dots)}")
        print(f"{'='*50}")
        
        # Print dot positions
        print("\nDot positions:")
        for i, (x, y, is_black) in enumerate(dots, 1):
            dot_type = "Black" if is_black else "White"
            print(f"  {i}. ({x:2d}, {y:2d}) - {dot_type}")
        
        # Visualize
        visualize_puzzle(w, h, dots)
        
        # Export to CSV if requested
        if '--csv' in sys.argv:
            csv_idx = sys.argv.index('--csv')
            if csv_idx + 1 < len(sys.argv):
                export_to_csv(w, h, dots, sys.argv[csv_idx + 1])
        
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()


if __name__ == '__main__':
    main()
