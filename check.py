import sys
import re

def parse_output(output_text):
    lines = output_text.strip().split('\n')
    
    # Find the grid
    size_match = re.search(r"Puzzle Grid \((\d+)x(\d+)\):", output_text)
    if not size_match:
        return None, "Could not find puzzle dimensions"
    
    w = int(size_match.group(1))
    h = int(size_match.group(2))
    
    start_idx = -1
    for i, line in enumerate(lines):
        if line.startswith("Puzzle Grid"):
            start_idx = i + 2 # Skip title and top border
            break
            
    if start_idx == -1:
        return None, "Could not find grid start"

    sy = 2 * h + 1
    if start_idx + sy > len(lines):
        return None, "Output truncated"
        
    grid_rows = lines[start_idx : start_idx + sy]
    
    if len(grid_rows) != sy:
        return None, f"Expected {sy} grid rows, found {len(grid_rows)}"
        
    return {
        'w': w,
        'h': h,
        'rows': grid_rows
    }, None

def validate_solution(data):
    w = data['w']
    h = data['h']
    rows = data['rows']
    
    dots = []
    adj = {} # (tx, ty) -> list of (ntx, nty)
    
    # Initialize all tiles
    tiles = []
    for ty in range(h):
        for tx in range(w):
            tiles.append((tx, ty))
            adj[(tx, ty)] = []

    # Parse dots and connections
    for y in range(len(rows)):
        line = rows[y]
        # Pad line if short
        if len(line) < 2 * w + 1:
            line = line + " " * (2 * w + 1 - len(line))
            
        for x in range(len(line)):
            char = line[x]
            
            # Check for dots
            if char in ['●', '○']:
                dots.append({'x': x, 'y': y, 'color': char})
            
            # Check connections (absence of walls)
            # Vertical edge check (x is even, y is odd) -> connects (x/2-1, y/2) and (x/2, y/2)
            if x % 2 == 0 and y % 2 == 1:
                if char == ' ' or char in ['●', '○']: # No wall (dots can be on edges)
                    tx1 = (x // 2) - 1
                    tx2 = x // 2
                    ty = (y - 1) // 2
                    if 0 <= tx1 < w and 0 <= tx2 < w:
                        adj[(tx1, ty)].append((tx2, ty))
                        adj[(tx2, ty)].append((tx1, ty))
            
            # Horizontal edge check (x is odd, y is even) -> connects (x/2, y/2-1) and (x/2, y/2)
            if x % 2 == 1 and y % 2 == 0:
                if char == ' ' or char in ['●', '○']: # No wall
                    tx = (x - 1) // 2
                    ty1 = (y // 2) - 1
                    ty2 = y // 2
                    if 0 <= ty1 < h and 0 <= ty2 < h:
                        adj[(tx, ty1)].append((tx, ty2))
                        adj[(tx, ty2)].append((tx, ty1))

    # 2. Find Connected Components (Regions)
    visited = set()
    regions = []
    
    for tile in tiles:
        if tile not in visited:
            # BFS
            region = []
            q = [tile]
            visited.add(tile)
            while q:
                curr = q.pop(0)
                region.append(curr)
                for neighbor in adj[curr]:
                    if neighbor not in visited:
                        visited.add(neighbor)
                        q.append(neighbor)
            regions.append(region)
            
    # 3. Validate Regions
    used_dots = []
    
    for region in regions:
        valid_dot = None
        
        # Check against all dots
        for i, dot in enumerate(dots):
            dx, dy = dot['x'], dot['y']
            
            # Check symmetry for this dot
            is_symmetric = True
            for tx, ty in region:
                # Tile center in internal coords
                cx = 2 * tx + 1
                cy = 2 * ty + 1
                
                # Rotated center
                rcx = 2 * dx - cx
                rcy = 2 * dy - cy
                
                # Rotated tile coords
                rtx = (rcx - 1) // 2
                rty = (rcy - 1) // 2
                
                if (rtx, rty) not in region:
                    is_symmetric = False
                    break
            
            if is_symmetric:
                if valid_dot is not None:
                    # This theoretically shouldn't happen for distinct dots
                    return False, "Region symmetric around multiple dots"
                valid_dot = i
        
        if valid_dot is None:
            return False, f"Region of size {len(region)} has no valid symmetry center (dot)"
        
        used_dots.append(valid_dot)
            
    # Check if number of regions equals number of dots
    if len(regions) != len(dots):
        return False, f"Found {len(regions)} regions but {len(dots)} dots"
        
    # Check if all dots are used exactly once
    if len(set(used_dots)) != len(dots):
        return False, "Not all dots are used or some dots used multiple times"
        
    return True, "Success"

def check(output_text):
    if "No solution found" in output_text:
        return False, "No solution found"
        
    data, err = parse_output(output_text)
    if err:
        return False, f"Parse Error: {err}"
        
    return validate_solution(data)

if __name__ == "__main__":
    if len(sys.argv) > 1:
        # Read from file
        with open(sys.argv[1], 'r') as f:
            content = f.read()
    else:
        # Read from stdin
        content = sys.stdin.read()
        
    success, msg = check(content)
    if success:
        print("PASS")
    else:
        print(f"FAIL: {msg}")
        sys.exit(1)
