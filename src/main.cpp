#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <iostream>
#include <string>
#include <thread>
#include <variant>
#include <vector>

using screen = std::array<std::array<int, 300>, 300>;

struct Color
{
    unsigned char r, g, b;
};

int compactColor(Color c)
{
    return c.r * 1000000 + c.g * 1000 + c.b;
}

inline void unpackColor(int packed, int& r, int& g, int& b)
{
    r = packed / 1000000;
    g = (packed / 1000) % 1000;
    b = packed % 1000;
}

enum class KeyType
{
    Char,
    Enter,
    Escape,
    Up,
    Down,
    Left,
    Right,
    Backspace,
    Tab,
    Unknown
};

using KeyEvent = std::variant<char, KeyType>;

#ifdef _WIN32

void enableRawInput()
{
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hIn, &mode);
    SetConsoleMode(hIn, mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
}

std::vector<KeyEvent> pollInput()
{
    std::vector<KeyEvent> keys;
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD events = 0;

    if (!GetNumberOfConsoleInputEvents(hIn, &events) || events == 0)
        return keys;

    INPUT_RECORD rec;
    DWORD read;

    for (DWORD i = 0; i < events; ++i)
    {
        ReadConsoleInput(hIn, &rec, 1, &read);
        if (rec.EventType != KEY_EVENT)
            continue;

        auto& key = rec.Event.KeyEvent;
        if (!key.bKeyDown)
            continue;

        if (key.uChar.AsciiChar)
            keys.emplace_back(key.uChar.AsciiChar);
        else
        {
            switch (key.wVirtualKeyCode)
            {
            case VK_UP: keys.emplace_back(KeyType::Up);
                break;
            case VK_DOWN: keys.emplace_back(KeyType::Down);
                break;
            case VK_LEFT: keys.emplace_back(KeyType::Left);
                break;
            case VK_RIGHT: keys.emplace_back(KeyType::Right);
                break;
            case VK_RETURN: keys.emplace_back(KeyType::Enter);
                break;
            case VK_ESCAPE: keys.emplace_back(KeyType::Escape);
                break;
            case VK_BACK: keys.emplace_back(KeyType::Backspace);
                break;
            case VK_TAB: keys.emplace_back(KeyType::Tab);
                break;
            default: keys.emplace_back(KeyType::Unknown);
                break;
            }
        }
    }
    return keys;
}

WORD rgbToWinAttr(int r, int g, int b)
{
    WORD attr = 0;
    if (r > 128) attr |= FOREGROUND_RED;
    if (g > 128) attr |= FOREGROUND_GREEN;
    if (b > 128) attr |= FOREGROUND_BLUE;
    if (r > 200 || g > 200 || b > 200)
        attr |= FOREGROUND_INTENSITY;
    return attr;
}

#endif

#ifndef _WIN32
termios origTerm;
std::deque<char> inputBuffer;

void enableRawInput()
{
    tcgetattr(STDIN_FILENO, &origTerm);
    termios t = origTerm;
    t.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
}

void restoreTerminal()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &origTerm);
}

std::vector<KeyEvent> pollInput()
{
    std::vector<KeyEvent> keys;
    char buf[32];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));

    for (ssize_t i = 0; i < n; ++i)
        inputBuffer.push_back(buf[i]);

    while (!inputBuffer.empty())
    {
        char c = inputBuffer.front();
        inputBuffer.pop_front();

        if (c == '\x1b')
        {
            if (inputBuffer.size() >= 2 && inputBuffer[0] == '[')
            {
                char code = inputBuffer[1];
                inputBuffer.pop_front();
                inputBuffer.pop_front();

                switch (code)
                {
                case 'A': keys.emplace_back(KeyType::Up);
                    break;
                case 'B': keys.emplace_back(KeyType::Down);
                    break;
                case 'C': keys.emplace_back(KeyType::Right);
                    break;
                case 'D': keys.emplace_back(KeyType::Left);
                    break;
                default: keys.emplace_back(KeyType::Unknown);
                    break;
                }
            }
        }
        else if (c == '\n' || c == '\r')
            keys.emplace_back(KeyType::Enter);
        else if (c == 127)
            keys.emplace_back(KeyType::Backspace);
        else
            keys.emplace_back(c);
    }
    return keys;
}
#endif

bool getTerminalSize(int& w, int& h)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(hConsole, &csbi))
        return false;
    w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
#else
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1)
        return false;
    w = ws.ws_col;
    h = ws.ws_row;
#endif
    return true;
}

