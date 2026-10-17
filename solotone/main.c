// solotone: riproduce un tono di prova sulla Focusrite Scarlett Solo (UAC2)
// collegata alla PS5, parlando direttamente con /dev/ugen* senza il sistema
// audio di Sony.
//
// Passi: trova la Solo -> legge clock/endpoint/canali dai descrittori ->
// alt setting 1 sull'interfaccia di streaming -> SET_CUR sample rate ->
// scrive pacchetti isocroni (uno per microframe da 125us) -> alt setting 0.

#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>

#define SOLO_VID 0x1235
#define SOLO_PID 0x8211

#define RATE        48000
#define TONE_HZ     440.0
#define AMPLITUDE   0.06    // circa -24 dBFS: volume basso di proposito
#define SECONDS     3
#define FADE_MS     50
#define UFRAMES_SEC 8000    // high-speed, bInterval=1 -> 8000 pacchetti/s

typedef struct {
  char useless1[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);

static void notify(const char *msg) {
  notify_request_t req = {0};
  snprintf(req.message, sizeof(req.message), "%s", msg);
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static void log_msg(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  fflush(stdout);
}

typedef struct {
  int bus, addr;
  int clock_id;       // entita' CLOCK SOURCE
  int ac_iface;       // interfaccia Audio Control
  int as_iface;       // interfaccia di streaming con endpoint OUT
  int as_alt;
  int channels;
  int subslot;
  int bits;
  uint8_t ep;         // indirizzo endpoint OUT
  uint16_t maxpkt;
} solo_t;

static double now_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

// Cerca la Solo tra i /dev/ugen*; restituisce l'fd di controllo aperto.
static int find_solo(solo_t *s) {
  char path[32];
  for (int bus = 0; bus < 8; bus++) {
    for (int addr = 2; addr < 16; addr++) {
      snprintf(path, sizeof(path), "/dev/ugen%d.%d", bus, addr);
      if (access(path, F_OK) != 0)
        continue;
      int fd = open(path, O_RDWR);
      if (fd < 0)
        continue;
      struct usb_device_descriptor dd;
      if (ioctl(fd, USB_GET_DEVICE_DESC, &dd) == 0 &&
          UGETW(dd.idVendor) == SOLO_VID && UGETW(dd.idProduct) == SOLO_PID) {
        s->bus = bus;
        s->addr = addr;
        log_msg("Solo trovata su %s\n", path);
        return fd;
      }
      close(fd);
    }
  }
  return -1;
}

// Estrae dai descrittori i parametri necessari allo streaming in uscita.
static int parse_solo(int fd, solo_t *s) {
  static uint8_t buf[4096];
  struct usb_gen_descriptor gd;
  memset(&gd, 0, sizeof(gd));
  gd.ugd_data = buf;
  gd.ugd_maxlen = sizeof(buf);
  if (ioctl(fd, USB_GET_FULL_DESC, &gd) != 0) {
    log_msg("USB_GET_FULL_DESC fallita (errno %d)\n", errno);
    return -1;
  }
  int len = gd.ugd_actlen ? gd.ugd_actlen : (buf[2] | (buf[3] << 8));

  int cur_if = -1, cur_alt = -1, cur_class = -1, cur_sub = -1;
  int alt_channels = 0, alt_subslot = 0, alt_bits = 0;
  s->clock_id = s->ac_iface = s->as_iface = -1;

  for (int i = 0; i + 2 <= len && buf[i] >= 2; i += buf[i]) {
    const uint8_t *d = buf + i;
    switch (d[1]) {
    case UDESC_INTERFACE:
      cur_if = d[2]; cur_alt = d[3]; cur_class = d[5]; cur_sub = d[6];
      alt_channels = alt_subslot = alt_bits = 0;
      if (cur_class == 1 && cur_sub == 1 && s->ac_iface < 0)
        s->ac_iface = cur_if;
      break;
    case 0x24: // CS_INTERFACE
      if (cur_class == 1 && cur_sub == 1 && d[2] == 0x0A && s->clock_id < 0)
        s->clock_id = d[3];
      else if (cur_class == 1 && cur_sub == 2 && d[2] == 0x01 && d[0] >= 11)
        alt_channels = d[10];             // AS_GENERAL: bNrChannels
      else if (cur_class == 1 && cur_sub == 2 && d[2] == 0x02 && d[0] >= 6) {
        alt_subslot = d[4];               // FORMAT_TYPE I (UAC2)
        alt_bits = d[5];
      }
      break;
    case UDESC_ENDPOINT:
      if (cur_class == 1 && cur_sub == 2 && (d[3] & 3) == 1 &&
          !(d[2] & 0x80) && s->as_iface < 0) {
        s->as_iface = cur_if;
        s->as_alt = cur_alt;
        s->ep = d[2];
        s->maxpkt = (d[4] | (d[5] << 8)) & 0x7ff;
        s->channels = alt_channels;
        s->subslot = alt_subslot;
        s->bits = alt_bits;
      }
      break;
    }
  }

  if (s->clock_id < 0 || s->as_iface < 0 || s->channels <= 0 || s->subslot <= 0) {
    log_msg("Descrittori incompleti: clock=%d as_if=%d ch=%d subslot=%d\n",
            s->clock_id, s->as_iface, s->channels, s->subslot);
    return -1;
  }
  log_msg("AC iface=%d clock id=%d | OUT iface=%d alt=%d ep=0x%02x maxpkt=%u "
          "| %d canali, %d bit in %d byte\n",
          s->ac_iface, s->clock_id, s->as_iface, s->as_alt, s->ep, s->maxpkt,
          s->channels, s->bits, s->subslot);
  return 0;
}

static int set_alt(int fd, int iface, int alt) {
  struct usb_alt_interface ai = { .uai_interface_index = iface, .uai_alt_index = alt };
  if (ioctl(fd, USB_SET_ALTINTERFACE, &ai) != 0) {
    log_msg("USB_SET_ALTINTERFACE if=%d alt=%d fallita (errno %d)\n", iface, alt, errno);
    return -1;
  }
  return 0;
}

// Richiesta di classe UAC2 CUR sul controllo frequenza del clock.
static int clock_rate(int fd, const solo_t *s, int write, uint32_t *rate) {
  uint8_t data[4] = { *rate, *rate >> 8, *rate >> 16, *rate >> 24 };
  struct usb_ctl_request req;
  memset(&req, 0, sizeof(req));
  req.ucr_data = data;
  req.ucr_request.bmRequestType = (write ? UT_WRITE : UT_READ) | UT_CLASS | UT_INTERFACE;
  req.ucr_request.bRequest = 0x01;                          // CUR
  USETW(req.ucr_request.wValue, 0x01 << 8);                 // CS_SAM_FREQ_CONTROL
  USETW(req.ucr_request.wIndex, (s->clock_id << 8) | s->ac_iface);
  USETW(req.ucr_request.wLength, 4);
  if (ioctl(fd, USB_DO_REQUEST, &req) != 0) {
    log_msg("%s sample rate fallita (errno %d)\n", write ? "SET" : "GET", errno);
    return -1;
  }
  if (!write)
    *rate = data[0] | (data[1] << 8) | (data[2] << 16) | ((uint32_t)data[3] << 24);
  return 0;
}

// Apre il nodo dell'endpoint OUT (/dev/usb/BUS.ADDR.EP).
static int open_endpoint(const solo_t *s) {
  char path[32];
  snprintf(path, sizeof(path), "/dev/usb/%d.%d.%d", s->bus, s->addr, s->ep & 0x0f);
  int fd = open(path, O_WRONLY | O_NONBLOCK);
  if (fd < 0)
    log_msg("open %s fallita (errno %d)\n", path, errno);
  else
    log_msg("Endpoint aperto: %s\n", path);
  return fd;
}

// Riempie un pacchetto con 'frames' campioni del tono.
static size_t fill_packet(uint8_t *pkt, const solo_t *s, int frames,
                          uint64_t *n, uint64_t total) {
  const uint64_t fade = (uint64_t)RATE * FADE_MS / 1000;
  size_t off = 0;
  for (int f = 0; f < frames; f++, (*n)++) {
    double env = 1.0;
    if (*n < fade) env = (double)*n / fade;
    else if (*n > total - fade) env = (double)(total - *n) / fade;
    double v = AMPLITUDE * env * sin(2.0 * M_PI * TONE_HZ * (double)*n / RATE);
    // PCM giustificato a sinistra nello slot (MSB), little-endian
    int64_t full = (int64_t)(v * 2147483647.0);
    uint32_t sample = (uint32_t)(int32_t)full;
    if (s->subslot < 4)
      sample >>= (4 - s->subslot) * 8;
    for (int c = 0; c < s->channels; c++)
      for (int b = 0; b < s->subslot; b++)
        pkt[off++] = sample >> (8 * b);
  }
  return off;
}

static int stream_tone(int ep_fd, const solo_t *s) {
  const uint64_t total = (uint64_t)RATE * SECONDS;
  const int base = RATE / UFRAMES_SEC;            // 6 frame per microframe a 48 kHz
  const size_t pkt_bytes = (size_t)base * s->channels * s->subslot;
  if (pkt_bytes > s->maxpkt) {
    log_msg("Pacchetto da %zu byte > maxpkt %u\n", pkt_bytes, s->maxpkt);
    return -1;
  }

  uint8_t pkt[1024];
  uint64_t n = 0;
  long packets = 0, retries = 0;
  double start = now_sec(), deadline = start + SECONDS + 5;

  while (n < total) {
    uint64_t before = n;
    size_t len = fill_packet(pkt, s, base, &n, total);
    for (;;) {
      ssize_t w = write(ep_fd, pkt, len);
      if (w == (ssize_t)len)
        break;
      if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        retries++;
        if (now_sec() > deadline) {
          log_msg("Timeout: il FIFO non si svuota (pacchetti=%ld)\n", packets);
          return -1;
        }
        usleep(500);
        continue;
      }
      log_msg("write fallita dopo %ld pacchetti (ret=%zd errno %d)\n", packets, w, errno);
      n = before;
      return -1;
    }
    packets++;
  }

  double elapsed = now_sec() - start;
  log_msg("Inviati %ld pacchetti da %zu byte in %.2f s (attese FIFO: %ld)\n",
          packets, pkt_bytes, elapsed, retries);
  // Una durata vicina a SECONDS indica che il flusso va davvero a tempo reale.
  return 0;
}

int main(void) {
  solo_t s;
  memset(&s, 0, sizeof(s));
  log_msg("solotone: pid=%d uid=%d\n", getpid(), getuid());

  int fd = find_solo(&s);
  if (fd < 0) {
    log_msg("Scarlett Solo non trovata: e' collegata alla PS5?\n");
    notify("solotone: Scarlett Solo non trovata");
    return 1;
  }

  int rc = 1, ep_fd = -1;
  if (parse_solo(fd, &s) != 0)
    goto out;

  uint32_t rate = 0;
  if (clock_rate(fd, &s, 0, &rate) == 0)
    log_msg("Sample rate attuale: %u Hz\n", rate);

  if (set_alt(fd, s.as_iface, s.as_alt) != 0)
    goto out;
  log_msg("Interfaccia %d impostata su alt %d\n", s.as_iface, s.as_alt);

  rate = RATE;
  if (clock_rate(fd, &s, 1, &rate) != 0)
    goto reset;
  rate = 0;
  if (clock_rate(fd, &s, 0, &rate) == 0)
    log_msg("Sample rate impostato: %u Hz\n", rate);

  ep_fd = open_endpoint(&s);
  if (ep_fd < 0)
    goto reset;

  notify("solotone: tono di prova sulla Scarlett Solo");
  if (stream_tone(ep_fd, &s) == 0)
    rc = 0;

  usleep(100 * 1000);   // lascia svuotare il FIFO
  close(ep_fd);

reset:
  set_alt(fd, s.as_iface, 0);
  log_msg("Interfaccia %d riportata su alt 0\n", s.as_iface);
out:
  close(fd);
  log_msg(rc == 0 ? "OK\n" : "Terminato con errori\n");
  return rc;
}
