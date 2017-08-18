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

#include <string>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <curl/curl.h>

#ifndef _WIN32
#include <unistd.h>
#define Sleep(ms) usleep(ms * 1000)
#else
#include <windows.h>
#endif

// safe strncpy - http://stackoverflow.com/q/869883
#define strncpy(dst, src, len) snprintf(dst, len, "%s", src)

namespace {
#include "base64.c"
}

#include "zte_mf283plus_watch.h"

namespace zte_mf283plus_watch {

int Info::getNetworkTypeAsInt() const {
  if (!strcmp(NetworkType, "LTE"))
    return 4;
  else if (!strcmp(NetworkType, "GSM") || !strcmp(NetworkType, "GPRS") ||
           !strcmp(NetworkType, "EDGE") || !strcmp(NetworkType, "E-EDGE"))
    return 2;
  else if (!strcmp(NetworkType, "No Service") ||
           !strcmp(NetworkType, "Limited Service"))
    return 0;
  else
    return 3;
}

void Info::reset() {
  LastUpdate = 0;
  NetworkType[0] = '\0';
  ProviderDesc[0] = '\0';
  LAC = 0;
  RSRP = RSCP = RSRQ = RSSI = 0;
  SINR = ECIO = 0.f;
  CSQ = 0.f;
  GlobalCellID = 0;
  Frequency = Channel = 0;
  MCCMNC = 0;
  GotNetworkType = GotProviderInfo = GotSignalStrength = GotCSQ = GotLAC = false;
  GotCellID = GotFreqency = GotChannel = false;
  N = 0;
}

Info::Info() { reset(); }

namespace {

std::string routerIP;
std::string routerPW;
int updateInterval;
std::thread *updateThreadHandle;
std::atomic_bool deinitRequest;

Info info;
std::mutex mutex;

bool httpRequest(const char *request, std::string &buf, const char *POSTData = nullptr);

int login() {
  std::string data;

  if (!httpRequest("/goform/goform_set_cmd_process", data,
                   (std::string("isTest=false&goformId=LOGIN&password=") + routerPW).c_str()))
    return -1;

  if (data.empty() || data.length() >= 20 || data[0] != '{')
    return -2;

  return data == R"({"result":"0"})" ? 1 : -3;
}

bool httpRequest(const char *request, std::string &buf, const char *POSTData) {
  CURL *curl = curl_easy_init();

  if (!curl)
    abort();

  std::string req = "http://" + routerIP + request;
  std::string ref = "http://" + routerIP + "/index.html";

  auto callback = [](void *data, size_t size, size_t nmemb, std::string &buf) {
    buf.append((const char *)data, size * nmemb);
    return size * nmemb;
  };

  buf.clear();

#define SET_CURL_OPT(OPT, VAL)                                               \
  if (curl_easy_setopt(curl, OPT, VAL) != CURLE_OK)                          \
    abort();

  SET_CURL_OPT(CURLOPT_CONNECTTIMEOUT, 30L);
  SET_CURL_OPT(CURLOPT_TIMEOUT, 30L);
  SET_CURL_OPT(CURLOPT_URL, req.c_str());

  SET_CURL_OPT(CURLOPT_REFERER, ref.c_str());

  if (POSTData)
    SET_CURL_OPT(CURLOPT_POSTFIELDS, POSTData);

  SET_CURL_OPT(CURLOPT_WRITEFUNCTION, +callback);
  SET_CURL_OPT(CURLOPT_WRITEDATA, &buf);
  SET_CURL_OPT(CURLOPT_NOSIGNAL, 1L);

  bool res = curl_easy_perform(curl) == CURLE_OK;

  curl_easy_cleanup(curl);

  return res;
}

template <typename BUF, size_t N>
bool getLine(const char *&str, BUF (&buf)[N]) {
  static_assert(N > 1, "");

  const char *start = str;
  while (*str && *str++ != '\n');

  if (str == start)
    return false;

  size_t length = std::min<size_t>(N - 1, str - start - 1);
  memcpy(buf, start, length);
  buf[length] = '\0';

  return true;
}

const size_t NPOS = size_t(-1);

size_t find(const char *s, const char *f) {
  const char *p = strstr(s, f);
  return p ? p - s : NPOS;
}

void parseMessages(const std::string &messages, Info &info) {
  const char *m = messages.c_str();
  char line[4096];

  int PrevNetworkType = -1;

  if (info.GotNetworkType)
    PrevNetworkType = info.getNetworkTypeAsInt();

  while (getLine(m, line)) {
    size_t pos;

    if ((pos = find(line, " ProcAtZrssiRes ")) != NPOS) {
      const char *s = line + pos + strlen(" ProcAtZrssiRes ");

      if (sscanf(s, "network_type = %63[^,], ", info.NetworkType) == 1)
        info.GotNetworkType = true;
    } else if ((pos = find(line, " +ZRSSI: ")) != NPOS) {
      const char *s = line + pos + strlen(" +ZRSSI: ");
      info.RSRP = info.RSCP = info.RSRQ = info.RSSI = 0xffff;
      info.SINR = info.ECIO = -NAN;

      int N = sscanf(s, "%d,%d,%d,%f", &info.RSRP, &info.RSRQ, &info.RSSI, &info.SINR);

      if (N >= 1) {
        if (N == 1) { // 2G
          std::swap(info.RSRP, info.RSSI);
        } else if (N == 2) { // 3G
          N = (sscanf(s, "%d,%f", &info.RSCP, &info.ECIO) == 2);
        } else { // 4G
#if 0
          if (info.RSSI < -113)
            info.CSQ = 0.f;
          else if (info.RSSI >= -51)
            info.CSQ = 31.f;
          else
            info.CSQ = (113 - (info.RSSI * -1)) / 2.f;
#endif
        }

        info.GotSignalStrength = (N > 0);
      }
    } else if ((pos = find(line, " +CSQ: ")) != NPOS) {
      for (char &c : line) if (c == ',') c = '.';
      const char *s = line + pos + strlen(" +CSQ: ");

      if (sscanf(s, "%f", &info.CSQ) == 1)
        info.GotCSQ = true;
    } else if ((pos = find(line, "LAC=")) != NPOS) {
      const char *s = line + pos;

      if (sscanf(s, "LAC=%x", &info.LAC) == 1)
       info.GotLAC = true;

      if ((pos = find(line, "CELL_ID=")) != NPOS) {
        const char *s = line + pos;

        if (sscanf(s, "CELL_ID=%x", &info.GlobalCellID) == 1)
         info.GotCellID = true;
      }
    } else if ((pos = find(line, " +ZDON: ")) != NPOS) {
      const char *s = line + pos + strlen(" +ZDON: ");

      if (*s++ == '"') {
        const char *p = strchr(s, '"');

        if (p) {
          while (*s == ' ')
            ++s;

          size_t len = p - s;

          if (len >= sizeof(info.ProviderDesc))
            len = sizeof(info.ProviderDesc) - 1;

          memcpy(info.ProviderDesc, s, len);
          info.ProviderDesc[len] = '\0';

          if (*++p == ',') {
            int MCC, MNC;

            if(sscanf(p, ",%d,%d", &MCC, &MNC) == 2) {
              char tmp[64];
              snprintf(tmp, sizeof(tmp), "%d%02d", MCC, MNC);
              info.MCCMNC = atoi(tmp);
              info.GotProviderInfo = true;
            }
          }
        }
      }
    } else if ((pos = find(line, " +ZCELLINFO: ")) != NPOS) {
      const char *s = line + pos + strlen(" +ZCELLINFO: ");
      char band[64];

      // 0: Global Cell ID, 1: Physical Cell ID, 2: Band, 3: Channel

      if (sscanf(s, "%*d, %*d, LTE %63[^,], %d", band, &info.Channel) == 2) {
        if (!strcmp(band, "B3"))
          info.Frequency = 1800;
        else if (!strcmp(band, "B7"))
          info.Frequency = 2600;
        else if (!strcmp(band, "B20"))
          info.Frequency = 800;
        else
          info.Frequency = -1;

        info.GotFreqency = true;
        info.GotChannel = true;
      } else if (sscanf(s, "%*d, %*d, %*s %d", &info.Frequency) >= 1) {
        info.GotFreqency = true;
      }
    }
  }

  if (PrevNetworkType != -1 && info.GotNetworkType &&
      PrevNetworkType != info.getNetworkTypeAsInt()) {
    info.reset(); // Force clean values after net switch
    return;
  }

  info.LastUpdate = time(nullptr);
  info.N++;
}

void updateThread() {
  std::string data;

  do {
    if (httpRequest("/goform/goform_set_cmd_process",
                    data,
                    "isTest=false&goformId=SYSLOG&syslog_flag=open&syslog_mode=wan_connect") &&
        httpRequest("/messages",
                    data)) {
      if (data.length() > 0 && data[0] == '<') {
        login();
      } else {
        mutex.lock();
        parseMessages(data, info);
        mutex.unlock();
      }
    }

    Sleep(updateInterval);
  } while (!deinitRequest);
}

} // unnamed namespace

InitCode init(const char *routerIP, const char *routerPW, int updateInterval) {
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    abort();

  char routerPWBase64[1024];

  base64_encode(strlen(routerPW), (const unsigned char*)routerPW,
                sizeof(routerPWBase64), routerPWBase64);

  ::zte_mf283plus_watch::routerIP = routerIP;
  ::zte_mf283plus_watch::routerPW = routerPWBase64;
  ::zte_mf283plus_watch::updateInterval = updateInterval;

  int rc = login();

  if (rc != 1)
    curl_global_cleanup();

  switch (rc) {
    case -1: return INIT_ERR_HTTP_REQUEST_FAILED;
    case -2: return INIT_ERR_NOT_A_ZTE_MF283P;
    case -3: return INIT_ERR_WRONG_PASSWORD;
  }

  info.reset();

  updateThreadHandle = new std::thread(updateThread);
  return INIT_OK;
}

void deinit() {
  if (!updateThreadHandle)
    return;

  deinitRequest = true;

  updateThreadHandle->join();
  delete updateThreadHandle;
  updateThreadHandle = nullptr;
  curl_global_cleanup();

  deinitRequest = false;
}

bool getInfo(Info &info) {
  mutex.lock();
  if (!::zte_mf283plus_watch::info.N) {
    mutex.unlock();
    return false;
  }
  info = ::zte_mf283plus_watch::info;
  mutex.unlock();
  return true;
}

bool fakeGetInfo(Info &info) {
  time_t now = time(nullptr);
  static time_t lastNetSwitch = 0;
  srand(rand() + now);
  info.CSQ = (rand() % 31) + .99f;
  info.GotCSQ = true;
  info.Channel = 1814;
  info.GotChannel = true;
  info.GlobalCellID = 0xFCFCFC;
  info.GotCellID = true;
  info.LAC = 0xFF;
  info.GotLAC = true;
  info.RSCP = -80 + (rand() % 20);
  info.ECIO = -13 + (rand() % 10);
  info.RSRP = -80 + (rand() % 20);
  info.RSRQ = -14 + (rand() % 10);
  info.SINR = 30.0f - (rand() % 20);
  info.RSSI = -80 + (rand() % 20);
  info.GotSignalStrength = true;
  info.Frequency = 1800;
  info.GotFreqency = true;
  if (now - lastNetSwitch >= 10) {
    switch (rand() % 3) {
      case 0: strncpy(info.NetworkType, "LTE", sizeof(info.NetworkType)); break;
      case 1: strncpy(info.NetworkType, "EDGE", sizeof(info.NetworkType)); break;
      case 2: strncpy(info.NetworkType, "DC-HSPA+", sizeof(info.NetworkType)); break;
    }
    lastNetSwitch = now;
  }
  info.GotNetworkType = true;
  strncpy(info.ProviderDesc, "3 AT", sizeof(info.ProviderDesc));
  info.MCCMNC = 23205;
  info.GotProviderInfo = true;
  info.LastUpdate = now;
  info.N++;
  return true;
}

} // namespace zte_mf283plus_watch


// C Interface

extern "C" {

zte_mf283plus_initcode zte_mf283plus_watch_init(const char *router_ip, const char *router_pw, int update_interval) {
  return zte_mf283plus_watch::init(router_ip, router_pw, update_interval);
}
void zte_mf283plus_watch_deinit() {
  zte_mf283plus_watch::deinit();
}

zte_mf283plus_info *zte_mf283plus_watch_new_info() {
  return new zte_mf283plus_info;
}
void zte_mf283plus_watch_free_info(zte_mf283plus_info *info) {
  delete info;
}

int zte_mf283plus_watch_get_info(zte_mf283plus_info *info) {
  return zte_mf283plus_watch::getInfo(*(zte_mf283plus_watch::Info*)info);
}
int zte_mf283plus_watch_fake_get_info(zte_mf283plus_info *info) {
  return zte_mf283plus_watch::fakeGetInfo(*(zte_mf283plus_watch::Info*)info);
}
int zte_mf283plus_watch_get_networktype_as_int(zte_mf283plus_info *info) {
  return info->getNetworkTypeAsInt();
}

} // extern C
