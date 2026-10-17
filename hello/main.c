#include <stdio.h>
#include <unistd.h>

// Struttura della richiesta per sceKernelSendNotificationRequest
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

int main(void) {
  printf("Hello from PS5 homebrew! pid=%d\n", getpid());
  notify("Hello dal mio primo homebrew!");
  return 0;
}
