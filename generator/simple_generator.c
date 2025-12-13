/*
 * Simplified Tentai Show (Galaxy) Puzzle Generator
 * Extracts only grid generation logic from galaxy.c
 * No UI, drawing, or solving - just generates puzzle data
 *
 * Modified from: https://github.com/franciscod/puzzles/blob/master/galaxies.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

/* ========== BASIC DEFINITIONS ========== */

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define snew(type) ((type *)malloc(sizeof(type)))
#define snewn(n, type) ((type *)malloc((n) * sizeof(type)))
#define sfree(ptr) (free(ptr))
#define sresize(array, number, type) ((type *)realloc(array, (number) * sizeof(type)))

/* ========== RANDOM NUMBER GENERATOR ========== */

typedef struct random_state {
    unsigned long state[4];
} random_state;

static random_state *random_new(void *seed, int len) {
    random_state *rs = snew(random_state);
    int i;
    unsigned long hash = 0;
    
    for (i = 0; i < len; i++) {
        hash = hash * 31 + ((unsigned char *)seed)[i];
    }
    
    rs->state[0] = hash;
    rs->state[1] = hash * 1103515245 + 12345;
    rs->state[2] = rs->state[1] * 1103515245 + 12345;
    rs->state[3] = rs->state[2] * 1103515245 + 12345;
    
    return rs;
}

static void random_free(random_state *rs) {
    sfree(rs);
}

static unsigned long random_bits(random_state *rs, int bits) {
    unsigned long ret;
    unsigned long *state = rs->state;
    
    ret = state[0] ^ (state[0] << 11);
    state[0] = state[1];
    state[1] = state[2];
    state[2] = state[3];
    state[3] = (state[3] ^ (state[3] >> 19)) ^ (ret ^ (ret >> 8));
    
    ret = state[3];
    
    if (bits < 32)
        ret &= (1UL << bits) - 1;
    return ret;
}

static unsigned long random_upto(random_state *rs, unsigned long limit) {
    if (limit == 0) return 0;
    
    unsigned long bits = 0;
    unsigned long max = limit;
    while (max > 0) {
        bits++;
        max >>= 1;
    }
    
    while (1) {
        unsigned long ret = random_bits(rs, bits);
        if (ret < limit)
            return ret;
    }
}

static void shuffle(void *array, int nelts, int eltsize, random_state *rs) {
    char *carray = (char *)array;
    int i;
    char *tmp = snewn(eltsize, char);
    
    for (i = nelts; i-- > 1; ) {
        int j = random_upto(rs, i+1);
        if (j != i) {
            memcpy(tmp, carray + i*eltsize, eltsize);
            memcpy(carray + i*eltsize, carray + j*eltsize, eltsize);
            memcpy(carray + j*eltsize, tmp, eltsize);
        }
    }
    
    sfree(tmp);
}

/* ========== GAME STRUCTURES ========== */

enum { DIFF_NORMAL, DIFF_UNREASONABLE };

typedef struct game_params {
    int w, h;      // Board width and height (user visible)
    int diff;      // Difficulty level
} game_params;

enum { s_tile, s_edge, s_vertex };

#define F_DOT           1
#define F_EDGE_SET      2
#define F_TILE_ASSOC    4
#define F_DOT_BLACK     8
#define F_MARK          16

typedef struct space {
    int x, y;
    int type;
    unsigned int flags;
    int dotx, doty;  // Associated dot location
    int nassoc;      // Number of associated tiles (for dots)
} space;

typedef struct game_state {
    int w, h;        // User-visible size
    int sx, sy;      // Grid size: (2*w-1) x (2*h-1)
    space *grid;
    int ndots;
    space **dots;
} game_state;

/* ========== GRID UTILITIES ========== */

#define INGRID(s,x,y) ((x) >= 0 && (y) >= 0 && (x) < (s)->sx && (y) < (s)->sy)
#define SPACE(s,x,y) ((s)->grid[((y)*(s)->sx)+(x)])
#define IS_VERTICAL_EDGE(x) ((x) % 2 == 0)

