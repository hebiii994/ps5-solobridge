// usbprobe: elenca i dispositivi USB visti dalla PS5 tramite /dev/ugen*
// e analizza i descrittori delle interfacce audio (UAC1 / UAC2).
// Solo lettura: non cambia configurazione e non fa trasferimenti.

#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>

#define REPORT_PATH "/data/usbprobe.txt"

typedef struct {
  char useless1[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);

static FILE *g_report;

static void out(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  if (g_report) {
    va_start(ap, fmt);
    vfprintf(g_report, fmt, ap);
    va_end(ap);
  }
}

static void notify(const char *msg) {
  notify_request_t req = {0};
  snprintf(req.message, sizeof(req.message), "%s", msg);
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static const char *speed_name(uint8_t s) {
  switch (s) {
  case USB_SPEED_LOW:   return "low (1.5M)";
  case USB_SPEED_FULL:  return "full (12M)";
  case USB_SPEED_HIGH:  return "high (480M)";
  case USB_SPEED_SUPER: return "super (5G)";
  default:              return "?";
  }
}

static const char *ep_type_name(uint8_t attr) {
  static const char *names[] = {"control", "isoc", "bulk", "interrupt"};
  return names[attr & 3];
}

// Analizza il descrittore di configurazione completo.
// Restituisce la versione UAC trovata (0x100, 0x200) o 0 se nessuna.
static int parse_config(const uint8_t *buf, int len) {
  int uac = 0;
  int cur_class = -1, cur_sub = -1;

  for (int i = 0; i + 2 <= len;) {
    uint8_t blen = buf[i], type = buf[i + 1];
    if (blen < 2 || i + blen > len) {
      out("    ! descrittore troncato a offset %d\n", i);
      break;
    }
    const uint8_t *d = buf + i;

    switch (type) {
    case UDESC_CONFIG:
      out("  CONFIG value=%u interfaces=%u maxpower=%umA\n",
          d[5], d[4], d[8] * 2);
      break;
    case 0x0B: // Interface Association
      out("  IAD first_if=%u count=%u class=%02x sub=%02x proto=%02x\n",
          d[2], d[3], d[4], d[5], d[6]);
      break;
    case UDESC_INTERFACE:
      cur_class = d[5];
      cur_sub = d[6];
      out("  INTERFACE #%u alt=%u eps=%u class=%02x sub=%02x proto=%02x%s\n",
          d[2], d[3], d[4], d[5], d[6], d[7],
          d[5] == 1 ? (d[7] == 0x20 ? "  [AUDIO UAC2]" : "  [AUDIO UAC1]") :
          d[5] == 0xFF ? "  [vendor]" : "");
      break;
    case UDESC_ENDPOINT: {
      uint16_t mps = d[4] | (d[5] << 8);
      out("    EP 0x%02x %-9s %s maxpkt=%u interval=%u%s\n",
          d[2], ep_type_name(d[3]), (d[2] & 0x80) ? "IN " : "OUT",
          mps & 0x7ff, d[6],
          (d[3] & 3) == 1 ? ((d[3] >> 2) & 3) == 1 ? " async" :
                            ((d[3] >> 2) & 3) == 2 ? " adaptive" :
                            ((d[3] >> 2) & 3) == 3 ? " sync" : " nosync" : "");
      break;
    }
    case 0x24: // CS_INTERFACE
      if (cur_class == 1 && cur_sub == 1 && blen >= 5 && d[2] == 0x01) {
        uac = d[3] | (d[4] << 8);
        out("    AC HEADER bcdADC=%x.%02x\n", uac >> 8, uac & 0xff);
      } else if (cur_class == 1 && cur_sub == 1 && d[2] == 0x0A && blen >= 6) {
        out("    CLOCK SOURCE id=%u attrs=%02x\n", d[3], d[4]);
      } else if (cur_class == 1 && cur_sub == 2 && d[2] == 0x02 && blen >= 6) {
        // FORMAT_TYPE: UAC1 e UAC2 hanno layout diversi
        if (uac >= 0x200)
          out("    FORMAT type=%u subslot=%u bits=%u\n", d[3], d[4], d[5]);
        else if (blen >= 8)
          out("    FORMAT type=%u ch=%u subframe=%u bits=%u rates=%u\n",
              d[3], d[4], d[5], d[6], d[7]);
      }
      break;
    default:
      break;
    }
    i += blen;
  }
  return uac;
}

static void probe(const char *path) {
  int fd = open(path, O_RDWR);
  if (fd < 0)
    fd = open(path, O_RDONLY);
  if (fd < 0) {
    out("%s: open fallita (errno %d)\n", path, errno);
    return;
  }

  struct usb_device_info di;
  memset(&di, 0, sizeof(di));
  if (ioctl(fd, USB_GET_DEVICEINFO, &di) == 0) {
    out("\n== %s  %04x:%04x  \"%s\" / \"%s\"\n", path,
        di.udi_vendorNo, di.udi_productNo, di.udi_vendor, di.udi_product);
    out("  bus=%u addr=%u speed=%s class=%02x power=%umA config=%u\n",
        di.udi_bus, di.udi_addr, speed_name(di.udi_speed),
        di.udi_class, di.udi_power, di.udi_config_no);
  } else {
    out("\n== %s  USB_GET_DEVICEINFO fallita (errno %d)\n", path, errno);
  }

  struct usb_device_descriptor dd;
  memset(&dd, 0, sizeof(dd));
  if (ioctl(fd, USB_GET_DEVICE_DESC, &dd) == 0) {
    out("  DEVICE bcdUSB=%x.%02x class=%02x/%02x/%02x vid=%04x pid=%04x configs=%u\n",
        UGETW(dd.bcdUSB) >> 8, UGETW(dd.bcdUSB) & 0xff,
        dd.bDeviceClass, dd.bDeviceSubClass, dd.bDeviceProtocol,
        UGETW(dd.idVendor), UGETW(dd.idProduct), dd.bNumConfigurations);
  } else {
    out("  USB_GET_DEVICE_DESC fallita (errno %d)\n", errno);
  }

  static uint8_t buf[4096];
  struct usb_gen_descriptor gd;
  memset(&gd, 0, sizeof(gd));
  gd.ugd_data = buf;
  gd.ugd_maxlen = sizeof(buf);
  gd.ugd_config_index = 0;
  if (ioctl(fd, USB_GET_FULL_DESC, &gd) == 0) {
    int len = gd.ugd_actlen ? gd.ugd_actlen : (buf[2] | (buf[3] << 8));
    out("  config descriptor: %d byte\n", len);
    int uac = parse_config(buf, len);
    if (uac) {
      char msg[160];
      snprintf(msg, sizeof(msg), "usbprobe: %s %04x:%04x audio UAC%d (bcdADC %x.%02x)",
               path, UGETW(dd.idVendor), UGETW(dd.idProduct),
               uac >= 0x200 ? 2 : 1, uac >> 8, uac & 0xff);
      notify(msg);
    }
  } else {
    out("  USB_GET_FULL_DESC fallita (errno %d)\n", errno);
  }

  close(fd);
}

int main(void) {
  g_report = fopen(REPORT_PATH, "w");
  out("usbprobe: pid=%d uid=%d\n", getpid(), getuid());

  int found = 0;
  char path[32];
  for (int bus = 0; bus < 8; bus++) {
    for (int addr = 1; addr < 16; addr++) {
      snprintf(path, sizeof(path), "/dev/ugen%d.%d", bus, addr);
      if (access(path, F_OK) != 0)
        continue;
      found++;
      probe(path);
    }
  }

  out("\nTotale dispositivi: %d. Report in %s\n", found, REPORT_PATH);
  if (g_report)
    fclose(g_report);

  char msg[96];
  snprintf(msg, sizeof(msg), "usbprobe: %d dispositivi USB, report in %s", found, REPORT_PATH);
  notify(msg);
  return 0;
}
