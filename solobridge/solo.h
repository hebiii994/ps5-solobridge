// Uscita audio diretta sulla Focusrite Scarlett Solo (UAC2) via /dev/ugen*.
#ifndef SOLO_H
#define SOLO_H

#include <stddef.h>
#include <stdint.h>

#define SOLO_RATE 48000

typedef struct {
  int ctl_fd;         // /dev/ugenB.A
  int ep_fd;          // /dev/usb/B.A.EP
  int bus, addr;
  int clock_id, ac_iface, as_iface, as_alt;
  int channels, subslot, bits;
  uint8_t ep;
  uint16_t maxpkt;
  uint32_t rate;      // sample rate riletto dalla Solo dopo l'impostazione
} solo_t;

// Cerca la Solo, legge i descrittori, attiva lo streaming a SOLO_RATE.
// 0 = pronta, -1 = non collegata o errore (s chiuso).
int  solo_open(solo_t *s, int verbose);

// Scrive un pacchetto (un microframe). 1 = scritto, 0 = FIFO pieno, -1 = errore
// (es. Solo scollegata).
int  solo_write(solo_t *s, const void *pkt, size_t len);

// Ferma lo streaming (alt 0) e chiude tutto.
void solo_close(solo_t *s);

#endif