void limitFPS(int fps)
{
    using clock = std::chrono::steady_clock;
    static auto next = clock::now();
    next += std::chrono::nanoseconds(1'000'000'000 / fps);
    std::this_thread::sleep_until(next);
}

class Window
{
public:
    Window()
    {
        clear({0, 0, 0});
    }

    void clear(Color c)
    {
        int v = compactColor(c);
        for (auto& row : buffer)
            row.fill(v);
    }

    void drawPixel(int x, int y, Color c)
    {
        if (x < 0 || y < 0 || x >= 300 || y >= 300)
            return;
        buffer[y][x] = compactColor(c);
    }

    void drawLine(int x0, int y0, int x1, int y1, Color c)
    {
        int dx = abs(x1 - x0);
        int dy = abs(y1 - y0);
        int steps = max(dx, dy);

        if (steps == 0)
        {
            drawPixel(x0, y0, c);
            return;
        }

        int packed = compactColor(c);

        for (int i = 0; i <= steps; ++i)
        {
            float t = static_cast<float>(i) / steps;
            int x = static_cast<int>(x0 + t * (x1 - x0));
            int y = static_cast<int>(y0 + t * (y1 - y0));

            if (x >= 0 && y >= 0 && x < 300 && y < 300)
                buffer[y][x] = packed;
        }
    }

    void present() const
    {
        drawBuffer(buffer);
    }

private:
    screen buffer{};

    static void drawBuffer(const screen& pixelBuff)
    {
#ifdef _WIN32
        static HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(hConsole, &csbi);

        int maxW = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        int maxH = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

        int h = pixelBuff.size() & ~1;
        int w = pixelBuff[0].size();
        int cellH = min(h / 2, maxH);
        int cellW = min(w, maxW);

        static std::vector<CHAR_INFO> buf(cellW * cellH);

        for (int y = 0; y < cellH; ++y)
        {
            for (int x = 0; x < cellW; ++x)
            {
                int ur, ug, ub, lr, lg, lb;
                unpackColor(pixelBuff[y * 2][x], ur, ug, ub);
                unpackColor(pixelBuff[y * 2 + 1][x], lr, lg, lb);

                auto& cell = buf[y * cellW + x];
                cell.Char.UnicodeChar = L'\u2584';
                cell.Attributes =
                    rgbToWinAttr(lr, lg, lb) |
                    (rgbToWinAttr(ur, ug, ub) << 4);
            }
        }

        COORD size{(SHORT)cellW, (SHORT)cellH};
        COORD zero{0, 0};
        SMALL_RECT rect{0, 0, (SHORT)(cellW - 1), (SHORT)(cellH - 1)};
        WriteConsoleOutputW(hConsole, buf.data(), size, zero, &rect);
#else
        write(STDOUT_FILENO, "\x1b[H\x1b[J", 6);

        int termW, termH;
        getTerminalSize(termW, termH);

        size_t cellH = std::min(pixelBuff.size() / 2, size_t(termH));
        size_t cellW = std::min(pixelBuff[0].size(), size_t(termW));

        std::string frame;
        frame.reserve(cellH * cellW * 12);

        auto appendInt = [&](int v)
        {
            if (v >= 100)
            {
                frame.push_back('0' + v / 100);
                frame.push_back('0' + (v / 10) % 10);
                frame.push_back('0' + v % 10);
            }
            else if (v >= 10)
            {
                frame.push_back('0' + v / 10);
                frame.push_back('0' + v % 10);
            }
            else
                frame.push_back('0' + v);
        };

        for (size_t y = 0; y < cellH; ++y)
        {
            for (size_t x = 0; x < cellW; ++x)
            {
                int ur, ug, ub, lr, lg, lb;
                unpackColor(pixelBuff[y * 2][x], ur, ug, ub);
                unpackColor(pixelBuff[y * 2 + 1][x], lr, lg, lb);

                frame.append("\x1b[48;2;");
                appendInt(ur);
                frame.push_back(';');
                appendInt(ug);
                frame.push_back(';');
                appendInt(ub);
                frame.push_back('m');

                frame.append("\x1b[38;2;");
                appendInt(lr);
                frame.push_back(';');
                appendInt(lg); frame.push_back(';');
                appendInt(lb);
                frame.push_back('m');

                frame.append("▄");
            }
            frame.append("\x1b[0m\n");
        }
        write(STDOUT_FILENO, frame.data(), frame.size());
#endif
    }
};

int main()
{
#ifndef _WIN32
    atexit(restoreTerminal);
#endif
    enableRawInput();

    Window window;

    window.drawPixel(0, 0, {255, 0, 0});
    window.drawPixel(2, 2, {0, 255, 0});
    window.drawLine(4, 4, 40, 20, {255, 0, 0});

    bool running = true;
    int termW = 0;
    int termH = 0;

    while (running)
    {
        // input
        for (auto keys = pollInput(); auto& k : keys)
        {
            if (!std::holds_alternative<char>(k) &&
                std::get<KeyType>(k) == KeyType::Escape)
            {
                running = false;
            }
        }

        // terminal size guard
        if (!getTerminalSize(termW, termH))
        {
            limitFPS(15);
            continue;
        }

        if (termW < 300 || termH < 150)
        {
            limitFPS(15);
            continue;
        }

        // render
        window.present();
        limitFPS(15);
    }

    return 0;
}

