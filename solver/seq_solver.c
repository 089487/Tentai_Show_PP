#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define MAX_W 20
#define MAX_H 20
#define MAX_DOTS 100
#define HASH_TABLE_SIZE 1000003

// Structure to represent a dot
typedef struct {
    int x, y; // Internal coordinates
    int is_black;
} Dot;

// Structure to represent the puzzle
typedef struct {
    int w, h;       // Puzzle dimensions (e.g., 4x4)
    int sx, sy;     // Internal grid dimensions (2*w+1, 2*h+1)
    int num_dots;
    Dot dots[MAX_DOTS];
} Puzzle;

// State for BFS
typedef struct State {
    int8_t grid[MAX_H][MAX_W]; // Stores dot_index for each tile, -1 if empty
    int filled_count;
    uint64_t hash;
    struct State *next; // For queue
} State;

// Zobrist Table
uint64_t zobrist_table[MAX_H * MAX_W][MAX_DOTS];

// Hash Table for Visited States
typedef struct HashEntry {
    uint64_t key;
    struct HashEntry *next;
} HashEntry;

HashEntry *visited[HASH_TABLE_SIZE];

// Queue
State *queue_head = NULL;
State *queue_tail = NULL;

// Function Prototypes
void init_zobrist();
Puzzle* parse_game_id(const char *game_id);
void solve(Puzzle *p);
void print_solution(Puzzle *p, State *s);

// --- Zobrist & Hashing ---

void init_zobrist() {
    srand(time(NULL));
    for (int i = 0; i < MAX_H * MAX_W; i++) {
        for (int j = 0; j < MAX_DOTS; j++) {
            uint64_t r1 = rand();
            uint64_t r2 = rand();
            zobrist_table[i][j] = (r1 << 32) | r2;
        }
    }
}

bool is_visited(uint64_t hash) {
    int idx = hash % HASH_TABLE_SIZE;
    HashEntry *entry = visited[idx];
    while (entry) {
        if (entry->key == hash) return true;
        entry = entry->next;
    }
    return false;
}

void add_visited(uint64_t hash) {
    int idx = hash % HASH_TABLE_SIZE;
    HashEntry *entry = (HashEntry*)malloc(sizeof(HashEntry));
    entry->key = hash;
    entry->next = visited[idx];
    visited[idx] = entry;
}

// --- Queue ---

void enqueue(State *s) {
    s->next = NULL;
    if (queue_tail) {
        queue_tail->next = s;
        queue_tail = s;
    } else {
        queue_head = queue_tail = s;
    }
}

State* dequeue() {
    if (!queue_head) return NULL;
    State *s = queue_head;
    queue_head = queue_head->next;
    if (!queue_head) queue_tail = NULL;
    return s;
}

// --- Puzzle Logic ---

Puzzle* parse_game_id(const char *game_id) {
    int w, h;
    char *colon = strchr(game_id, ':');
    if (!colon) return NULL;

    if (sscanf(game_id, "%dx%d", &w, &h) != 2) return NULL;

    Puzzle *p = (Puzzle*)malloc(sizeof(Puzzle));
    p->w = w;
    p->h = h;
    p->sx = 2 * w + 1;
    p->sy = 2 * h + 1;
    p->num_dots = 0;

    const char *data = colon + 1;
    int pos = 0;
    int len = strlen(data);
    
    for (int i = 0; i < len; i++) {
        char c = data[i];
        if (c == 'M') {
            p->dots[p->num_dots].x = pos % p->sx;
            p->dots[p->num_dots].y = pos / p->sx;
            p->dots[p->num_dots].is_black = 0;
            p->num_dots++;
            pos++;
        } else if (c == 'B') {
            p->dots[p->num_dots].x = pos % p->sx;
            p->dots[p->num_dots].y = pos / p->sx;
            p->dots[p->num_dots].is_black = 1;
            p->num_dots++;
            pos++;
        } else if (c >= 'a' && c <= 'z') {
            pos += c - 'a' + 1;
        }
    }
    return p;
}

