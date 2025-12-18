/**
 * @file seq_solver.cpp
 * @brief Sequential solver for the Tentai Show (Symmetry) puzzle.
 *
 * Parses a Game ID string from an input file, constructs the puzzle grid
 * and dot layout, then performs a breadth-first search (BFS) that enforces
 * mirror symmetry around each dot to fill the board with regions. Zobrist
 * hashing is used to deduplicate visited states efficiently.
 *
 * Input format:
 *  - First line is either "Game ID: <id>" or just "<id>".
 *  - <id> is of the form "WxH:<data>", where <data> encodes cells on the
 *    internal (2W+1)x(2H+1) grid using:
 *      - 'M' for a white (unfilled) dot
 *      - 'B' for a black dot
 *      - 'a'..'z' as run-length skips (a=1, b=2, ..., z=26)
 *
 * Output:
 *  - On success, prints an ASCII visualization of the solved puzzle showing
 *    dots (● black, ○ white) and region borders using '-', '|', and '+'.
 *  - Returns exit code 0 on success.
 *  - On parse error or if no solution is found, reports an error and returns 1.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <queue>
#include <random>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

class Dot {
   public:
    /**
     * @class Dot
     * @brief A clue dot on the internal (2W+1)x(2H+1) grid.
     *
     * Holds integer coordinates and a color flag. Coordinates are expressed
     * in the internal grid space (not tile indices). The color is used for
     * visualization when printing solutions in this solver.
     */
    Dot() = default;
    Dot(int x_, int y_, bool black) : x_(x_), y_(y_), is_black_(black) {}

    int x() const noexcept { return x_; }
    int y() const noexcept { return y_; }
    bool is_black() const noexcept { return is_black_; }

   private:
    int x_{0};
    int y_{0};
    bool is_black_{false};
};

class Puzzle {
   public:
    /**
     * @class Puzzle
     * @brief Parsed puzzle specification and geometry.
     *
     * Stores the board size in tiles (W x H), the derived internal grid size
     * (sx = 2W + 1, sy = 2H + 1), and the list of clue dots positioned on the
     * internal grid. Provides a factory to parse the canonical Game ID string.
     *
     * Invariants:
     *  - 0 <= dot.x < sx and 0 <= dot.y < sy for every dot
     *  - Internal coordinates with both x and y odd correspond to tile centers
     */
    Puzzle() = default;

    /**
     * @brief Parse a puzzle from a canonical Game ID string.
     * @param game_id String like "WxH:<data>" (optionally already prefixed by
     *        "Game ID: "). Width and height are parsed from the prefix and
     *        <data> is decoded over the internal (2W+1)x(2H+1) grid using:
     *        'M' for white dot, 'B' for black dot, and 'a'..'z' as run-length
     *        skips (a=1, b=2, ..., z=26).
     * @return Unique pointer to a constructed `Puzzle` on success, or nullptr
     *         if parsing fails.
     */
    static std::unique_ptr<Puzzle> fromGameId(const std::string& game_id) {
        const auto colon = game_id.find(':');
        if (colon == std::string::npos) return nullptr;

        const auto xpos = game_id.find('x');
        if (xpos == std::string::npos || xpos > colon) return nullptr;

        int w = 0, h = 0;
        try {
            w = std::stoi(game_id.substr(0, xpos));
            h = std::stoi(game_id.substr(xpos + 1, colon - (xpos + 1)));
        } catch (...) {
            return nullptr;
        }

        auto p = std::make_unique<Puzzle>();
        p->w_ = w;
        p->h_ = h;
        p->sx_ = 2 * w + 1;
        p->sy_ = 2 * h + 1;

        const std::string data = game_id.substr(colon + 1);
        int pos = 0;
        for (char c : data) {
            if (c == 'M') {
                p->dots_.emplace_back(pos % p->sx_, pos / p->sx_, false);
                ++pos;
            } else if (c == 'B') {
                p->dots_.emplace_back(pos % p->sx_, pos / p->sx_, true);
                ++pos;
            } else if (c >= 'a' && c <= 'z') {
                pos += (c - 'a' + 1);
            }
        }
        return p;
    }

