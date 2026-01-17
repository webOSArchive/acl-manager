# ACL Manager for WebOS
# Makefile for cross-compilation

CROSS_COMPILE ?= arm-linux-gnueabi-
CC = $(CROSS_COMPILE)gcc

# PalmPDK paths
PDK_PATH = /opt/PalmPDK

CFLAGS = -Wall -O2 \
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
