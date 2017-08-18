/*
  This is free and unencumbered software released into the public domain.

  Anyone is free to copy, modify, publish, use, compile, sell, or
  distribute this software, either in source code form or as a compiled
  binary, for any purpose, commercial or non-commercial, and by any
  means.

  In jurisdictions that recognize copyright laws, the author or authors
  of this software dedicate any and all copyright interest in the
  software to the public domain. We make this dedication for the benefit
  of the public at large and to the detriment of our heirs and
  successors. We intend this dedication to be an overt act of
  relinquishment in perpetuity of all present and future rights to this
  software under copyright law.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
  OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
  OTHER DEALINGS IN THE SOFTWARE.

  For more information, please refer to <http://unlicense.org/>

  unknown @ LTEFORUM.AT - December, 2015
*/

#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cassert>
#include <ctime>

#include "zte_mf283plus_watch.h"

#ifdef _WIN32
#include <windows.h>
#define xstr(s) str(s)
#define str(s) #s
#define COLS 130
#define LINES 20
#else
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define Sleep(ms) usleep(ms * 1000)
#endif

// safe strncpy - http://stackoverflow.com/q/869883
#define strncpy(dst, src, len) snprintf(dst, len, "%s", src)

//#define EXPERIMENTAL

namespace {

std::atomic_bool shouldExit;
bool noCLS = getenv("NO_CLEAR_SCREEN");

void clearScreen(bool force = false) {
  if (!force && noCLS)
    return;

#ifdef _WIN32
  DWORD cCharsWritten;
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  DWORD dwConSize;
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  GetConsoleScreenBufferInfo(hConsole, &csbi);
  dwConSize = csbi.dwSize.X * csbi.dwSize.Y;
  FillConsoleOutputCharacter(hConsole, TEXT(' '), dwConSize, {}, &cCharsWritten);
  GetConsoleScreenBufferInfo(hConsole, &csbi);
  FillConsoleOutputAttribute(hConsole, csbi.wAttributes, dwConSize, {}, &cCharsWritten);
  SetConsoleCursorPosition(hConsole, {});
#else
  printf("%s", "\e[1;1H\e[2J");
#endif
}

void error(const char *msg) {
  fprintf(stderr, "Error: %s%s\n", msg, (msg[0] && msg[strlen(msg) - 1] != '?' ? "!" : ""));
#ifdef _WIN32
  getchar();
#endif
  exit(EXIT_FAILURE);
}

void getRouterIP(char *RouterIP, size_t size) {
#ifdef EXPERIMENTAL
  printf("Getting Router IP...\n");
  struct hostent *h = gethostbyname("ralink.ralinktech.com");

  if (h)
    strncpy(RouterIP, inet_ntoa(*((struct in_addr *)h->h_addr)), size);

  clearScreen(true);
#else
  void *h = nullptr;
#endif

  if (!h || !strcmp(RouterIP, "127.0.0.1")) {
    printf("Router IP [192.168.0.1]: ");

    if (fgets(RouterIP, size, stdin) && RouterIP[0] != '\n') {
      RouterIP[strlen(RouterIP) - 1] = '\0';
    } else {
      strncpy(RouterIP, "192.168.0.1", size);
    }
  }
}

void getRouterPassword(char *RouterPass, size_t size) {
  char tmp[16384];

  assert(size > 1);

  while (!shouldExit) {
    printf("Router Password: ");

    if (fgets(tmp, sizeof(tmp), stdin) && tmp[0] != '\n') {
      size_t len = strlen(tmp) - 1;
      if (len > 32 || len > size) {
        fprintf(stderr, "Password too long!\n");
        continue;
      }
      memcpy(RouterPass, tmp, len);
      RouterPass[std::min(len, size - 1)] = '\0';
      return;
    }
  }
}

void signalHandler(int) {
  shouldExit = true;
}

} // unnamed namespace

