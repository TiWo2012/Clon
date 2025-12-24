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

struct Color {
  unsigned char r, g, b;
};
int compactColor(const Color c) { return c.r * 1000000 + c.g * 1000 + c.b; }
inline void unpackColor(int packed, int &r, int &g, int &b) {
  r = packed / 1000000;
  g = (packed / 1000) % 1000;
  b = packed % 1000;
}

enum class KeyType {
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

void enableRawInput() {
  HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode;
  GetConsoleMode(hIn, &mode);
  SetConsoleMode(hIn, mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
}

std::vector<KeyEvent> pollInput() {
  std::vector<KeyEvent> keys;
  HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
  DWORD events = 0;
  if (!GetNumberOfConsoleInputEvents(hIn, &events) || events == 0)
    return keys;

  INPUT_RECORD rec;
  DWORD read;
  for (DWORD i = 0; i < events; ++i) {
    if (!ReadConsoleInput(hIn, &rec, 1, &read))
      break;
    if (rec.EventType == KEY_EVENT) {
      KEY_EVENT_RECORD &key = rec.Event.KeyEvent;
      if (key.bKeyDown) {
        if (key.uChar.AsciiChar)
          keys.emplace_back(key.uChar.AsciiChar);
        else {
          switch (key.wVirtualKeyCode) {
          case VK_UP:
            keys.emplace_back(KeyType::Up);
            break;
          case VK_DOWN:
            keys.emplace_back(KeyType::Down);
            break;
          case VK_LEFT:
            keys.emplace_back(KeyType::Left);
            break;
          case VK_RIGHT:
            keys.emplace_back(KeyType::Right);
            break;
          case VK_RETURN:
            keys.emplace_back(KeyType::Enter);
            break;
          case VK_ESCAPE:
            keys.emplace_back(KeyType::Escape);
            break;
          case VK_BACK:
            keys.emplace_back(KeyType::Backspace);
            break;
          case VK_TAB:
            keys.emplace_back(KeyType::Tab);
            break;
          default:
            keys.emplace_back(KeyType::Unknown);
            break;
          }
        }
      }
    }
  }
  return keys;
}

WORD rgbToWinAttr(const int r, const int g, const int b) {
  WORD attr = 0;
  if (r > 128)
    attr |= FOREGROUND_RED;
  if (g > 128)
    attr |= FOREGROUND_GREEN;
  if (b > 128)
    attr |= FOREGROUND_BLUE;
  if (r > 200 || g > 200 || b > 200)
    attr |= FOREGROUND_INTENSITY;
  return attr;
}

#endif

#ifndef _WIN32
termios origTerm;
std::deque<char> inputBuffer;

void enableRawInput() {
  tcgetattr(STDIN_FILENO, &origTerm);
  termios t = origTerm;
  t.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &t);
  fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
}
void restoreTerminal() { tcsetattr(STDIN_FILENO, TCSANOW, &origTerm); }

std::vector<KeyEvent> pollInput() {
  std::vector<KeyEvent> keys;
  char buf[32];
  ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
  for (ssize_t i = 0; i < n; ++i)
    inputBuffer.push_back(buf[i]);
  while (!inputBuffer.empty()) {
    char c = inputBuffer.front();
    inputBuffer.pop_front();
    if (c == '\x1b') {
      if (inputBuffer.size() >= 2 && inputBuffer[0] == '[') {
        char code = inputBuffer[1];
        inputBuffer.pop_front();
        inputBuffer.pop_front();
        switch (code) {
        case 'A':
          keys.push_back(KeyType::Up);
          break;
        case 'B':
          keys.push_back(KeyType::Down);
          break;
        case 'C':
          keys.push_back(KeyType::Right);
          break;
        case 'D':
          keys.push_back(KeyType::Left);
          break;
        default:
          keys.push_back(KeyType::Unknown);
          break;
        }
      } else {
        inputBuffer.push_front(c);
        break;
      }
    } else if (c == '\r' || c == '\n')
      keys.push_back(KeyType::Enter);
    else if (c == '\b' || c == 127)
      keys.push_back(KeyType::Backspace);
    else if (c == '\t')
      keys.push_back(KeyType::Tab);
    else
      keys.push_back(c);
  }
  return keys;
}
#endif

bool getTerminalSize(int &w, int &h) {
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

void hideCursor() { std::cout << "\x1b[?25l"; }
void showCursor() { std::cout << "\x1b[?25h"; }
void moveCursorHome() { std::cout << "\x1b[H"; }
void limitFPS(const int fps) {
  using clock = std::chrono::steady_clock;
  static auto nextFrame = clock::now();
  nextFrame += std::chrono::nanoseconds(1'000'000'000 / fps);
  std::this_thread::sleep_until(nextFrame);
}
void drawPixel(screen &buff, const int x, const int y, const Color c) {
  if (x < 0 || y < 0 || x >= static_cast<int>(buff[0].size()) || y >= static_cast<int>(buff.size()))
    return;
  buff[y][x] = compactColor(c);
}

void initializeTerminal() {
#ifdef _WIN32
  // nothing
#else
  std::cout << "\x1b[?1049h";
  hideCursor();
#endif
}
void deinitializeTerminal() {
#ifdef _WIN32
  // nothing
#else
  showCursor();
  std::cout << "\x1b[?1049l";
#endif
}

void drawBuff(const screen &pixelBuff) {
#ifdef _WIN32
  static HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo(hConsole, &csbi);
  const int maxW = csbi.srWindow.Right - csbi.srWindow.Left + 1;
  const int maxH = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  const int h = static_cast<int>(pixelBuff.size()) & ~1;
  const int w = static_cast<int>(pixelBuff[0].size());
  const int cellH = min(h / 2, maxH);
  const int cellW = min(w, maxW);
  static std::vector<CHAR_INFO> buf(cellW * cellH);
  for (int y = 0; y < cellH; ++y) {
    for (int x = 0; x < cellW; ++x) {
      int ur, ug, ub, lr, lg, lb;
      unpackColor(pixelBuff[y * 2][x], ur, ug, ub);
      unpackColor(pixelBuff[y * 2 + 1][x], lr, lg, lb);
      auto &[Char, Attributes] = buf[y * cellW + x];
      Char.UnicodeChar = L'\u2584';
      Attributes =
          rgbToWinAttr(lr, lg, lb) | (rgbToWinAttr(ur, ug, ub) << 4);
    }
  }
  const COORD size = {static_cast<SHORT>(cellW), static_cast<SHORT>(cellH)}, zero = {0, 0};
  SMALL_RECT rect = {0, 0, static_cast<SHORT>(cellW - 1), static_cast<SHORT>(cellH - 1)};
  WriteConsoleOutputW(hConsole, buf.data(), size, zero, &rect);
#else
  write(STDOUT_FILENO, "\x1b[H\x1b[J", 6);
  int termW, termH;
  getTerminalSize(termW, termH);
  const size_t cellHeight = std::min(pixelBuff.size() / 2, size_t(termH));
  const size_t cellWidth = std::min(pixelBuff[0].size(), size_t(termW));
  std::string frame;
  frame.reserve(cellWidth * cellHeight * 12);
  auto appendInt = [&](int v) {
    if (v >= 100) {
      frame.push_back('0' + v / 100);
      frame.push_back('0' + (v / 10) % 10);
      frame.push_back('0' + v % 10);
    } else if (v >= 10) {
      frame.push_back('0' + v / 10);
      frame.push_back('0' + v % 10);
    } else {
      frame.push_back('0' + v);
    }
  };
  int lastUR = -1, lastUG = -1, lastUB = -1;
  int lastLR = -1, lastLG = -1, lastLB = -1;
  for (size_t y = 0; y < cellHeight; ++y) {
    const size_t py = y * 2;
    for (size_t x = 0; x < cellWidth; ++x) {
      int ur, ug, ub;
      int lr, lg, lb;
      unpackColor(pixelBuff[py][x], ur, ug, ub);
      unpackColor(pixelBuff[py + 1][x], lr, lg, lb);
      if (ur != lastUR || ug != lastUG || ub != lastUB) {
        frame.append("\x1b[48;2;");
        appendInt(ur);
        frame.push_back(';');
        appendInt(ug);
        frame.push_back(';');
        appendInt(ub);
        frame.push_back('m');
        lastUR = ur;
        lastUG = ug;
        lastUB = ub;
      }
      if (lr != lastLR || lg != lastLG || lb != lastLB) {
        frame.append("\x1b[38;2;");
        appendInt(lr);
        frame.push_back(';');
        appendInt(lg);
        frame.push_back(';');
        appendInt(lb);
        frame.push_back('m');
        lastLR = lr;
        lastLG = lg;
        lastLB = lb;
      }
      frame.append("▄");
    }
    frame.append("\x1b[0m\n");
    lastUR = lastUG = lastUB = -1;
    lastLR = lastLG = lastLB = -1;
  }
  write(STDOUT_FILENO, frame.data(), frame.size());
#endif
}

int main() {
#ifndef _WIN32
  atexit(restoreTerminal);
#endif
  initializeTerminal();
  enableRawInput();

  auto *pixelBuff = new screen{};
  drawPixel(*pixelBuff, 0, 0, {255, 0, 0});
  drawPixel(*pixelBuff, 0, 2, {0, 255, 0});
  drawPixel(*pixelBuff, 2, 0, {0, 0, 255});
  drawPixel(*pixelBuff, 0, 4, {0, 0, 255});

  int termW, termH;

  bool running = true;
  while (running == true) {
    if (getTerminalSize(termW, termH)) {
      if (termW < 300)
        continue;
      if (termH < 150)
        continue;
    }

    for (auto keys = pollInput(); auto &k : keys) {
       if (std::holds_alternative<char>(k))
         std::cout << "Char: " << std::get<char>(k) << "\n";
       else {
         switch (std::get<KeyType>(k)) {
         case KeyType::Up:
           std::cout << "Up\n";
           break;
         case KeyType::Down:
           std::cout << "Down\n";
           break;
         case KeyType::Left:
           std::cout << "Left\n";
           break;
         case KeyType::Right:
           std::cout << "Right\n";
           break;
         case KeyType::Enter:
           std::cout << "Enter\n";
           break;
         case KeyType::Escape:
           std::cout << "Escape\n";
           running = false;
           break;
         case KeyType::Backspace:
           std::cout << "Backspace\n";
           break;
         case KeyType::Tab:
           std::cout << "Tab\n";
           break;
         default:
           std::cout << "Unknown\n";
           break;
         }
       }
     }

    drawBuff(*pixelBuff);
    limitFPS(15);
  }

  delete pixelBuff;
  deinitializeTerminal();
  return 0;
}
