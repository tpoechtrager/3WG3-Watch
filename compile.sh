#!/bin/bash
set -xe

[ -z "$CXX" ] && CXX=g++
[ -z "$AR" ] && AR=ar

HOST=$(uname -s)

[ -z "$TARGET" ] && TARGET=$(uname -s)

if [[ "$TARGET" == *NT* ]]; then
  if [ "$HOST" != "$TARGET" ]; then
    HOSTPREFIX=i686-w64-mingw32
  fi
  INCPATHS="-Iwin32/include -Lwin32/lib"
  CXXFLAGS="-DCURL_STATICLIB -fno-pic"
  LDFLAGS="-lws2_32 -lwldap32 -static-libgcc -static-libstdc++"
  EXESUFFIX=".exe"
  DLLSUFFIX=".dll"
elif [ "$TARGET" == "Darwin" ]; then
  INCPATHS=""
  CXXFLAGS="-arch i386 -arch x86_64 -mmacosx-version-min=10.7 -stdlib=libc++"
  LDFLAGS=" -arch i386 -arch x86_64 -mmacosx-version-min=10.7 -stdlib=libc++"
  EXESUFFIX=""
  DLLSUFFIX=".dylib"
else
  EXESUFFIX=""
  DLLSUFFIX=".so"
fi

CXXFLAGS+=" -Wall -Wextra -Wno-unused-function -Wformat -Wformat-security"

if [ "$TARGET" != "Darwin" ]; then
  LDFLAGS+=" -Wl,-s"
fi

if [ -n "$HOSTPREFIX" ]; then
  CXX="$HOSTPREFIX-$CXX"
  AR="$HOSTPREFIX-$AR"
fi

rm -f *.o *.a *.so 3wg3-watch{,.exe} libzte_mf283plus_watch$SUFFIX{.a,.dll,.dylib,.dll}

$CXX zte_mf283plus_watch.cpp -fpic $CXXFLAGS $INCPATHS -std=c++11 -O2 -c
$CXX main.cpp $CXXFLAGS $INCPATHS -std=c++11 -c -O2

$AR rcs  libzte_mf283plus_watch$SUFFIX.a zte_mf283plus_watch.o
$CXX zte_mf283plus_watch.o -shared -pthread $CXXFLAGS $INCPATHS -lcurl $LDFLAGS -o libzte_mf283plus_watch$SUFFIX$DLLSUFFIX
$CXX main.o libzte_mf283plus_watch$SUFFIX.a -pthread $INCPATHS -lcurl $LDFLAGS -o 3wg3-watch$SUFFIX$EXESUFFIX
