PS5_HOST ?= 192.168.88.2
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := installer.elf

CFLAGS := -Wall -Werror -O2

all: $(ELF)

$(ELF): main.c ../solobridge/solobridge.elf
	$(CC) $(CFLAGS) -o $@ main.c

clean:
	rm -f $(ELF)
