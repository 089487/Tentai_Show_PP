import sys
import os
from PIL import Image, ImageDraw

def draw_grid(state, w, h, dots, cell_size=30):
    img_w = w * cell_size
    img_h = h * cell_size
    image = Image.new("RGB", (img_w, img_h), "white")
    draw = ImageDraw.Draw(image)

    # Colors for different regions
    region_colors = ['#E6E6FA', '#D8BFD8', '#B0E0E6', '#ADD8E6', '#90EE90', '#F0E68C',
                     '#FFB6C1', '#FFA07A', '#BDB76B', '#DDA0DD', '#87CEFA', '#F5DEB3']

    # Fill tile colors
    for y in range(h):
        for x in range(w):
            dot_index = state[y][x]
            if dot_index != -1:
                color = region_colors[dot_index % len(region_colors)]
                draw.rectangle([x * cell_size, y * cell_size, (x + 1) * cell_size, (y + 1) * cell_size],
                               fill=color)

    # Draw region boundaries
    for y in range(h):
        for x in range(w):
            if x > 0 and state[y][x] != state[y][x - 1]:
                draw.line([x * cell_size, y * cell_size, x * cell_size, (y + 1) * cell_size], fill='black', width=2)
            if y > 0 and state[y][x] != state[y - 1][x]:
                draw.line([x * cell_size, y * cell_size, (x + 1) * cell_size, y * cell_size], fill='black', width=2)

    # Draw dots
    dot_radius = cell_size / 4
    for dot_x, dot_y, is_black in dots:
        center_x = dot_x * cell_size / 2.0
        center_y = dot_y * cell_size / 2.0
        color = "black" if is_black else "white"
        outline = "black"
        draw.ellipse([center_x - dot_radius, center_y - dot_radius, center_x + dot_radius, center_y + dot_radius],
                     fill=color, outline=outline, width=2)

    # Draw outer border
    draw.rectangle([0, 0, img_w - 1, img_h - 1], outline='black', width=3)

    return image

def main():
    if len(sys.argv) != 3:
        print("Usage: python visualizer.py <history_file> <output_frame_dir>")
        sys.exit(1)

    history_file = sys.argv[1]
    output_dir = sys.argv[2]

    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    with open(history_file, 'r') as f:
        w, h = map(int, f.readline().split())
        num_dots = int(f.readline())
        dots = []
        for _ in range(num_dots):
            x, y, is_black = f.readline().split()
            dots.append((int(x), int(y), bool(int(is_black))))
        
        states_data = f.readlines()

    total_frames = len(states_data)
    for i, line in enumerate(states_data):
        grid_flat = list(map(int, line.split()))
        if not grid_flat: continue
        state = [grid_flat[i*w:(i+1)*w] for i in range(h)]
        img = draw_grid(state, w, h, dots)
        
        # Add frame number
        draw = ImageDraw.Draw(img)
        draw.text((5, 5), f"Step: {i}/{total_frames-1}", fill="black")
        
        frame_path = os.path.join(output_dir, f"frame_{i:05d}.png")
        img.save(frame_path)

    if total_frames > 0:
        print(f"Successfully generated {total_frames} frames in {output_dir}")
    else:
        print("No states found in history file to generate frames.")

if __name__ == "__main__":
    main()