/* ========== GAME STATE MANAGEMENT ========== */

static game_state *blank_game(int w, int h) {
    game_state *state = snew(game_state);
    int x, y;
    
    state->w = w;
    state->h = h;
    state->sx = (2*w) + 1;
    state->sy = (2*h) + 1;
    state->grid = snewn(state->sx * state->sy, space);
    state->ndots = 0;
    state->dots = NULL;
    
    for (y = 0; y < state->sy; y++) {
        for (x = 0; x < state->sx; x++) {
            space *sp = &SPACE(state, x, y);
            memset(sp, 0, sizeof(space));
            sp->x = x;
            sp->y = y;
            
            if ((x % 2) && (y % 2))
                sp->type = s_tile;
            else if (!(x % 2) && !(y % 2))
                sp->type = s_vertex;
            else
                sp->type = s_edge;
        }
    }
    
    // Set edges around the border
    for (x = 0; x < state->sx; x++) {
        SPACE(state, x, 0).flags |= F_EDGE_SET;
        SPACE(state, x, state->sy-1).flags |= F_EDGE_SET;
    }
    for (y = 0; y < state->sy; y++) {
        SPACE(state, 0, y).flags |= F_EDGE_SET;
        SPACE(state, state->sx-1, y).flags |= F_EDGE_SET;
    }
    
    return state;
}

static void clear_game(game_state *state) {
    int i;
    
    for (i = 0; i < state->sx * state->sy; i++) {
        space *sp = &state->grid[i];
        if (sp->x == 0 || sp->y == 0 || 
            sp->x == state->sx-1 || sp->y == state->sy-1) {
            sp->flags = F_EDGE_SET;
        } else {
            sp->flags = 0;
        }
        sp->nassoc = 0;
        sp->dotx = -1;
        sp->doty = -1;
    }
    
    if (state->dots) sfree(state->dots);
    state->ndots = 0;
    state->dots = NULL;
}

static void free_game(game_state *state) {
    if (state->dots) sfree(state->dots);
    sfree(state->grid);
    sfree(state);
}

static void game_update_dots(game_state *state) {
    int i, n;
    
    if (state->dots) sfree(state->dots);
    state->ndots = 0;
    
    for (i = 0; i < state->sx * state->sy; i++) {
        if (state->grid[i].flags & F_DOT)
            state->ndots++;
    }
    
    if (state->ndots == 0) {
        state->dots = NULL;
        return;
    }
    
    state->dots = snewn(state->ndots, space *);
    n = 0;
    for (i = 0; i < state->sx * state->sy; i++) {
        if (state->grid[i].flags & F_DOT)
            state->dots[n++] = &state->grid[i];
    }
}

/* ========== DOT AND TILE LOGIC ========== */

static int dot_is_possible(game_state *state, space *sp, int allow_assoc) {
    int bx = 0, by = 0, dx, dy;
    space *adj;
    
    switch (sp->type) {
    case s_tile:
        bx = by = 1; break;
    case s_edge:
        if (IS_VERTICAL_EDGE(sp->x)) {
            bx = 2; by = 1;
        } else {
            bx = 1; by = 2;
        }
        break;
    case s_vertex:
        bx = by = 2; break;
    }

    for (dx = -bx; dx <= bx; dx++) {
        for (dy = -by; dy <= by; dy++) {
            if (!INGRID(state, sp->x+dx, sp->y+dy)) continue;

            adj = &SPACE(state, sp->x+dx, sp->y+dy);

            if (!allow_assoc && (adj->flags & F_TILE_ASSOC))
                return 0;

            if (dx != 0 || dy != 0) {
                /* Other than our own square, no dots nearby. */
                if (adj->flags & (F_DOT))
                    return 0;
            }

            /* We don't want edges within our rectangle
             * (but don't care about edges on the edge) */
            if (abs(dx) < bx && abs(dy) < by &&
                adj->flags & F_EDGE_SET)
                return 0;
        }
    }
    return 1;
}

