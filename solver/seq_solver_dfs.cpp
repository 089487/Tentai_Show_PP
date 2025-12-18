/**
 * @file seq_solver_dfs.cpp
 * @brief Sequential solver for the Tentai Show (Symmetry) puzzle using DFS.
 *
 * Parses a Game ID string from an input file, constructs the puzzle grid
 * and dot layout, then performs a depth-first search (DFS) with backtracking
 * to fill the board. Uses Zobrist hashing to prune visited states.
 *
 * This version uses a global state (member of Solver) and modifies it in-place
 * to avoid the overhead of copying the grid state (O(map size)) at each step.
 * State updates are O(1).
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

class Dot {
   public:
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
    Puzzle() = default;

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

class Solver {
   public:
    explicit Solver(const Puzzle& puz) : p_(puz) {
        w_ = p_.width();
        h_ = p_.height();
        grid_.assign(h_, std::vector<int16_t>(w_, -1));
        filled_count_ = 0;
        current_hash_ = 0;
        initZobrist();
    }

    bool solve() {
        // Prefill forced tiles
        if (!seedForcedTiles()) {
            return false;
        }

        visited_.insert(current_hash_);
        return dfs();
    }

    void printSolution() const {
        std::cout << "\nPuzzle Grid (" << w_ << "x" << h_ << "):\n";
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
                            (tx_left >= 0) ? grid_[ty][tx_left] : -2;
                        const int id_right =
                            (tx_right < w_) ? grid_[ty][tx_right] : -2;
                        std::cout << (id_left != id_right ? '|' : ' ');
                    } else if (y % 2 == 0) {
                        const int ty_up = (y - 2) / 2;
                        const int ty_down = y / 2;
                        const int tx = (x - 1) / 2;
                        const int id_up = (ty_up >= 0) ? grid_[ty_up][tx] : -2;
                        const int id_down =
                            (ty_down < h_) ? grid_[ty_down][tx] : -2;
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
    int w_, h_;
    std::vector<std::vector<int16_t>> grid_;
    int filled_count_;
    std::uint64_t current_hash_;
    std::vector<std::vector<std::uint64_t>> zobrist_;
    std::unordered_set<std::uint64_t> visited_;

    struct Move {
        int tx, ty;
        int d;
        int sx, sy;
        bool symmetric_was_empty;
    };

    bool dfs() {
        if (filled_count_ == w_ * h_) {
            return allDotsUsed();
        }

        // Generate all valid expansions
        // Strategy: Iterate over all empty cells that are adjacent to a filled cell.
        // For each such cell, try assigning it to the adjacent dot.
        
        std::vector<Move> moves;
        
        for (int y = 0; y < h_; ++y) {
            for (int x = 0; x < w_; ++x) {
                if (grid_[y][x] == -1) {
                    // Check neighbors for filled cells
                    static constexpr int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                    for (auto& dir : dirs) {
                        int nx = x + dir[0];
                        int ny = y + dir[1];
                        if (inTileBounds(nx, ny) && grid_[ny][nx] != -1) {
                            int d = grid_[ny][nx];
                            
                            // Check if we already have (x,y,d) in moves
                            bool already_added = false;
                            for(const auto& m : moves) {
                                if(m.tx == x && m.ty == y && m.d == d) {
                                    already_added = true;
                                    break;
                                }
                            }
                            if(already_added) continue;

                            // Validate symmetry
                            const auto [sx, sy] = getSymmetricTile(p_.dots()[d].x(), p_.dots()[d].y(), x, y);
                            
                            if (isValidTile(sx, sy) || (sx == x && sy == y)) {
                                // Valid move candidate
                                bool sym_empty = (grid_[sy][sx] == -1);
                                // If symmetric tile is already filled, it MUST be filled by d
                                if (!sym_empty && grid_[sy][sx] != d) continue;
                                
                                moves.push_back({x, y, d, sx, sy, sym_empty});
                            }
                        }
                    }
                }
            }
        }

        // If no moves and not full, we are stuck
        if (moves.empty()) return false;

        for (const auto& m : moves) {
            // Apply move
            applyMove(m);

            if (visited_.find(current_hash_) == visited_.end()) {
                visited_.insert(current_hash_);
                if (dfs()) return true;
            }

            // Backtrack
            undoMove(m);
        }

        return false;
    }

    void applyMove(const Move& m) {
        grid_[m.ty][m.tx] = m.d;
        current_hash_ ^= zobrist_[m.ty * w_ + m.tx][m.d];
        filled_count_++;

        if (m.tx != m.sx || m.ty != m.sy) {
            if (m.symmetric_was_empty) {
                grid_[m.sy][m.sx] = m.d;
                current_hash_ ^= zobrist_[m.sy * w_ + m.sx][m.d];
                filled_count_++;
            }
        }
    }

    void undoMove(const Move& m) {
        grid_[m.ty][m.tx] = -1;
        current_hash_ ^= zobrist_[m.ty * w_ + m.tx][m.d];
        filled_count_--;

        if (m.tx != m.sx || m.ty != m.sy) {
            if (m.symmetric_was_empty) {
                grid_[m.sy][m.sx] = -1;
                current_hash_ ^= zobrist_[m.sy * w_ + m.sx][m.d];
                filled_count_--;
            }
        }
    }

    bool tryFill(int tx, int ty, int d) {
        if (!inTileBounds(tx, ty)) return true;
        const int16_t cur = grid_[ty][tx];
        if (cur == -1) {
            grid_[ty][tx] = static_cast<int16_t>(d);
            current_hash_ ^= zobrist_[ty * w_ + tx][d];
            ++filled_count_;
            return true;
        }
        return cur == d;
    }

    bool seedForcedTiles() {
        for (int d = 0; d < static_cast<int>(p_.dotCount()); ++d) {
            const int dx = p_.dots()[d].x();
            const int dy = p_.dots()[d].y();
            const bool oddx = (dx % 2) != 0;
            const bool oddy = (dy % 2) != 0;

            if (oddx && oddy) {
                const int tx = (dx - 1) / 2;
                const int ty = (dy - 1) / 2;
                if (!tryFill(tx, ty, d)) return false;
            } else if (oddx && !oddy) {
                const int tx = (dx - 1) / 2;
                const int ty1 = dy / 2 - 1;
                const int ty2 = dy / 2;
                if (!tryFill(tx, ty1, d) || !tryFill(tx, ty2, d)) return false;
            } else if (!oddx && oddy) {
                const int ty = (dy - 1) / 2;
                const int tx1 = dx / 2 - 1;
                const int tx2 = dx / 2;
                if (!tryFill(tx1, ty, d) || !tryFill(tx2, ty, d)) return false;
            } else {
                const int tx1 = dx / 2 - 1;
                const int tx2 = dx / 2;
                const int ty1 = dy / 2 - 1;
                const int ty2 = dy / 2;
                if (!tryFill(tx1, ty1, d) || !tryFill(tx1, ty2, d)
                    || !tryFill(tx2, ty1, d) || !tryFill(tx2, ty2, d)) {
                    return false;
                }
            }
        }
        return true;
    }

    void initZobrist() {
        const std::size_t cells = static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_);
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
        return (tx >= 0 && tx < w_ && ty >= 0 && ty < h_);
    }

    bool isValidTile(int tx, int ty) const noexcept {
        if (!inTileBounds(tx, ty)) return false;
        return grid_[ty][tx] == -1;
    }

    static std::pair<int, int> getSymmetricTile(int dot_x, int dot_y, int tx, int ty) noexcept {
        const int cx = tx * 2 + 1;
        const int cy = ty * 2 + 1;
        const int rx = 2 * dot_x - cx;
        const int ry = 2 * dot_y - cy;
        return {(rx - 1) / 2, (ry - 1) / 2};
    }

    bool allDotsUsed() const {
        std::vector<char> used(p_.dotCount(), 0);
        for (int y = 0; y < h_; ++y) {
            for (int x = 0; x < w_; ++x) {
                const int v = grid_[y][x];
                if (v >= 0 && v < static_cast<int>(used.size())) used[v] = 1;
            }
        }
        return std::all_of(used.begin(), used.end(), [](char c) { return c != 0; });
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
    if (solver.solve()) {
        solver.printSolution();
        return 0;
    }
    std::cout << "No solution found." << '\n';
    return 0;
}
