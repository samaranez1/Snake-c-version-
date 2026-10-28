// Terminal Snake for Termux. No ncurses needed: termios + ANSI escapes.
// Controls: W/A/S/D or arrow keys, Q = quit, R = restart after game over.
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <string>
#include <chrono>

static termios g_old;
static bool g_raw = false;

static void restore_terminal() {
    if (g_raw) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_old);
        g_raw = false;
    }
    printf("\x1b[?25h\x1b[0m\n");
    fflush(stdout);
}

static void on_signal(int) {
    restore_terminal();
    _exit(0);
}

static void enable_raw() {
    tcgetattr(STDIN_FILENO, &g_old);
    termios t = g_old;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    g_raw = true;
    printf("\x1b[?25l\x1b[2J");
    fflush(stdout);
    atexit(restore_terminal);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
}

struct P { int x, y; };

static const int W = 30;
static const int H = 15;

static bool on_snake(const std::deque<P>& s, int x, int y) {
    for (const P& p : s) if (p.x == x && p.y == y) return true;
    return false;
}

static void place_food(const std::deque<P>& s, P& f) {
    do {
        f.x = rand() % W;
        f.y = rand() % H;
    } while (on_snake(s, f.x, f.y));
}

static void draw(const std::deque<P>& s, const P& f, int score, int best) {
    std::string out = "\x1b[H";
    out += "SNAKE  score: " + std::to_string(score) +
           "  best: " + std::to_string(best) + "   \n";
    out += "+" + std::string(W, '-') + "+\n";
    for (int y = 0; y < H; y++) {
        out += "|";
        for (int x = 0; x < W; x++) {
            if (s.front().x == x && s.front().y == y) out += "\x1b[1;32m@\x1b[0m";
            else if (on_snake(s, x, y)) out += "\x1b[32mo\x1b[0m";
            else if (f.x == x && f.y == y) out += "\x1b[1;31m*\x1b[0m";
            else out += " ";
        }
        out += "|\n";
    }
    out += "+" + std::string(W, '-') + "+\n";
    out += "WASD / arrows to move, Q to quit\n";
    fwrite(out.data(), 1, out.size(), stdout);
    fflush(stdout);
}

// Returns true if the user asked to quit.
static bool play(int& best) {
    std::deque<P> snake = {{W / 2, H / 2}, {W / 2 - 1, H / 2}, {W / 2 - 2, H / 2}};
    int dx = 1, dy = 0;     // current direction
    int ndx = 1, ndy = 0;   // requested direction
    int score = 0;
    int delay_ms = 140;
    P food;
    place_food(snake, food);

    while (true) {
        draw(snake, food, score, best);

        auto start = std::chrono::steady_clock::now();
        while (true) {
            auto now = std::chrono::steady_clock::now();
            int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            int remain = delay_ms - elapsed;
            if (remain <= 0) break;

            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);
            timeval tv{remain / 1000, (remain % 1000) * 1000};
            if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0) {
                char c;
                if (read(STDIN_FILENO, &c, 1) == 1) {
                    if (c == 27) { // arrow keys: ESC [ A/B/C/D
                        char a, b;
                        if (read(STDIN_FILENO, &a, 1) == 1 && a == '[' &&
                            read(STDIN_FILENO, &b, 1) == 1) {
                            if (b == 'A') c = 'w';
                            else if (b == 'B') c = 's';
                            else if (b == 'C') c = 'd';
                            else if (b == 'D') c = 'a';
                        }
                    }
                    if ((c == 'w' || c == 'W') && dy == 0) { ndx = 0; ndy = -1; }
                    else if ((c == 's' || c == 'S') && dy == 0) { ndx = 0; ndy = 1; }
                    else if ((c == 'a' || c == 'A') && dx == 0) { ndx = -1; ndy = 0; }
                    else if ((c == 'd' || c == 'D') && dx == 0) { ndx = 1; ndy = 0; }
                    else if (c == 'q' || c == 'Q') return true;
                }
            }
        }

        dx = ndx; dy = ndy;
        P head = {snake.front().x + dx, snake.front().y + dy};

        bool hit_wall = head.x < 0 || head.x >= W || head.y < 0 || head.y >= H;
        if (hit_wall || on_snake(snake, head.x, head.y)) break;

        snake.push_front(head);
        if (head.x == food.x && head.y == food.y) {
            score += 10;
            if (score > best) best = score;
            if (delay_ms > 60) delay_ms -= 3;
            place_food(snake, food);
        } else {
            snake.pop_back();
        }
    }

    draw(snake, food, score, best);
    printf("\n\x1b[1;31mGAME OVER\x1b[0m  Score: %d   R = restart, Q = quit\n", score);
    fflush(stdout);

    while (true) {
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1) {
            if (c == 'r' || c == 'R') { printf("\x1b[2J"); return false; }
            if (c == 'q' || c == 'Q') return true;
        }
        usleep(20000);
    }
}

int main() {
    srand((unsigned)time(nullptr));
    enable_raw();
    int best = 0;
    while (!play(best)) {}
    return 0;
}