static void add_dot(space *sp) {
    sp->flags |= F_DOT;
    sp->nassoc = 0;
}

static space *space_opposite_dot(const game_state *state, const space *sp, const space *dot) {
    int dx, dy, x, y;
    
    dx = dot->x - sp->x;
    dy = dot->y - sp->y;
    
    x = dot->x + dx;
    y = dot->y + dy;
    
    if (!INGRID(state, x, y)) return NULL;
    
    return &SPACE(state, x, y);
}

static int outline_tile_fordot(game_state *state, space *tile, int mark);

static int solver_obvious_dot(game_state *state, space *dot) {
    int x, y, ret = 0;
    
    assert(dot->flags & F_DOT);
    
    for (x = 1; x < state->sx; x += 2) {
        for (y = 1; y < state->sy; y += 2) {
            space *tile = &SPACE(state, x, y);
            space *tile_opp;
            
            if (tile->flags & F_TILE_ASSOC) continue;
            
            tile_opp = space_opposite_dot(state, tile, dot);
            if (!tile_opp) continue;
            
            if (tile_opp->flags & F_TILE_ASSOC &&
                (tile_opp->dotx != dot->x || tile_opp->doty != dot->y))
                continue;
            
            // Associate this tile with dot
            tile->flags |= F_TILE_ASSOC;
            tile->dotx = dot->x;
            tile->doty = dot->y;
            dot->nassoc++;
            
            if (tile_opp->flags & F_TILE_ASSOC) {
                assert(tile_opp->dotx == dot->x && tile_opp->doty == dot->y);
            } else {
                tile_opp->flags |= F_TILE_ASSOC;
                tile_opp->dotx = dot->x;
                tile_opp->doty = dot->y;
                dot->nassoc++;
            }
            
            ret = 1;
        }
    }
    
    return ret;
}

static int outline_tile_fordot(game_state *state, space *tile, int mark) {
    int dxs[4] = {-1, 1, 0, 0}, dys[4] = {0, 0, -1, 1};
    int i, didsth = 0;
    
    for (i = 0; i < 4; i++) {
        int ex = tile->x + dxs[i];
        int ey = tile->y + dys[i];
        int tx = ex + dxs[i];
        int ty = ey + dys[i];
        
        if (!INGRID(state, ex, ey)) continue;
        
        space *edge = &SPACE(state, ex, ey);
        int has_edge = (edge->flags & F_EDGE_SET) ? 1 : 0;
        int same = 0;
        
        if (INGRID(state, tx, ty)) {
            space *tadj = &SPACE(state, tx, ty);
            if (!(tile->flags & F_TILE_ASSOC))
                same = (tadj->flags & F_TILE_ASSOC) ? 0 : 1;
            else
                same = ((tadj->flags & F_TILE_ASSOC) &&
                       tile->dotx == tadj->dotx &&
                       tile->doty == tadj->doty) ? 1 : 0;
        }
        
        if (!has_edge && !same) {
            if (mark) edge->flags |= F_EDGE_SET;
            didsth = 1;
        } else if (has_edge && same) {
            if (mark) edge->flags &= ~F_EDGE_SET;
            didsth = 1;
        }
    }
    
    return didsth;
}

/* ========== GENERATOR ========== */

static int dot_expand_or_move(game_state *state, space *dot, space **toadd, int nadd) {
    int i, ret;
    
    assert(dot->flags & F_DOT);
    
    for (i = 0; i < nadd; i++) {
        space *tile_opp = space_opposite_dot(state, toadd[i], dot);
        
        if (!tile_opp) return 0;
        if (tile_opp->flags & F_TILE_ASSOC &&
            (tile_opp->dotx != dot->x || tile_opp->doty != dot->y))
            return 0;
    }
    
    for (i = 0; i < nadd; i++) {
        space *tile = toadd[i];
        space *tile_opp = space_opposite_dot(state, tile, dot);
        
        tile->flags |= F_TILE_ASSOC;
        tile->dotx = dot->x;
        tile->doty = dot->y;
        dot->nassoc++;
        
        if (!(tile_opp->flags & F_TILE_ASSOC)) {
            tile_opp->flags |= F_TILE_ASSOC;
            tile_opp->dotx = dot->x;
            tile_opp->doty = dot->y;
            dot->nassoc++;
        }
    }
    
    ret = solver_obvious_dot(state, dot);
    assert(ret != -1);
    
    return 1;
}

