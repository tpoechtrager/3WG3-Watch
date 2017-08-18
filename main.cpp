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

  unknown @ LTEFORUM.AT - December, 2015 / January, 2016
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
bool noClearScreen = getenv("NO_CLEAR_SCREEN");


constexpr float minVal(float) { return -100000.f; }
template<typename T> constexpr T minVal(T) { return std::numeric_limits<T>::min(); }
template<typename T> constexpr T maxVal(T) { return std::numeric_limits<T>::max(); }

template<typename T>
struct MinMaxSum {
  T min;
  T max;
  double sum;
  size_t count;

  float avg() const {
    return count ? sum / count : T();
  }

  void update(const T val) {
    if (val < min)
      min = val;
    if (val > max)
      max = val;
    sum += val;
    count++;
  }

  void reset() {
    min = maxVal(T());
    max = minVal(T());
    sum = 0.0;
    count = 0;
  }

  MinMaxSum() { reset(); }
};


void clearScreen(bool force = false) {
  if (!force && noClearScreen)
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

void getRouterIP(char *routerIP, size_t size) {
#ifdef EXPERIMENTAL
  printf("Getting Router IP...\n");
  struct hostent *h = gethostbyname("ralink.ralinktech.com");

  if (h)
    strncpy(routerIP, inet_ntoa(*((struct in_addr *)h->h_addr)), size);

  clearScreen(true);
#else
  void *h = nullptr;
#endif

  if (!h || !strcmp(routerIP, "127.0.0.1")) {
    printf("Router IP [192.168.0.1]: ");

    if (fgets(routerIP, size, stdin) && routerIP[0] != '\n') {
      routerIP[strlen(routerIP) - 1] = '\0';
    } else {
      strncpy(routerIP, "192.168.0.1", size);
    }
  }
}

void getRouterPassword(char *routerPass, size_t size) {
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
      memcpy(routerPass, tmp, len);
      routerPass[std::min(len, size - 1)] = '\0';
      return;
    }
  }
}

void signalHandler(int) {
  shouldExit = true;
}

} // unnamed namespace

