#include "solo.h"

#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>

#define SOLO_VID 0x1235
#define SOLO_PID 0x8211

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
        return fd;
      }
      close(fd);
    }
  }
  return -1;
}

static int parse_solo(solo_t *s) {
  static uint8_t buf[4096];
  struct usb_gen_descriptor gd;
  memset(&gd, 0, sizeof(gd));
  gd.ugd_data = buf;
  gd.ugd_maxlen = sizeof(buf);
  if (ioctl(s->ctl_fd, USB_GET_FULL_DESC, &gd) != 0)
    return -1;
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
        alt_channels = d[10];
      else if (cur_class == 1 && cur_sub == 2 && d[2] == 0x02 && d[0] >= 6) {
        alt_subslot = d[4];
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
  return (s->clock_id < 0 || s->as_iface < 0 || s->channels <= 0 ||
          s->subslot <= 0) ? -1 : 0;
}

static int set_alt(solo_t *s, int alt) {
  struct usb_alt_interface ai = { .uai_interface_index = s->as_iface,
                                  .uai_alt_index = alt };
  return ioctl(s->ctl_fd, USB_SET_ALTINTERFACE, &ai);
}

// Richiesta UAC2 CUR sul controllo frequenza del clock (scrittura o lettura).
static int clock_rate(solo_t *s, int write, uint32_t *rate) {
  uint8_t data[4] = { *rate, *rate >> 8, *rate >> 16, *rate >> 24 };
  struct usb_ctl_request req;
  memset(&req, 0, sizeof(req));
  req.ucr_data = data;
  req.ucr_request.bmRequestType = (write ? UT_WRITE : UT_READ) | UT_CLASS | UT_INTERFACE;
  req.ucr_request.bRequest = 0x01;                          // CUR
  USETW(req.ucr_request.wValue, 0x01 << 8);                 // CS_SAM_FREQ_CONTROL
  USETW(req.ucr_request.wIndex, (s->clock_id << 8) | s->ac_iface);
  USETW(req.ucr_request.wLength, 4);
  if (ioctl(s->ctl_fd, USB_DO_REQUEST, &req) != 0)
    return -1;
  if (!write)
    *rate = data[0] | (data[1] << 8) | (data[2] << 16) | ((uint32_t)data[3] << 24);
  return 0;
}

int solo_open(solo_t *s, int verbose) {
  memset(s, 0, sizeof(*s));
  s->ep_fd = -1;
  s->ctl_fd = find_solo(s);
  if (s->ctl_fd < 0)
    return -1;

  const char *what = "descrittori";
  if (parse_solo(s) != 0)
    goto fail;
  what = "alt setting";
  if (set_alt(s, s->as_alt) != 0)
    goto fail;
  what = "sample rate";
  uint32_t rate = SOLO_RATE;
  if (clock_rate(s, 1, &rate) != 0)
    goto fail;
  s->rate = 0;
  clock_rate(s, 0, &s->rate);

  char path[32];
  snprintf(path, sizeof(path), "/dev/usb/%d.%d.%d", s->bus, s->addr, s->ep & 0x0f);
  what = path;
  s->ep_fd = open(path, O_WRONLY | O_NONBLOCK);
  if (s->ep_fd < 0)
    goto fail;

  if (verbose)
    printf("solo: ugen%d.%d, %d canali, %d bit in %d byte, ep 0x%02x, %u Hz\n",
           s->bus, s->addr, s->channels, s->bits, s->subslot, s->ep, s->rate);
  return 0;

fail:
  printf("solo: errore su %s (errno %d)\n", what, errno);
  solo_close(s);
  return -1;
}

int solo_write(solo_t *s, const void *pkt, size_t len) {
  ssize_t w = write(s->ep_fd, pkt, len);
  if (w == (ssize_t)len)
    return 1;
  if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    return 0;
  return -1;
}

void solo_close(solo_t *s) {
  if (s->ep_fd >= 0) {
    close(s->ep_fd);
    s->ep_fd = -1;
  }
  if (s->ctl_fd >= 0) {
    if (s->as_iface >= 0)
      set_alt(s, 0);
    close(s->ctl_fd);
    s->ctl_fd = -1;
  }
}