#define MAX_TOADD 20
#define MAX_OUTSIDE 100

static int generate_try_block(game_state *state, random_state *rs,
                              int x1, int y1, int x2, int y2) {
    space *toadd[MAX_TOADD], *outside[MAX_OUTSIDE];
    int nadd = 0, nout = 0, i, maxsz;
    
    if (x1 < 0 || y1 < 0 || x2 >= state->sx || y2 >= state->sy) return 0;
    
    maxsz = (state->w * state->h) / state->ndots;
    if (maxsz < 4) maxsz = 4;
    
    for (int y = y1; y <= y2; y += 2) {
        for (int x = x1; x <= x2; x += 2) {
            space *sp = &SPACE(state, x, y);
            assert(sp->type == s_tile);
            if (sp->flags & F_TILE_ASSOC) return 0;
            if (nadd >= MAX_TOADD) return 0;
            toadd[nadd++] = sp;
        }
    }
    
    // Collect outside tiles
    for (int x = x1; x <= x2; x += 2) {
        if (y1 >= 2 && nout < MAX_OUTSIDE) {
            outside[nout++] = &SPACE(state, x, y1-2);
        }
        if (y2 <= state->sy-3 && nout < MAX_OUTSIDE) {
            outside[nout++] = &SPACE(state, x, y2+2);
        }
    }
    for (int y = y1; y <= y2; y += 2) {
        if (x1 >= 2 && nout < MAX_OUTSIDE) {
            outside[nout++] = &SPACE(state, x1-2, y);
        }
        if (x2 <= state->sx-3 && nout < MAX_OUTSIDE) {
            outside[nout++] = &SPACE(state, x2+2, y);
        }
    }
    
    shuffle(outside, nout, sizeof(space *), rs);
    
    for (i = 0; i < nout; i++) {
        if (!(outside[i]->flags & F_TILE_ASSOC)) continue;
        space *dot = &SPACE(state, outside[i]->dotx, outside[i]->doty);
        if (dot->nassoc >= maxsz) continue;
        if (dot_expand_or_move(state, dot, toadd, nadd)) return 1;
    }
    
    return 0;
}

#define GP_DOTS 1

static void generate_pass(game_state *state, random_state *rs, int *scratch,
                         int perc, unsigned int flags) {
    int sz = state->sx * state->sy;
    int nspc = (perc * sz) / 100;
    int i;
    
    shuffle(scratch, sz, sizeof(int), rs);
    
    for (i = 0; i < nspc; i++) {
        space *sp = &state->grid[scratch[i]];
        int x1 = sp->x, y1 = sp->y, x2 = sp->x, y2 = sp->y;
        
        if (sp->type == s_edge) {
            if (IS_VERTICAL_EDGE(sp->x)) {
                x1--; x2++;
            } else {
                y1--; y2++;
            }
        }
        
        if (sp->type != s_vertex) {
            if (generate_try_block(state, rs, x1, y1, x2, y2))
                continue;
        }
        
        if (!(flags & GP_DOTS)) continue;
        if ((sp->type == s_edge) && (i % 2)) continue;
        
        if (dot_is_possible(state, sp, 0)) {
            add_dot(sp);
            solver_obvious_dot(state, sp);
        }
    }
}

/* ========== ENCODING ========== */

