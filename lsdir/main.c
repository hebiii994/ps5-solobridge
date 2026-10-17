// lsdir: elenca ricorsivamente (2 livelli) alcune cartelle della PS5.

#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void list(const char *path, int depth) {
  DIR *d = opendir(path);
  if (!d) {
    printf("%*s(%s non leggibile, errno %d)\n", depth * 2, "", path, errno);
    return;
  }
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    if (e->d_name[0] == '.')
      continue;
    char sub[512];
    snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name);
    struct stat st;
    long size = stat(sub, &st) == 0 ? (long)st.st_size : -1;
    if (e->d_type == DT_DIR) {
      printf("%*s%s/\n", depth * 2, "", e->d_name);
      if (depth < 4)
        list(sub, depth + 1);
    } else {
      printf("%*s%s (%ld)\n", depth * 2, "", e->d_name, size);
      if ((strstr(e->d_name, ".txt") || (strstr(e->d_name, ".json") && size < 300)) &&
          size > 0 && size < 2048) {
        FILE *f = fopen(sub, "r");
        char line[256];
        while (f && fgets(line, sizeof(line), f))
          printf("%*s| %s", depth * 2 + 2, "", line);
        if (f) fclose(f);
      }
    }
  }
  closedir(d);
}

int main(void) {
  const char *roots[] = { "/data/pldmgr" };
  for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
    printf("%s/\n", roots[i]);
    list(roots[i], 1);
  }
  return 0;
}
