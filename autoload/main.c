// autoload: aggiunge local/solobridge a /data/pldmgr/autoload.txt
// (copia di sicurezza in autoload.txt.bak, nessun doppione).

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define AUTOLOAD  "/data/pldmgr/autoload.txt"
#define BACKUP    AUTOLOAD ".bak"
#define TMP       AUTOLOAD ".tmp"
#define ENTRY     "local/solobridge"

int main(void) {
  static char buf[16384];
  FILE *f = fopen(AUTOLOAD, "r");
  if (!f) {
    printf("%s non leggibile (errno %d): nessuna modifica\n", AUTOLOAD, errno);
    return 1;
  }
  size_t len = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[len] = 0;

  printf("autoload.txt attuale:\n%s%s", buf, (len && buf[len - 1] != '\n') ? "\n" : "");

  // gia' presente? confronta riga per riga
  char copy[sizeof(buf)];
  memcpy(copy, buf, len + 1);
  for (char *save, *line = strtok_r(copy, "\r\n", &save); line;
       line = strtok_r(NULL, "\r\n", &save)) {
    if (strcmp(line, ENTRY) == 0) {
      printf("%s e' gia' presente: nessuna modifica\n", ENTRY);
      return 0;
    }
  }

  FILE *b = fopen(BACKUP, "w");
  if (!b || fwrite(buf, 1, len, b) != len) {
    printf("backup %s fallito (errno %d): nessuna modifica\n", BACKUP, errno);
    if (b) fclose(b);
    return 1;
  }
  fclose(b);

  FILE *t = fopen(TMP, "w");
  if (!t) {
    printf("open %s fallita (errno %d)\n", TMP, errno);
    return 1;
  }
  fwrite(buf, 1, len, t);
  if (len && buf[len - 1] != '\n')
    fputc('\n', t);
  fputs(ENTRY "\n", t);
  if (fclose(t) != 0 || rename(TMP, AUTOLOAD) != 0) {
    printf("scrittura %s fallita (errno %d)\n", AUTOLOAD, errno);
    remove(TMP);
    return 1;
  }

  f = fopen(AUTOLOAD, "r");
  len = f ? fread(buf, 1, sizeof(buf) - 1, f) : 0;
  if (f) fclose(f);
  buf[len] = 0;
  printf("\nautoload.txt aggiornato (backup in %s):\n%s", BACKUP, buf);
  return 0;
}
