#!/bin/sh

#mv luajit/src/libluajit.a sys/linux/

g++ -g -std=c++0x \
  -Wextra -Wno-unused-function -Wno-unused-value -Wno-parentheses \
  -I../thirdpart/luajit/include -I../thirdpart/mysql/inc -I../thirdpart/openssl \
  -DSERVER *.cpp *.c \
  ../Library/libnavpathd.a ../Library/libluajit.a ../Library/zlib.a ../Library/libxls-linux.a ../Library/libcrypto.a ../Library/libmysqlclient-linux.a \
  -static-libgcc -m32 -msse -pthread -o qserver-gdb \
  -Wl,-Bstatic -lstdc++ -Wl,-Bstatic -lz -Wl,-Bstatic -lm -Wl,-Bstatic -ldl
