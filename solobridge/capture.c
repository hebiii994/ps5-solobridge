// Basato su src/capture.c di FGG-PlayPods (GPL-3.0):
// https://github.com/FGGstore/FGG-PlayPods
// NID, layout dei parametri e codici di errore vengono da li'.

#include "capture.h"

#include <ps5/kernel.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define LIB_PATH  "/system/common/lib/libSceAvcap2.sprx"
// libSceAvcap2 importa libSceIpmi: se non e' caricata, la prima chiamata IPC
// termina il processo senza segnale.
#define IPMI_PATH "/system/common/lib/libSceIpmi.sprx"

#define KIND_AUDIO 0

#define ERR_EMPTY   0x81950002u   // niente da leggere
#define ERR_OVERRUN 0x81950004u   // il writer ci ha superato; gia' risincronizzato
#define READ_NOWAIT 1u

int sceKernelLoadStartModule(const char *path, size_t argc, const void *argv,
                             uint32_t flags, void *opt, int *res);

// Parametri di sceAvcap2OpenAudio come li compila Remote Play.
// source 0 = mix di registrazione (solo gioco, muto con la UI di sistema);
// source 2 = mix di Remote Play (gioco + suoni di sistema).
// mode 0 = PCM float stereo grezzo.
typedef struct {
  uint64_t size;      // 0x38
  uint64_t unk08;     // 0x18
  uint32_t unk10;
  uint32_t source;
  uint32_t mode;
  uint32_t unk1c;     // 0xffffffff
  uint64_t unk20;
  uint32_t bitrate;
  uint32_t pad;
  uint64_t unk30;
} open_param;

_Static_assert(sizeof(open_param) == 0x38, "dimensione parametri OpenAudio");

typedef int (*init_fn)(void);
typedef int (*open_fn)(void **out, const void *param, uint8_t kind);
typedef int (*handle_fn)(void *h);
typedef int (*read_fn)(void *h, void *buf, size_t size, void *info, uint32_t flags);

static init_fn   p_initialize, p_terminate;
static open_fn   p_open_audio;
static handle_fn p_start, p_stop, p_close;
static read_fn   p_read_audio;

static void *g_handle;
static int g_initialized, g_started;
static long g_overruns;

long capture_overruns(void) {
  return g_overruns;
}

// Gli export del modulo non si trovano per nome, si risolvono per NID.
static void *resolve(int mod, const char *nid, const char *name) {
  intptr_t p = kernel_dynlib_resolve(getpid(), (uint32_t)mod, nid);
  if (!p)
    printf("capture: %s (%s) non trovata\n", name, nid);
  return (void *)p;
}

static int start_capture(void) {
  open_param param;
  const char *what;
  int r;

  what = "Initialize";
  r = p_initialize();
  if (r < 0) goto fail;
  g_initialized = 1;

  what = "OpenAudio";
  memset(&param, 0, sizeof(param));
  param.size = sizeof(param);
  param.unk08 = 0x18;
  param.source = 2;
  param.mode = 0;
  param.unk1c = 0xffffffffu;
  r = p_open_audio(&g_handle, &param, KIND_AUDIO);
  if (r < 0 || !g_handle) {
    g_handle = NULL;
    goto fail;
  }

  what = "Start";
  r = p_start(g_handle);
  if (r < 0) goto fail;

  g_started = 1;
  printf("capture: attiva, %d Hz stereo\n", CAPTURE_RATE);
  return 1;

fail:
  printf("capture: %s -> %#x\n", what, (unsigned)r);
  capture_close();
  return 0;
}

int capture_open(void) {
  int mod, res = 0, r;

  r = sceKernelLoadStartModule(IPMI_PATH, 0, NULL, 0, NULL, &res);
  if (r < 0) {
    printf("capture: load %s -> %#x\n", IPMI_PATH, (unsigned)r);
    return -1;
  }
  mod = sceKernelLoadStartModule(LIB_PATH, 0, NULL, 0, NULL, &res);
  if (mod < 0) {
    printf("capture: load %s -> %#x\n", LIB_PATH, (unsigned)mod);
    return -1;
  }

  p_initialize = (init_fn)resolve(mod, "svzPXluOz8U", "sceAvcap2Initialize");
  p_terminate  = (init_fn)resolve(mod, "gojqghDU+1Y", "sceAvcap2Terminate");
  p_open_audio = (open_fn)resolve(mod, "3nHpE7Dp5SE", "sceAvcap2OpenAudio");
  p_close      = (handle_fn)resolve(mod, "tR7gPe1i8hw", "sceAvcap2Close");
  p_start      = (handle_fn)resolve(mod, "8CGNCwBsItI", "sceAvcap2Start");
  p_stop       = (handle_fn)resolve(mod, "Z5dKK0xnQ8g", "sceAvcap2Stop");
  p_read_audio = (read_fn)resolve(mod, "WhsHggqdwcg", "sceAvcap2ReadAudio");
  if (!p_initialize || !p_terminate || !p_open_audio || !p_close ||
      !p_start || !p_stop || !p_read_audio)
    return -1;

  return start_capture();
}

int capture_read(float *buf, size_t size) {
  unsigned char info[16];
  int r;

  if (!g_started)
    return -1;
  r = p_read_audio(g_handle, buf, size, info, READ_NOWAIT);
  if (r == (int)ERR_EMPTY)
    return 0;
  if (r == (int)ERR_OVERRUN) {
    g_overruns++;
    return 0;
  }
  if (r < 0) {
    // 0x81950008: il servizio ha chiuso la sessione (es. gioco chiuso)
    printf("capture: ReadAudio -> %#x\n", (unsigned)r);
    return -1;
  }
  if ((size_t)r > size || (size_t)r % (sizeof(float) * CAPTURE_CHANNELS)) {
    printf("capture: lunghezza ReadAudio non valida: %d\n", r);
    return -1;
  }
  return r;
}

int capture_restart(void) {
  capture_close();
  return start_capture();
}

void capture_close(void) {
  void *handle = g_handle;
  int started = g_started, initialized = g_initialized;

  g_started = g_initialized = 0;
  g_handle = NULL;
  if (started)
    p_stop(handle);
  if (handle)
    p_close(handle);
  if (initialized)
    p_terminate();
}
