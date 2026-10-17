PS5_HOST ?= 192.168.88.2
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := solobridge.elf

CFLAGS := -Wall -Werror -O2
LDADD  := -lkernel_sys

SRCS := main.c capture.c solo.c

all: $(ELF)

$(ELF): $(SRCS) capture.h solo.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDADD)

clean:
	rm -f $(ELF)

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<
