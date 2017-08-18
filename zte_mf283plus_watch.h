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

#include <time.h>
#include <stdint.h>

#if !defined(__cplusplus) && !defined(bool)
#define bool signed char
#endif

#ifdef __cplusplus
namespace zte_mf283plus_watch {
#endif

struct Info {
  time_t LastUpdate;
  char NetworkType[64];
  char ProviderDesc[64];
  int RSRP;
  int RSCP;
  int RSRQ;
  int RSSI;
  float SINR;
  float ECIO;
  float CSQ;
  int LAC;
  int GlobalCellID;
  int Frequency;
  int Channel;
  int MCCMNC;
  bool GotNetworkType;
  bool GotProviderInfo;
  bool GotSignalStrength;
  bool GotCSQ;
  bool GotLAC;
  bool GotCellID;
  bool GotFreqency;
  bool GotChannel;

  size_t N;

#ifdef __cplusplus
  int getNetworkTypeAsInt() const;
  void reset();
  Info();
#endif
};

enum InitCode {
  INIT_OK,
  INIT_ERR_HTTP_REQUEST_FAILED,
  INIT_ERR_NOT_A_ZTE_MF283P,
  INIT_ERR_WRONG_PASSWORD
};

#ifdef __cplusplus
InitCode init(const char *RouterIP, const char *RouterPW, int UpdateInterval = 1000);
void deinit();
bool getInfo(Info &info);
bool fakeGetInfo(Info &info);
} // namespace zte_mf283plus_watch
#endif

/* C Interface */

#ifdef __cplusplus
extern "C" {
typedef zte_mf283plus_watch::Info zte_mf283plus_info;
typedef zte_mf283plus_watch::InitCode zte_mf283plus_initcode;
#else
typedef struct Info zte_mf283plus_info;
typedef enum InitCode zte_mf283plus_initcode;
#endif

zte_mf283plus_initcode zte_mf283plus_watch_init(const char *router_ip, const char *router_pw, int update_interval);
void zte_mf283plus_watch_deinit();

zte_mf283plus_info *zte_mf283plus_watch_new_info();
void zte_mf283plus_watch_free_info(zte_mf283plus_info *info);

int zte_mf283plus_watch_get_info(zte_mf283plus_info *info);
int zte_mf283plus_watch_fake_get_info(zte_mf283plus_info *info);
int zte_mf283plus_watch_get_networktype_as_int(zte_mf283plus_info *info);

#ifdef __cplusplus
} // extern C
#endif
