// Cattura del mix audio della console tramite il servizio di sistema
// av_capture_manager (libSceAvcap2), aperto come lo apre Remote Play:
// giochi + suoni di sistema, 48 kHz stereo float32, record da 1024 frame.
//
// Basato su src/capture.c di FGG-PlayPods (GPL-3.0):
// https://github.com/FGGstore/FGG-PlayPods
#ifndef CAPTURE_H
#define CAPTURE_H

#include <stddef.h>

#define CAPTURE_RATE     48000
#define CAPTURE_CHANNELS 2
#define CAPTURE_RECORD   8192    // byte per record (1024 frame stereo float)

// 1 = in cattura, 0 = librerie pronte ma servizio non disponibile (riprovare
// con capture_restart), -1 = errore.
int  capture_open(void);

// Legge un record senza attendere: byte letti, 0 se non c'e' nulla, -1 errore.
int  capture_read(float *buf, size_t size);

int  capture_restart(void);
void capture_close(void);
long capture_overruns(void);

#endif