    int width() const noexcept { return w_; }
    int height() const noexcept { return h_; }
    int internalWidth() const noexcept { return sx_; }
    int internalHeight() const noexcept { return sy_; }
    std::size_t dotCount() const noexcept { return dots_.size(); }
    const std::vector<Dot>& dots() const noexcept { return dots_; }

    int dotIndexAt(int x, int y) const noexcept {
        for (int i = 0; i < static_cast<int>(dots_.size()); ++i) {
            if (dots_[i].x() == x && dots_[i].y() == y) return i;
        }
        return -1;
    }

   private:
    int w_{0}, h_{0};
    int sx_{0}, sy_{0};
    std::vector<Dot> dots_{};
};

class State {
   public:
    /**
     * @class State
     * @brief Candidate assignment of tiles to dot indices.
     *
     * The grid has dimensions W x H (tiles). Each entry is the owning dot
     * index (>= 0) or -1 for unfilled. `filledCount` tracks the number of
     * filled tiles for quick goal checks. `hash` stores the Zobrist hash used
     * to deduplicate states during search.
     */
    State() = default;
    State(int w, int h) : grid_(h, std::vector<int16_t>(w, -1)) {}

    int16_t& at(int x, int y) { return grid_[y][x]; }
    int16_t at(int x, int y) const { return grid_[y][x]; }
    const std::vector<std::vector<int16_t>>& grid() const noexcept {
        return grid_;
    }

    int filledCount{0};
    std::uint64_t hash{0};

   private:
    std::vector<std::vector<int16_t>> grid_{};  // [h][w]
};

class Solver {
   public:
    /**
     * @class Solver
     * @brief Sequential BFS-based solver with dot-centered symmetry.
     *
     * Expands regions from dots while enforcing mirror symmetry around the
     * dot center in the internal grid. Uses Zobrist hashing and a visited-set
     * to avoid revisiting states. Produces a solved `State` or no solution.
     */
    explicit Solver(const Puzzle& puz) : p_(puz) { initZobrist(); }

