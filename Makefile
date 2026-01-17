# ACL Manager for WebOS
# Makefile for cross-compilation
#
# IMPORTANT: Uses Linaro GCC 4.9.4 to produce binaries compatible with
# TouchPad's glibc 2.8. System arm-linux-gnueabi-gcc (GCC 13+) links against
# glibc 2.15+ which is NOT available on device.

# Linaro toolchain path (required for glibc compatibility)
LINARO_PATH = /opt/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabi/bin

# Check if Linaro toolchain exists, fall back to system toolchain with warning
ifneq ($(wildcard $(LINARO_PATH)/arm-linux-gnueabi-gcc),)
  CC = $(LINARO_PATH)/arm-linux-gnueabi-gcc
else
  $(warning Linaro toolchain not found at $(LINARO_PATH))
  $(warning Build may not run on TouchPad due to glibc version mismatch!)
  CC = arm-linux-gnueabi-gcc
endif

# PalmPDK paths
PDK_PATH = /opt/PalmPDK

CFLAGS = -Wall -O2 -std=gnu99 \
	-march=armv7-a -mfpu=neon -mfloat-abi=softfp \
	-I$(PDK_PATH)/include \
	-I$(PDK_PATH)/include/SDL

LDFLAGS = -L$(PDK_PATH)/device/lib \
	-Wl,--allow-shlib-undefined \
	-lSDL -lpdl

TARGET = acl-manager
SRC = acl-manager.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