int main() {
#ifdef _WIN32
#ifdef EXPERIMENTAL
  WSADATA wsaData;
  if (WSAStartup(0x0202, &wsaData))
    error("Initing Winsock failed");
#endif
  system("mode con:cols=" xstr(COLS) " lines=" xstr(LINES));
#endif

  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  char RouterIP[64];
  char RouterPW[64];

  getRouterIP(RouterIP, sizeof(RouterIP));
  getpass:;
  getRouterPassword(RouterPW, sizeof(RouterPW));

  if (shouldExit)
    return 0;

  switch (zte_mf283plus_watch::init(RouterIP, RouterPW)) {
    case zte_mf283plus_watch::INIT_OK:
      break;
    case zte_mf283plus_watch::INIT_ERR_HTTP_REQUEST_FAILED:
      error("HTTP Request failed");
    case zte_mf283plus_watch::INIT_ERR_NOT_A_ZTE_MF283P:
      error("Probably not a ZTE 283MF+ / 3Webgate 3");
    case zte_mf283plus_watch::INIT_ERR_WRONG_PASSWORD:
      fprintf(stderr, "Wrong Password!\n");
      goto getpass;
  }

  clearScreen(true);
  printf("Please be patient...");
  fflush(stdout);

  zte_mf283plus_watch::Info info;
  size_t N = size_t(-1);
  char str[120] = "";
  bool forceCls = true;

  do {
    if (zte_mf283plus_watch::getInfo(info) && info.N != N &&
        info.GotNetworkType && info.GotSignalStrength) {

      switch (info.getNetworkType()) {
        case 4:
          snprintf(str, sizeof(str),
                   "[LTE | %s (%d) | %d MHz (%d)] [RSRP: %d, RSRQ: %d, RSSI: %d, SINR: %.1f (%.1f%%)] [CELL ID: %d]",
                   info.GotProviderInfo ? info.ProviderDesc : "??",
                   info.GotProviderInfo ? info.MCCMNC : -1,
                   info.GotFreqency ? info.Frequency : -1,
                   info.GotChannel ? info.Channel : -1,
                   info.RSRP, info.RSRQ, info.RSSI, info.SINR,
                   info.GotCSQ ? (100.f / 31.99f) * info.CSQ : -1.f,
                   info.GotCellID ? info.GlobalCellID : -1);
          break;
        case 3:
          snprintf(str, sizeof(str),
                   "[%s | %s (%d) | %d MHz] [RSCP: %d, EC/IO: %.1f (%.1f%%)] [CELL ID: %d, LAC: %d]",
                   info.NetworkType,
                   info.GotProviderInfo ? info.ProviderDesc : "??",
                   info.GotProviderInfo ? info.MCCMNC : -1,
                   info.GotFreqency ? info.Frequency : -1,
                   info.RSCP, info.ECIO,
                   info.GotCSQ ? (100.f / 31.99f) * info.CSQ : -1.f,
                   info.GotCellID ? info.GlobalCellID : -1,
                   info.GotLAC ? info.LAC : -1);
          break;
        case 2:
          snprintf(str, sizeof(str),
                   "[%s | %s (%d) | %d MHz] [RSSI: %d (%.1f%%)] [CELL ID: %d, LAC: %d]",
                   info.NetworkType,
                   info.GotProviderInfo ? info.ProviderDesc : "??",
                   info.GotProviderInfo ? info.MCCMNC : -1,
                   info.GotFreqency ? info.Frequency : -1,
                   info.RSSI,
                   info.GotCSQ ? (100.f / 31.99f) * info.CSQ : -1.f,
                   info.GotCellID ? info.GlobalCellID : -1,
                   info.GotLAC ? info.LAC : -1);
          break;
        case 0:
          strcpy(str, "No Service!");
      }

      N = info.N;
    }

    if (str[0]) {
      clearScreen(forceCls);
      forceCls = false;
      printf("%s [%ds]", str, int(time(nullptr) - info.LastUpdate));
      if (noCLS)
        printf("\n");
      fflush(stdout);
    }

    Sleep(1000);
  } while (!shouldExit);

  clearScreen();
  zte_mf283plus_watch::deinit();

#if defined(_WIN32) && defined(EXPERIMENTAL)
  WSACleanup();
#endif

  return 0;
}