// Check if a tile is valid and unassigned
bool is_valid_tile(State *s, int w, int h, int tx, int ty) {
    if (tx < 0 || tx >= w || ty < 0 || ty >= h) return false;
    return s->grid[ty][tx] == -1;
}

// Get symmetric tile coordinates
void get_symmetric_tile(int dot_x, int dot_y, int tx, int ty, int *out_tx, int *out_ty) {
    // Internal center of tile
    int cx = tx * 2 + 1;
    int cy = ty * 2 + 1;
    
    // Rotated center
    int rx = 2 * dot_x - cx;
    int ry = 2 * dot_y - cy;
    
    // Back to tile coords
    *out_tx = (rx - 1) / 2;
    *out_ty = (ry - 1) / 2;
}

// Check if tile touches the dot (for initial expansion)
bool touches_dot(int dot_x, int dot_y, int tx, int ty) {
    int cx = tx * 2 + 1;
    int cy = ty * 2 + 1;
    return abs(cx - dot_x) <= 1 && abs(cy - dot_y) <= 1;
}

void solve(Puzzle *p) {
    init_zobrist();
    
    State *initial = (State*)malloc(sizeof(State));
    memset(initial->grid, -1, sizeof(initial->grid));
    initial->filled_count = 0;
    initial->hash = 0;
    
    // Pre-fill tiles that have dots in their center
    for (int i = 0; i < p->num_dots; i++) {
        int dx = p->dots[i].x;
        int dy = p->dots[i].y;
        
        // If dot is on a tile center (both coords odd)
        if (dx % 2 != 0 && dy % 2 != 0) {
            int tx = (dx - 1) / 2;
            int ty = (dy - 1) / 2;
            
            if (initial->grid[ty][tx] == -1) {
                initial->grid[ty][tx] = i;
                initial->filled_count++;
                initial->hash ^= zobrist_table[ty * p->w + tx][i];
            } else {
                // Collision (should not happen in valid puzzle)
                printf("Error: Overlapping dots?\n");
                return;
            }
        }
    }
    
    enqueue(initial);
    add_visited(initial->hash);
    
    int iterations = 0;
    
    while (queue_head) {
        State *s = dequeue();
        iterations++;
        
        if (s->filled_count == p->w * p->h) {
            // Verify all dots are used
            bool all_dots_used = true;
            bool dot_used[MAX_DOTS] = {false};
            for(int y=0; y<p->h; y++) {
                for(int x=0; x<p->w; x++) {
                    if(s->grid[y][x] >= 0) dot_used[s->grid[y][x]] = true;
                }
            }
            for(int i=0; i<p->num_dots; i++) {
                if(!dot_used[i]) { all_dots_used = false; break; }
            }
            
            if(all_dots_used) {
                print_solution(p, s);
                return;
            }
            continue;
        }
        
        // Try to expand each dot
        for (int d = 0; d < p->num_dots; d++) {
            // Find frontier for dot d
            // Optimization: Instead of scanning full grid, we could maintain frontier.
            // But for small grids, scanning is okay.
            
            // We need to find a pair of tiles (T, T') such that:
            // 1. T is unassigned.
            // 2. T is adjacent to an existing tile of d OR (if d has no tiles) T touches d.
            // 3. T' is symmetric to T wrt d.
            // 4. T' is unassigned (or T == T').
            
            // To avoid duplicates (adding T then neighbor vs neighbor then T), 
            // we can iterate over all currently assigned tiles of d and check their neighbors.
            
            int dx = p->dots[d].x;
            int dy = p->dots[d].y;
            
            bool has_tiles = false;
            
            // Check neighbors of existing tiles
            for (int y = 0; y < p->h; y++) {
                for (int x = 0; x < p->w; x++) {
                    if (s->grid[y][x] == d) {
                        has_tiles = true;
                        int neighbors[4][2] = {{x+1, y}, {x-1, y}, {x, y+1}, {x, y-1}};
                        for (int k = 0; k < 4; k++) {
                            int nx = neighbors[k][0];
                            int ny = neighbors[k][1];
                            
                            if (is_valid_tile(s, p->w, p->h, nx, ny)) {
                                // Check symmetry
                                int sym_x, sym_y;
                                get_symmetric_tile(dx, dy, nx, ny, &sym_x, &sym_y);
                                
                                if (is_valid_tile(s, p->w, p->h, sym_x, sym_y) || (nx == sym_x && ny == sym_y)) {
                                    // Valid expansion
                                    // Create new state
                                    State *next = (State*)malloc(sizeof(State));
                                    *next = *s; // Copy
                                    
                                    next->grid[ny][nx] = d;
                                    next->hash ^= zobrist_table[ny * p->w + nx][d];
                                    next->filled_count++;
                                    
                                    if (nx != sym_x || ny != sym_y) {
                                        if (next->grid[sym_y][sym_x] == -1) {
                                            next->grid[sym_y][sym_x] = d;
                                            next->hash ^= zobrist_table[sym_y * p->w + sym_x][d];
                                            next->filled_count++;
                                        } else {
                                            // Should not happen if is_valid_tile check passed
                                            free(next);
                                            continue;
                                        }
                                    }
                                    
                                    if (!is_visited(next->hash)) {
                                        add_visited(next->hash);
                                        enqueue(next);
                                    } else {
                                        free(next);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // If dot has no tiles yet, check tiles touching the dot
            if (!has_tiles) {
                // Check all tiles. If touches_dot, try adding.
                // Optimization: only check tiles around (dx/2, dy/2)
                int ctx = (dx - 1) / 2;
                int cty = (dy - 1) / 2;
                
                for (int ty = cty - 1; ty <= cty + 1; ty++) {
                    for (int tx = ctx - 1; tx <= ctx + 1; tx++) {
                        if (is_valid_tile(s, p->w, p->h, tx, ty) && touches_dot(dx, dy, tx, ty)) {
                             // Check symmetry
                            int sym_x, sym_y;
                            get_symmetric_tile(dx, dy, tx, ty, &sym_x, &sym_y);
                            
                            if (is_valid_tile(s, p->w, p->h, sym_x, sym_y) || (tx == sym_x && ty == sym_y)) {
                                State *next = (State*)malloc(sizeof(State));
                                *next = *s;
                                
                                next->grid[ty][tx] = d;
                                next->hash ^= zobrist_table[ty * p->w + tx][d];
                                next->filled_count++;
                                
                                if (tx != sym_x || ty != sym_y) {
                                    if (next->grid[sym_y][sym_x] == -1) {
                                        next->grid[sym_y][sym_x] = d;
                                        next->hash ^= zobrist_table[sym_y * p->w + sym_x][d];
                                        next->filled_count++;
                                    } else {
                                        free(next);
                                        continue;
                                    }
                                }
                                
                                if (!is_visited(next->hash)) {
                                    add_visited(next->hash);
                                    enqueue(next);
                                } else {
                                    free(next);
                                }
                            }
                        }
                    }
                }
            } else {
                // If dot has tiles, we MUST expand from existing tiles.
                // However, we might have missed the case where a dot has tiles but can still expand 
                // to a tile that is adjacent to the DOT itself but not any existing tile.
                // This happens when the dot is on an edge/vertex and we added some tiles, 
                // but there are other valid starting tiles touching the dot.
                
                // Check tiles touching the dot again
                int ctx = (dx - 1) / 2;
                int cty = (dy - 1) / 2;
                
                for (int ty = cty - 1; ty <= cty + 1; ty++) {
                    for (int tx = ctx - 1; tx <= ctx + 1; tx++) {
                        // Only consider tiles touching the dot that are unassigned
                        if (is_valid_tile(s, p->w, p->h, tx, ty) && touches_dot(dx, dy, tx, ty)) {
                             // Check symmetry
                            int sym_x, sym_y;
                            get_symmetric_tile(dx, dy, tx, ty, &sym_x, &sym_y);
                            
                            if (is_valid_tile(s, p->w, p->h, sym_x, sym_y) || (tx == sym_x && ty == sym_y)) {
                                State *next = (State*)malloc(sizeof(State));
                                *next = *s;
                                
                                next->grid[ty][tx] = d;
                                next->hash ^= zobrist_table[ty * p->w + tx][d];
                                next->filled_count++;
                                
                                if (tx != sym_x || ty != sym_y) {
                                    if (next->grid[sym_y][sym_x] == -1) {
                                        next->grid[sym_y][sym_x] = d;
                                        next->hash ^= zobrist_table[sym_y * p->w + sym_x][d];
                                        next->filled_count++;
                                    } else {
                                        free(next);
                                        continue;
                                    }
                                }
                                
                                if (!is_visited(next->hash)) {
                                    add_visited(next->hash);
                                    enqueue(next);
                                } else {
                                    free(next);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    printf("No solution found.\n");
}

void print_solution(Puzzle *p, State *s) {
    printf("\nPuzzle Grid (%dx%d):\n", p->w, p->h);
    for (int i = 0; i < p->sx + 2; i++) printf("=");
    printf("\n");

    for (int y = 0; y < p->sy; y++) {
        for (int x = 0; x < p->sx; x++) {
            // Check for dot
            int dot_idx = -1;
            for (int i = 0; i < p->num_dots; i++) {
                if (p->dots[i].x == x && p->dots[i].y == y) {
                    dot_idx = i;
                    break;
                }
            }

            if (dot_idx != -1) {
                if (p->dots[dot_idx].is_black) printf("●");
                else printf("○");
            } else {
                // Determine character based on grid/tile assignment
                if (x % 2 == 0 && y % 2 == 0) {
                    printf("+");
                } else if (x % 2 == 0) {
                    // Vertical edge
                    // Check tiles left and right
                    int tx_left = (x - 2) / 2;
                    int tx_right = x / 2;
                    int ty = (y - 1) / 2;
                    
                    int id_left = (tx_left >= 0) ? s->grid[ty][tx_left] : -2;
                    int id_right = (tx_right < p->w) ? s->grid[ty][tx_right] : -2;
                    
                    if (id_left != id_right) printf("|");
                    else printf(" ");
                } else if (y % 2 == 0) {
                    // Horizontal edge
                    int ty_up = (y - 2) / 2;
                    int ty_down = y / 2;
                    int tx = (x - 1) / 2;
                    
                    int id_up = (ty_up >= 0) ? s->grid[ty_up][tx] : -2;
                    int id_down = (ty_down < p->h) ? s->grid[ty_down][tx] : -2;
                    
                    if (id_up != id_down) printf("-");
                    else printf(" ");
                } else {
                    printf(" ");
                }
            }
        }
        printf("\n");
    }
    for (int i = 0; i < p->sx + 2; i++) printf("=");
    printf("\n");
    printf("Total dots: %d\n", p->num_dots);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "r");
    if (!f) {
        perror("Error opening file");
        return 1;
    }

    char line[4096];
    if (!fgets(line, sizeof(line), f)) {
        fprintf(stderr, "Error reading file\n");
        fclose(f);
        return 1;
    }
    fclose(f);

    line[strcspn(line, "\n")] = 0;

    char *prefix = "Game ID: ";
    char *start = strstr(line, prefix);
    
    Puzzle *p = NULL;
    if (start) {
        p = parse_game_id(start + strlen(prefix));
    } else if (strchr(line, ':')) {
        p = parse_game_id(line);
    }

    if (p) {
        solve(p);
        free(p);
    } else {
        fprintf(stderr, "Failed to parse puzzle.\n");
        return 1;
    }

    return 0;
}
