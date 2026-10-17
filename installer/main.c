// installer: installa solobridge in Payload Manager come payload locale
// (/data/pldmgr/payloads/local/solobridge/ con l'ELF e meta.json).

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LOCAL_DIR   "/data/pldmgr/payloads/local"
#define TARGET_DIR  LOCAL_DIR "/solobridge"
#define ELF_NAME    "solobridge.elf"

// Cartella creata per errore da una versione precedente di questo installer.
#define OLD_DIR     "/data/pldmgr/payloads/solobridge"

#define EMBED(name, path)                                        \
  __asm__(".section .rodata\n.balign 16\n"                      \
          ".global " #name "_start\n" #name "_start:\n"         \
          ".incbin \"" path "\"\n"                               \
          ".global " #name "_end\n" #name "_end:\n.previous\n"); \
  extern const unsigned char name##_start[], name##_end[];

EMBED(elf, "../solobridge/solobridge.elf")

static int write_file(const char *name, const void *data, size_t len) {
  char path[256], tmp[256];
  snprintf(path, sizeof(path), "%s/%s", TARGET_DIR, name);
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);

  int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0777);
  if (fd < 0) {
    printf("open %s fallita (errno %d)\n", tmp, errno);
    return -1;
  }
  size_t off = 0;
  while (off < len) {
    ssize_t w = write(fd, (const char *)data + off, len - off);
    if (w <= 0) {
      printf("write %s fallita (errno %d)\n", tmp, errno);
      close(fd);
      unlink(tmp);
      return -1;
    }
    off += w;
  }
  close(fd);
  if (rename(tmp, path) != 0) {
    printf("rename %s fallita (errno %d)\n", path, errno);
    unlink(tmp);
    return -1;
  }

  struct stat st;
  if (stat(path, &st) != 0 || (size_t)st.st_size != len) {
    printf("verifica %s fallita\n", path);
    return -1;
  }
  printf("scritto %s (%zu byte)\n", path, len);
  return 0;
}

static void remove_old_dir(void) {
  const char *files[] = { "solobridge.elf", "solobridge.elf.json" };
  struct stat st;
  if (stat(OLD_DIR, &st) != 0)
    return;
  for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
    char p[256];
    snprintf(p, sizeof(p), "%s/%s", OLD_DIR, files[i]);
    unlink(p);
  }
  if (rmdir(OLD_DIR) == 0)
    printf("rimossa la vecchia cartella %s\n", OLD_DIR);
  else
    printf("impossibile rimuovere %s (errno %d)\n", OLD_DIR, errno);
}

int main(void) {
  struct stat st;
  if (stat(LOCAL_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) {
    printf("%s non esiste: Payload Manager non e' installato? Nessuna modifica.\n",
           LOCAL_DIR);
    return 1;
  }

  if (mkdir(TARGET_DIR, 0777) != 0 && errno != EEXIST) {
    printf("mkdir %s fallita (errno %d)\n", TARGET_DIR, errno);
    return 1;
  }
  if (write_file(ELF_NAME, elf_start, elf_end - elf_start) != 0)
    return 1;

  // Stesso formato dei payload caricati dall'interfaccia web di pldmgr.
  char ts[32], meta[512];
  time_t t = time(NULL);
  struct tm tm;
  localtime_r(&t, &tm);
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S%z", &tm);
  int n = snprintf(meta, sizeof(meta),
                   "{\n"
                   "  \"schema\": 2,\n"
                   "  \"kind\": \"local\",\n"
                   "  \"filename\": \"%s\",\n"
                   "  \"installed_at\": \"%s\",\n"
                   "  \"origin\": \"web_upload\",\n"
                   "  \"origin_detail\": \"\"\n"
                   "}\n", ELF_NAME, ts);
  if (write_file("meta.json", meta, n) != 0)
    return 1;

  remove_old_dir();
  printf("OK: solobridge installato in Payload Manager (local/solobridge)\n");
  return 0;
}
