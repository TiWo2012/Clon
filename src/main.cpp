//
// Created by TIMO on 20.12.2025.
//

#include <array>
#include <iostream>
#include <print>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

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

int colorToInt(const Color color)
{
    return static_cast<int>(color.r) * 1000000 + static_cast<int>(color.g) * 1000 + static_cast<int>(color.b);
}

void drawPixel(screen& pixelBuff, const int x, const int y, const Color color)
{
    pixelBuff[x][y] = colorToInt(color);
}

void limitFPS(int targetFps)
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


int main()
{
    bool running = true;
    bool terminalIsValid = false;
    int terminalWidth, terminalHeight;

    SetConsoleOutputCP(CP_UTF8);

    // initialize the Pixel buffer
    auto* pixelBuff = new screen();
    drawPixel(*pixelBuff, 0, 0, {255, 0, 0});

    std::cout << "\x1b[31m▄\x1b[0m";

    hideCursor();

    while (running)
    {
        getTerminalSize(terminalWidth, terminalHeight);

        moveCursorHome();
        if (!terminalIsValid)
            {
            if (terminalWidth < 300)
            {
                std::println("Terminal too small (Width: {})", terminalWidth);
                limitFPS(15);
                continue;
            }
            else
                terminalIsValid = true;

            if (terminalHeight < 150)
            {
                std::println("Terminal too small (Height: {})", terminalHeight);
                limitFPS(15);
                continue;
            }
            else
                terminalIsValid = true;
        }

        std::println("Height: {}, Width: {}", terminalHeight, terminalWidth);

        limitFPS(15);
    }
    showCursor();

    delete pixelBuff;

    return 0;
}