    /**
     * @brief Solve the puzzle using BFS with symmetry constraints.
     * @return A solved `State` if found; `std::nullopt` otherwise.
     * @details Seeds tiles under odd-odd internal coordinates that contain
     *          dots, then expands tiles by assigning them to dot indices in a
     *          way that maintains mirror symmetry around the dot center.
     *          Uses Zobrist hashing plus a visited set to avoid revisiting
     *          equivalent states.
     */
    std::optional<State> solve() {
        State initial(p_.width(), p_.height());
        initial.filledCount = 0;
        initial.hash = 0;

        // Prefill forced tiles from all dots: center, edge, and corner cases.
        if (!seedForcedTiles(initial)) {
            return std::nullopt;
        }

        std::queue<State> q;
        q.push(initial);
        visited_.insert(initial.hash);

        while (!q.empty()) {
            State s = std::move(q.front());
            q.pop();

            if (s.filledCount == p_.width() * p_.height()) {
                if (allDotsUsed(s)) return s;
                continue;
            }

            //for (int d = 0; d < static_cast<int>(p_.dotCount()); ++d) {
                //const int dx = p_.dots()[d].x();
                //const int dy = p_.dots()[d].y();

                for (int y = 0; y < p_.height(); ++y) {
                    for (int x = 0; x < p_.width(); ++x) {
                        if (s.at(x, y) !=-1 ) {
                            int d = s.at(x, y);
                            const int dx = p_.dots()[d].x();
                            const int dy = p_.dots()[d].y();
                            static constexpr int dirs[4][2] = {
                                {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                            for (auto& dir : dirs) {
                                const int nx = x + dir[0];
                                const int ny = y + dir[1];
                                if (!isValidTile(s, nx, ny)) continue;

                                const auto [sx, sy] =
                                    getSymmetricTile(dx, dy, nx, ny);
                                if (isValidTile(s, sx, sy)
                                    || (nx == sx && ny == sy)) {
                                    State next = s;
                                    next.at(nx, ny) = static_cast<int16_t>(d);
                                    next.hash ^=
                                        zobrist_[ny * p_.width() + nx][d];
                                    ++next.filledCount;

                                    if (nx != sx || ny != sy) {
                                        if (next.at(sx, sy) == -1) {
                                            next.at(sx, sy) =
                                                static_cast<int16_t>(d);
                                            next.hash ^=
                                                zobrist_[sy * p_.width() + sx]
                                                        [d];
                                            ++next.filledCount;
                                        } else {
                                            continue;
                                        }
                                    }

                                    if (visited_.insert(next.hash).second) {
                                        q.push(std::move(next));
                                    }
                                }
                            }
                        }
                    }
                }

                // Removed check loop: initial forced tiles are prefilled.
            //}
        }
        return std::nullopt;
    }

    /**
     * @brief Print an ASCII visualization of a solved state.
     * @param s Solved `State` to render.
     * @details Renders the internal (2W+1)x(2H+1) grid with dots (● for black,
     *          ○ for white), '+' at grid intersections, '-' and '|' for region
     *          borders, and spaces for interiors. Also prints basic metadata.
     */
    void printSolution(const State& s) const {
        std::cout << "\nPuzzle Grid (" << p_.width() << "x" << p_.height()
                  << "):\n";
        for (int i = 0; i < p_.internalWidth() + 2; ++i) std::cout << '=';
        std::cout << '\n';

        for (int y = 0; y < p_.internalHeight(); ++y) {
            for (int x = 0; x < p_.internalWidth(); ++x) {
                const int dot_idx = p_.dotIndexAt(x, y);
                if (dot_idx != -1) {
                    std::cout << (p_.dots()[dot_idx].is_black() ? "●" : "○");
                } else {
                    if (x % 2 == 0 && y % 2 == 0) {
                        std::cout << '+';
                    } else if (x % 2 == 0) {
                        const int tx_left = (x - 2) / 2;
                        const int tx_right = x / 2;
                        const int ty = (y - 1) / 2;
                        const int id_left =
                            (tx_left >= 0) ? s.at(tx_left, ty) : -2;
                        const int id_right =
                            (tx_right < p_.width()) ? s.at(tx_right, ty) : -2;
                        std::cout << (id_left != id_right ? '|' : ' ');
                    } else if (y % 2 == 0) {
                        const int ty_up = (y - 2) / 2;
                        const int ty_down = y / 2;
                        const int tx = (x - 1) / 2;
                        const int id_up = (ty_up >= 0) ? s.at(tx, ty_up) : -2;
                        const int id_down =
                            (ty_down < p_.height()) ? s.at(tx, ty_down) : -2;
                        std::cout << (id_up != id_down ? '-' : ' ');
                    } else {
                        std::cout << ' ';
                    }
                }
            }
            std::cout << '\n';
        }
        for (int i = 0; i < p_.internalWidth() + 2; ++i) std::cout << '=';
        std::cout << "\nTotal dots: " << p_.dotCount() << "\n";
    }

   private:
    const Puzzle& p_;
    std::vector<std::vector<std::uint64_t>> zobrist_;
    std::unordered_set<std::uint64_t> visited_;

    // Try to fill a tile in the state with dot id `d` and update hash/counters.
    bool tryFill(State& s, int tx, int ty, int d) {
        if (!inTileBounds(tx, ty)) return true;  // out-of-bounds is ignored
        const int16_t cur = s.at(tx, ty);
        if (cur == -1) {
            s.at(tx, ty) = static_cast<int16_t>(d);
            s.hash ^= zobrist_[ty * p_.width() + tx][d];
            ++s.filledCount;
            return true;
        }
        return cur == d;  // ok if already same dot, conflict otherwise
    }

    // Prefill tiles that are directly occupied by a dot: center, edge, corner.
    bool seedForcedTiles(State& s) {
        for (int d = 0; d < static_cast<int>(p_.dotCount()); ++d) {
            const int dx = p_.dots()[d].x();
            const int dy = p_.dots()[d].y();
            const bool oddx = (dx % 2) != 0;
            const bool oddy = (dy % 2) != 0;

            if (oddx && oddy) {
                // Dot at tile center
                const int tx = (dx - 1) / 2;
                const int ty = (dy - 1) / 2;
                if (!tryFill(s, tx, ty, d)) {
                    std::fprintf(stderr, "Error: Overlapping dots at center?\n");
                    return false;
                }
            } else if (oddx && !oddy) {
                // Dot on horizontal edge between two tiles (above/below)
                const int tx = (dx - 1) / 2;
                const int ty1 = dy / 2 - 1;
                const int ty2 = dy / 2;
                if (!tryFill(s, tx, ty1, d) || !tryFill(s, tx, ty2, d)) {
                    std::fprintf(stderr, "Error: Overlapping dots on edge?\n");
                    return false;
                }
            } else if (!oddx && oddy) {
                // Dot on vertical edge between two tiles (left/right)
                const int ty = (dy - 1) / 2;
                const int tx1 = dx / 2 - 1;
                const int tx2 = dx / 2;
                if (!tryFill(s, tx1, ty, d) || !tryFill(s, tx2, ty, d)) {
                    std::fprintf(stderr, "Error: Overlapping dots on edge?\n");
                    return false;
                }
            } else {
                // Dot at a corner shared by four tiles
                const int tx1 = dx / 2 - 1;
                const int tx2 = dx / 2;
                const int ty1 = dy / 2 - 1;
                const int ty2 = dy / 2;
                if (!tryFill(s, tx1, ty1, d) || !tryFill(s, tx1, ty2, d)
                    || !tryFill(s, tx2, ty1, d) || !tryFill(s, tx2, ty2, d)) {
                    std::fprintf(stderr, "Error: Overlapping dots at corner?\n");
                    return false;
                }
            }
        }
        return true;
    }

    void initZobrist() {
        const std::size_t cells = static_cast<std::size_t>(p_.width())
                                  * static_cast<std::size_t>(p_.height());
        const std::size_t nd = p_.dotCount();
        zobrist_.assign(cells, std::vector<std::uint64_t>(nd));
        std::mt19937_64 rng(std::random_device{}());
        for (std::size_t i = 0; i < cells; ++i) {
            for (std::size_t j = 0; j < nd; ++j) {
                zobrist_[i][j] = rng();
            }
        }
    }

    bool inTileBounds(int tx, int ty) const noexcept {
        return (tx >= 0 && tx < p_.width() && ty >= 0 && ty < p_.height());
    }

    bool isValidTile(const State& s, int tx, int ty) const noexcept {
        if (!inTileBounds(tx, ty)) return false;
        return s.at(tx, ty) == -1;
    }

    static std::pair<int, int> getSymmetricTile(int dot_x, int dot_y, int tx,
                                                int ty) noexcept {
        const int cx = tx * 2 + 1;
        const int cy = ty * 2 + 1;
        const int rx = 2 * dot_x - cx;
        const int ry = 2 * dot_y - cy;
        return {(rx - 1) / 2, (ry - 1) / 2};
    }

    static bool touchesDot(int dot_x, int dot_y, int tx, int ty) noexcept {
        const int cx = tx * 2 + 1;
        const int cy = ty * 2 + 1;
        return (std::abs(cx - dot_x) <= 1) && (std::abs(cy - dot_y) <= 1);
    }

    bool allDotsUsed(const State& s) const {
        std::vector<char> used(p_.dotCount(), 0);
        for (int y = 0; y < p_.height(); ++y) {
            for (int x = 0; x < p_.width(); ++x) {
                const int v = s.at(x, y);
                if (v >= 0 && v < static_cast<int>(used.size())) used[v] = 1;
            }
        }
        return std::all_of(used.begin(), used.end(),
                           [](char c) { return c != 0; });
    }
};

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    if (argc != 2) {
        std::fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    std::ifstream fin(argv[1]);
    if (!fin) {
        std::perror("Error opening file");
        return 1;
    }
    std::string line;
    if (!std::getline(fin, line)) {
        std::fprintf(stderr, "Error reading file\n");
        return 1;
    }
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.pop_back();

    const std::string prefix = "Game ID: ";
    std::unique_ptr<Puzzle> puzzle;
    const auto pos = line.find(prefix);
    if (pos != std::string::npos) {
        puzzle = Puzzle::fromGameId(line.substr(pos + prefix.size()));
    } else if (line.find(':') != std::string::npos) {
        puzzle = Puzzle::fromGameId(line);
    }

    if (!puzzle) {
        std::fprintf(stderr, "Failed to parse puzzle.\n");
        return 1;
    }

    Solver solver(*puzzle);
    auto result = solver.solve();
    if (result) {
        solver.printSolution(*result);
        return 0;
    }
    std::cout << "No solution found." << '\n';
    return 0;
}