int main(int argc, char **argv) {
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

  char routerIP[64] = "";
  char routerPW[33] = "";
  int updateInterval = 1000;
  bool pipe = false;
  bool testMode = false;
  bool showStats = false;

  for (int i = 1; i < argc; ++i) {
    const char *parameter = argv[i];
    const char *value;

    if (!strcmp(parameter, "--pipe")) {
      pipe = true;
      noClearScreen = true;
      continue;
    } else if (!strcmp(parameter, "--test-mode")) {
      testMode = true;
      continue;
    } else if (!strcmp(parameter, "--stats")) {
      showStats = true;
      continue;
    }

    value = argv[++i];

    if (!value) {
      fprintf(stderr, "Missing value for %s!\n", parameter);
      return 2;
    }

    if (!strcmp(parameter, "--router-ip"))
      strncpy(routerIP, value, sizeof(routerIP));
    else if (!strncmp(parameter, "--router-p", __builtin_strlen("--router-p")))
      strncpy(routerPW, value, sizeof(routerPW));
    else if (!strcmp(parameter, "--update-interval"))
      updateInterval = atoi(value);
  }

  if (updateInterval < 100) {
    fprintf(stderr, "--update-interval must be >= 100!\n");
    return 2;
  }

  if (!routerIP[0])
    getRouterIP(routerIP, sizeof(routerIP));

  getpass:;

  if (!routerPW[0])
    getRouterPassword(routerPW, sizeof(routerPW));

  if (shouldExit)
    return 0;

  if (!testMode) {
    switch (zte_mf283plus_watch::init(routerIP, routerPW, updateInterval)) {
      case zte_mf283plus_watch::INIT_OK:
        break;
      case zte_mf283plus_watch::INIT_ERR_HTTP_REQUEST_FAILED:
        error("HTTP Request failed");
      case zte_mf283plus_watch::INIT_ERR_NOT_A_ZTE_MF283P:
        error("Probably not a ZTE 283MF+ / 3Webgate 3");
      case zte_mf283plus_watch::INIT_ERR_WRONG_PASSWORD:
        fprintf(stderr, "Wrong Password!\n");
        routerPW[0] = '\0';
        goto getpass;
    }
  }

  clearScreen(true);
  if (!pipe)
    printf("Please be patient...");
  fflush(stdout);

  zte_mf283plus_watch::Info info;
  size_t N = size_t(-1);
  const char *fmtStr = "%s%s [%ds]";
  char str[1024] = "";
  char statsStr[1024] = "";
  bool forceClearScreen = true;

  if (showStats)
    fmtStr = pipe ? "%s%s [%ds] | %s" : (noClearScreen ? "%s%s [%ds]\n%s\n" : "%s%s [%ds]\n\n%s\n");

  struct {
    MinMaxSum<decltype(zte_mf283plus_watch::Info::RSRP)> RSRP;
    MinMaxSum<decltype(zte_mf283plus_watch::Info::RSCP)> RSCP;
    MinMaxSum<decltype(zte_mf283plus_watch::Info::RSRQ)> RSRQ;
    MinMaxSum<decltype(zte_mf283plus_watch::Info::RSSI)> RSSI;
    MinMaxSum<decltype(zte_mf283plus_watch::Info::SINR)> SINR;
    MinMaxSum<decltype(zte_mf283plus_watch::Info::ECIO)> ECIO;
    MinMaxSum<decltype(zte_mf283plus_watch::Info::CSQ)>  CSQ;

    int NetworkType = -1;

    void update(const zte_mf283plus_watch::Info &info, const int currentNetworkType) {
      if (NetworkType != currentNetworkType) {
        RSRP.reset(); RSCP.reset(); RSRQ.reset();
        RSSI.reset(); SINR.reset(); ECIO.reset();
        CSQ.reset();
        NetworkType = currentNetworkType;
      }

      RSRP.update(info.RSRP); RSCP.update(info.RSCP); RSRQ.update(info.RSRQ);
      RSSI.update(info.RSSI); SINR.update(info.SINR); ECIO.update(info.ECIO);
      CSQ.update(info.CSQ);
    }
  } stats;

  do {
    if ((testMode ? zte_mf283plus_watch::fakeGetInfo(info) : zte_mf283plus_watch::getInfo(info)) &&
        info.N != N && info.GotNetworkType && info.GotSignalStrength && info.GotCSQ) {
          
      int networkType = info.getNetworkTypeAsInt();
      stats.update(info, networkType);

      auto calculateSignalStrength = [](const float CSQ) {
        return (100.f / 31.99f) * CSQ;
      };

      switch (networkType) {
        case 4:
          snprintf(str, sizeof(str),
                   "[LTE | %s (%d) | %d MHz (%d)] [RSRP: %d, RSRQ: %d, RSSI: %d, SINR: %.1f (%.1f%%)] [CELL ID: %X]",
                   info.GotProviderInfo ? info.ProviderDesc : "??",
                   info.GotProviderInfo ? info.MCCMNC : -1,
                   info.GotFreqency ? info.Frequency : -1,
                   info.GotChannel ? info.Channel : -1,
                   info.RSRP, info.RSRQ, info.RSSI, info.SINR,
                   calculateSignalStrength(info.CSQ),
                   info.GotCellID ? info.GlobalCellID : -1);

          snprintf(statsStr, sizeof(statsStr),
                   "[RSRP: %d/%d/%.1f,  RSRQ: %d/%d/%.1f,  RSSI: %d/%d/%.1f,  SINR: %.1f/%.1f/%.1f  (%.1f%%/%.1f%%/%.1f%%)]",
                   stats.RSRP.max, stats.RSRP.min, stats.RSRP.avg(),
                   stats.RSRQ.max, stats.RSRQ.min, stats.RSRQ.avg(),
                   stats.RSSI.max, stats.RSSI.min, stats.RSSI.avg(),
                   stats.SINR.max, stats.SINR.min, stats.SINR.avg(),
                   calculateSignalStrength(stats.CSQ.max),
                   calculateSignalStrength(stats.CSQ.min),
                   calculateSignalStrength(stats.CSQ.avg()));
          break;
        case 3:
          snprintf(str, sizeof(str),
                   "[%s | %s (%d) | %d MHz] [RSCP: %d, EC/IO: %.1f (%.1f%%)] [CELL ID: %X, LAC: %d]",
                   info.NetworkType,
                   info.GotProviderInfo ? info.ProviderDesc : "??",
                   info.GotProviderInfo ? info.MCCMNC : -1,
                   info.GotFreqency ? info.Frequency : -1,
                   info.RSCP, info.ECIO,
                   calculateSignalStrength(info.CSQ),
                   info.GotCellID ? info.GlobalCellID : -1,
                   info.GotLAC ? info.LAC : -1);

          snprintf(statsStr, sizeof(statsStr),
                   "[RSCP: %d/%d/%.1f,  EC/IO: %.1f/%.1f/%.1f  (%.1f%%/%.1f%%/%.1f%%)]",
                   stats.RSCP.max, stats.RSCP.min, stats.RSCP.avg(),
                   stats.ECIO.max, stats.ECIO.min, stats.ECIO.avg(),
                   calculateSignalStrength(stats.CSQ.max),
                   calculateSignalStrength(stats.CSQ.min),
                   calculateSignalStrength(stats.CSQ.avg()));
          break;
        case 2:
          snprintf(str, sizeof(str),
                   "[%s | %s (%d) | %d MHz] [RSSI: %d (%.1f%%)] [CELL ID: %X, LAC: %d]",
                   info.NetworkType,
                   info.GotProviderInfo ? info.ProviderDesc : "??",
                   info.GotProviderInfo ? info.MCCMNC : -1,
                   info.GotFreqency ? info.Frequency : -1,
                   info.RSSI,
                   calculateSignalStrength(info.CSQ),
                   info.GotCellID ? info.GlobalCellID : -1,
                   info.GotLAC ? info.LAC : -1);

          snprintf(statsStr, sizeof(statsStr),
                   "[RSSI: %d/%d/%.1f  (%.1f%%/%.1f%%/%.1f%%)]",
                   stats.RSSI.max, stats.RSSI.min, stats.RSSI.avg(),
                   calculateSignalStrength(stats.CSQ.max),
                   calculateSignalStrength(stats.CSQ.min),
                   calculateSignalStrength(stats.CSQ.avg()));
          break;
        case 0:
          strcpy(str, "No Service!");
          statsStr[0] = '\0';
      }

      N = info.N;
    }

    if (str[0]) {
      clearScreen(forceClearScreen);
      forceClearScreen = false;
      char timeStr[64] = "";
      if (pipe) {
        time_t t = time(nullptr);
        strftime(timeStr, sizeof(timeStr), "[%Y-%m-%d - %H:%M:%S] | ", localtime(&t));
      }
      printf(fmtStr, timeStr, str, int(time(nullptr) - info.LastUpdate), statsStr);
      if (noClearScreen)
        printf("\n");
      fflush(stdout);
    }

    Sleep(updateInterval < 1000 ? updateInterval : 1000);
  } while (!shouldExit);

  clearScreen();
  zte_mf283plus_watch::deinit();

#if defined(_WIN32) && defined(EXPERIMENTAL)
  WSACleanup();
#endif

  return 0;
}