static char *encode_game(game_state *state) {
    char *desc, *p;
    int run, i, x, y;
    space *sp;
    
    desc = snewn(state->sx * state->sy + 100, char);
    p = desc;
    run = 0;
    
    for (y = 0; y < state->sy; y++) {
        for (x = 0; x < state->sx; x++) {
            sp = &SPACE(state, x, y);
            
            if (sp->flags & F_DOT) {
                if (run) {
                    while (run > 26) {
                        *p++ = 'z';
                        run -= 26;
                    }
                    *p++ = 'a' + run - 1;
                    run = 0;
                }
                
                if (sp->flags & F_DOT_BLACK)
                    *p++ = 'B';
                else
                    *p++ = 'M';
            } else {
                run++;
            }
        }
    }
    
    if (run) {
        while (run > 26) {
            *p++ = 'z';
            run -= 26;
        }
        *p++ = 'a' + run - 1;
    }
    
    *p = '\0';
    
    return desc;
}

static game_state *generate_new_game(const game_params *params, random_state *rs) {
    game_state *state = blank_game(params->w, params->h);
    int *scratch, sz = state->sx * state->sy;
    
    scratch = snewn(sz, int);
    for (int i = 0; i < sz; i++) scratch[i] = i;
    
    clear_game(state);
    generate_pass(state, rs, scratch, 100, GP_DOTS);
    game_update_dots(state);
    
    // Outline all tiles
    for (int i = 0; i < state->sx * state->sy; i++) {
        if (state->grid[i].type == s_tile)
            outline_tile_fordot(state, &state->grid[i], TRUE);
    }
    
    sfree(scratch);
    return state;
}

/* ========== MAIN ========== */

static void print_grid(game_state *state) {
    printf("\nGrid %dx%d (internal %dx%d):\n", state->w, state->h, state->sx, state->sy);
    printf("Number of dots: %d\n\n", state->ndots);
    
    for (int y = 0; y < state->sy; y++) {
        for (int x = 0; x < state->sx; x++) {
            space *sp = &SPACE(state, x, y);
            
            if (sp->flags & F_DOT) {
                if (sp->flags & F_DOT_BLACK)
                    printf("●");
                else
                    printf("○");
            } else if (sp->type == s_tile) {
                if (sp->flags & F_TILE_ASSOC)
                    printf(" ");
                else
                    printf(" ");
            } else if (sp->type == s_edge) {
                if (sp->flags & F_EDGE_SET) {
                    if (IS_VERTICAL_EDGE(x))
                        printf("|");
                    else
                        printf("-");
                } else {
                    printf(" ");
                }
            } else {
                printf("+");
            }
        }
        printf("\n");
    }
}

static void print_usage(const char *progname) {
    printf("Usage: %s [options]\n", progname);
    printf("Options:\n");
    printf("  --size=WxH       Set puzzle size (default: 7x7)\n");
    printf("  --seed=N         Set random seed\n");
    printf("  --count=N        Generate N puzzles (default: 1)\n");
    printf("  --help           Show this help\n");
}

int main(int argc, char **argv) {
    game_params params;
    random_state *rs;
    char *desc;
    time_t seed = time(NULL);
    int num_puzzles = 1;
    
    // Default parameters
    params.w = 7;
    params.h = 7;
    params.diff = DIFF_NORMAL;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--seed=", 7) == 0) {
            seed = atoi(argv[i] + 7);
        } else if (strncmp(argv[i], "--count=", 8) == 0) {
            num_puzzles = atoi(argv[i] + 8);
        } else if (strncmp(argv[i], "--size=", 7) == 0) {
            if (sscanf(argv[i] + 7, "%dx%d", &params.w, &params.h) != 2) {
                fprintf(stderr, "Invalid size format. Use --size=WxH\n");
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    rs = random_new(&seed, sizeof(seed));
    
    for (int i = 0; i < num_puzzles; i++) {
        game_state *state = generate_new_game(&params, rs);
        char *desc = encode_game(state);
        
        printf("Puzzle %d:\n", i + 1);
        printf("Game ID: %dx%d:%s\n", params.w, params.h, desc);
        
        print_grid(state);
        
        sfree(desc);
        free_game(state);
        
        if (i < num_puzzles - 1) printf("\n---\n\n");
    }
    
    random_free(rs);
    
    return 0;
}
