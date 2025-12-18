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
#include <atomic>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

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
        shared_ = std::make_shared<Shared>();
    }

    // Copy constructor: safe because Zobrist table and puzzle reference are immutable.
    // The visited table and (optional) solution output are shared across workers.
    Solver(const Solver& other)
        : p_(other.p_),
          w_(other.w_),
          h_(other.h_),
          grid_(other.grid_),
          filled_count_(other.filled_count_),
          current_hash_(other.current_hash_),
          zobrist_(other.zobrist_),
          shared_(other.shared_) {}

    bool solve() {
        // Prefill forced tiles
        if (!seedForcedTiles()) {
            return false;
        }

        // Mark the seeded state as visited.
        (void)tryMarkVisited(current_hash_);

#ifdef _OPENMP
        // Parallelize the search by splitting at the root frontier.
        // Each worker gets an independent Solver copy and runs DFS sequentially.
        // We share a visited set (protected by a lock) and a global "found" flag.
        const int max_threads = omp_get_max_threads();
        if (max_threads > 1) {
            std::atomic<bool> found{false};
            std::vector<std::vector<int16_t>> solution;

            const std::vector<Move> root_moves = generateMoves();
            if (root_moves.empty()) return false;

            #pragma omp parallel
            {
                // Thread-local small cache to reduce lock pressure.
                std::unordered_set<std::uint64_t> local_seen;
                local_seen.reserve(1u << 16);

                #pragma omp for schedule(dynamic, 1)
                for (int i = 0; i < static_cast<int>(root_moves.size()); ++i) {
                    if (found.load(std::memory_order_relaxed)) continue;

                    Solver worker(*this);
                    worker.applyMove(root_moves[i]);

                    if (!worker.tryMarkVisitedWithLocal(worker.current_hash_, local_seen)) continue;

                    if (worker.dfs(found, solution, local_seen)) {
                        found.store(true, std::memory_order_relaxed);
                    }
                }
            }

            if (found.load(std::memory_order_relaxed)) {
                grid_ = std::move(solution);
                filled_count_ = w_ * h_;
                return true;
            }
            return false;
        }
#endif

        // Fallback: sequential DFS.
        std::atomic<bool> found{false};
        std::vector<std::vector<int16_t>> solution;
        std::unordered_set<std::uint64_t> local_seen;
        local_seen.reserve(1u << 16);
        const bool ok = dfs(found, solution, local_seen);
        if (ok) {
            grid_ = std::move(solution);
            filled_count_ = w_ * h_;
        }
        return ok;
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

    struct Shared {
        std::unordered_set<std::uint64_t> visited;
#ifdef _OPENMP
        omp_lock_t lock;
        Shared() {
            omp_init_lock(&lock);
            visited.reserve(1u << 20);
        }
        ~Shared() { omp_destroy_lock(&lock); }
        bool insertIfNew(std::uint64_t h) {
            omp_set_lock(&lock);
            const bool inserted = visited.insert(h).second;
            omp_unset_lock(&lock);
            return inserted;
        }
#else
        Shared() { visited.reserve(1u << 20); }
        bool insertIfNew(std::uint64_t h) { return visited.insert(h).second; }
#endif
    };

    std::shared_ptr<Shared> shared_;

    struct Move {
        int tx, ty;
        int d;
        int sx, sy;
        bool symmetric_was_empty;
    };

    bool dfs(std::atomic<bool>& found,
             std::vector<std::vector<int16_t>>& solution,
             std::unordered_set<std::uint64_t>& local_seen) {
        if (found.load(std::memory_order_relaxed)) return false;

        if (filled_count_ == w_ * h_) {
            if (!allDotsUsed()) return false;

            // Commit solution once.
            bool expected = false;
            if (found.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
#ifdef _OPENMP
                #pragma omp critical
#endif
                {
                    solution = grid_;
                }
            }
            return true;
        }

        const std::vector<Move> moves = generateMoves();
        if (moves.empty()) return false;

        for (const auto& m : moves) {
            if (found.load(std::memory_order_relaxed)) return false;

            applyMove(m);

            if (tryMarkVisitedWithLocal(current_hash_, local_seen)) {
                if (dfs(found, solution, local_seen)) {
                    undoMove(m);
                    return true;
                }
            }

            undoMove(m);
        }

        return false;
    }

    std::vector<Move> generateMoves() const {
        // Strategy: Iterate over all empty cells that are adjacent to a filled cell.
        // For each such cell, try assigning it to the adjacent dot.
        std::vector<Move> moves;
        moves.reserve(static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_));

        static constexpr int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

        for (int y = 0; y < h_; ++y) {
            for (int x = 0; x < w_; ++x) {
                if (grid_[y][x] != -1) continue;

                for (const auto& dir : dirs) {
                    const int nx = x + dir[0];
                    const int ny = y + dir[1];
                    if (!inTileBounds(nx, ny)) continue;
                    const int d = grid_[ny][nx];
                    if (d == -1) continue;

                    // De-dup (x,y,d) candidates.
                    bool already_added = false;
                    for (const auto& m : moves) {
                        if (m.tx == x && m.ty == y && m.d == d) {
                            already_added = true;
                            break;
                        }
                    }
                    if (already_added) continue;

                    const auto [sx, sy] = getSymmetricTile(p_.dots()[d].x(), p_.dots()[d].y(), x, y);
                    if (!inTileBounds(sx, sy)) continue;

                    // Symmetric tile must be empty OR already filled by the same dot.
                    const bool sym_empty = (grid_[sy][sx] == -1);
                    if (!sym_empty && grid_[sy][sx] != d) continue;

                    moves.push_back({x, y, d, sx, sy, sym_empty});
                }
            }
        }
        return moves;
    }

    bool tryMarkVisited(std::uint64_t h) {
        return shared_->insertIfNew(h);
    }

    bool tryMarkVisitedWithLocal(std::uint64_t h, std::unordered_set<std::uint64_t>& local_seen) {
        if (local_seen.find(h) != local_seen.end()) return false;
        if (!tryMarkVisited(h)) return false;
        local_seen.insert(h);
        return true;
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
