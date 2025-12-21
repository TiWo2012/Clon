//
// Created by TIMO on 20.12.2025.
//

#define NOMINMAX

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif
#include <array>
#include <iostream>
#include <print>
#include <chrono>
#include <thread>
#include <string>
#include <algorithm>

using screen = std::array<std::array<int, 300>, 300>;

bool getTerminalSize(int& width, int& height)
{
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE)
        return false;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hConsole, &csbi))
        return false;

    width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return true;
#else
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1)
        return false;

    width = ws.ws_col;
    height = ws.ws_row;
    return true;
#endif
}

void clearConsole()
{
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE)
        return;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hConsole, &csbi))
        return;

    DWORD cellCount = csbi.dwSize.X * csbi.dwSize.Y;
    DWORD written;

    COORD home = {0, 0};

    FillConsoleOutputCharacterA(
        hConsole,
        ' ',
        cellCount,
        home,
        &written
    );

    FillConsoleOutputAttribute(
        hConsole,
        csbi.wAttributes,
        cellCount,
        home,
        &written
    );

    SetConsoleCursorPosition(hConsole, home);
#else
    // ANSI escape sequence: clear screen + move cursor home
    std::cout << "\x1b[2J\x1b[H";
    std::cout.flush();
#endif
}

struct Color
{
    unsigned char r, g, b;
};

int compactColor(const Color color)
{
    return static_cast<int>(color.r) * 1000000 + static_cast<int>(color.g) * 1000 + static_cast<int>(color.b);
}

inline void unpackColor(int packed, int& r, int& g, int& b)
{
    r = packed / 1'000'000;
    g = (packed / 1'000) % 1'000;
    b = packed % 1'000;
}

void drawPixel(screen& pixelBuff, const int x, const int y, const Color color)
{
    pixelBuff[y][x] = compactColor(color);
}

void limitFPS(const int targetFps)
{
    using clock = std::chrono::steady_clock;
    static auto nextFrame = clock::now();

    const auto frameDuration =
        std::chrono::nanoseconds(1'000'000'000 / targetFps);

    nextFrame += frameDuration;
    std::this_thread::sleep_until(nextFrame);
}

void hideCursor()
{
    std::cout << "\x1b[?25l";
}

void showCursor()
{
    std::cout << "\x1b[?25h";
}

void moveCursorHome()
{
    std::cout << "\x1b[H";
}

bool testTerminalSize(bool& terminalIsValid, int terminalWidth, int terminalHeight)
{
    if (terminalWidth < 300)
    {
        // std::println("Terminal too small (Width: {})", terminalWidth);
        limitFPS(15);
        return true;
    }
    else
    {
        terminalIsValid = true;
    }

    if (terminalHeight < 150)
    {
        // std::println("Terminal too small (Height: {})", terminalHeight);
        limitFPS(15);
        return true;
    }
    else
    {
        terminalIsValid = true;
    }
    return false;
}

void initializeTerminal()
{
#ifdef _WIN32
    // NOTHING for Win32 renderer
#else
    std::cout << "\x1b[?1049h\x1b[?25l";
#endif
}

void deinitializeTerminal()
{
#ifdef _WIN32
    // NOTHING
#else
    std::cout << "\x1b[?25h\x1b[?1049l";
#endif
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

void drawBuff(const screen& pixelBuff)
{
#ifdef _WIN32
    static HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);

    const int maxW =
        csbi.srWindow.Right - csbi.srWindow.Left + 1;
    const int maxH =
        csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

    const int pixelHeight =
        (int(pixelBuff.size()) & ~1);
    const int pixelWidth =
        int(pixelBuff[0].size());

    const int cellHeight =
        std::min(pixelHeight / 2, maxH);
    const int cellWidth =
        std::min(pixelWidth, maxW);

    static std::vector<CHAR_INFO> buffer;
    buffer.resize(cellWidth * cellHeight);

    for (int y = 0; y < cellHeight; ++y)
    {
        for (int x = 0; x < cellWidth; ++x)
        {
            int ur, ug, ub;
            int lr, lg, lb;

            unpackColor(pixelBuff[y * 2][x], ur, ug, ub);
            unpackColor(pixelBuff[y * 2 + 1][x], lr, lg, lb);

            WORD fg = rgbToWinAttr(lr, lg, lb);
            WORD bg = rgbToWinAttr(ur, ug, ub) << 4;

            CHAR_INFO& c = buffer[y * cellWidth + x];
            c.Char.UnicodeChar = L'\u2584';
            c.Attributes = fg | bg;
        }
    }

    COORD size = {
        SHORT(cellWidth),
        SHORT(cellHeight)
    };

    COORD zero = {0, 0};

    SMALL_RECT rect = {
        0,
        0,
        SHORT(cellWidth - 1),
        SHORT(cellHeight - 1)
    };

    WriteConsoleOutputW(
        hConsole,
        buffer.data(),
        size,
        zero,
        &rect
    );

#else

    std::string frame;
    frame.reserve(300 * 150 * 20);

    const size_t height = pixelBuff.size();
    const size_t width = pixelBuff[0].size();

    for (size_t y = 0; y + 1 < height; y += 2)
    {
        for (size_t x = 0; x < width; ++x)
        {
            int ur, ug, ub;
            int lr, lg, lb;

            unpackColor(pixelBuff[y][x], ur, ug, ub);
            unpackColor(pixelBuff[y + 1][x], lr, lg, lb);

            frame += std::format(
                "\x1b[48;2;{};{};{}m\x1b[38;2;{};{};{}m▄",
                ur, ug, ub, lr, lg, lb
            );
        }
        frame += "\x1b[0m\n";
    }

    std::cout << frame;
#endif
}

int main()
{
    bool running = true;
    int terminalWidth, terminalHeight;

    initializeTerminal();

    auto* pixelBuff = new screen{};
    drawPixel(*pixelBuff, 0, 0, {255, 0, 0});

    while (running)
    {
        getTerminalSize(terminalWidth, terminalHeight);

        if (terminalWidth < 300 || terminalHeight < 150)
        {
            limitFPS(15);
            continue;
        }

        drawBuff(*pixelBuff);
        limitFPS(15);
    }

    delete pixelBuff;
    deinitializeTerminal();
    return 0;
}
