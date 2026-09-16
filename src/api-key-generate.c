#include "../include/api-key-generate.h"

void generate_api_key(char *out_key) {
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0) {
    perror("Fatal Error: Could not access /dev/urandom");
    exit(1);
  }

unsigned char rand_bytes[32];
    ssize_t bytes_read = read(fd, rand_bytes, sizeof(rand_bytes));
    close(fd);
    
    if (bytes_read != sizeof(rand_bytes)) {
        perror("Fatal Error: Failed to generate full 256-bit key");
        exit(1);
    }  for (int i = 0; i < 32; i++) {
    sprintf(out_key + (i * 2), "%02x", rand_bytes[i]);
  }
  out_key[64] = '\0';
}

/*
 * @Store as a simple mapping db_name:api_key 
 */

/*
 * @Drop every line belonging to db_name.
 *
 * Nothing in the codebase removed a line from .auth_keys before this, so a
 * table folder deleted by hand left its key behind. init then appended a
 * second line for the same name, and verify_api_key returns on the first
 * name match, so whichever line came first won: the dead key could still
 * authenticate against the new table, or shadow the key the user was just
 * given.
 */
void prune_api_key(const char *db_name) {
  FILE *in = fopen("origin_data/.auth_keys", "r");
  if (in == NULL)
    return;

  FILE *out = fopen("origin_data/.auth_keys.tmp", "w");
  if (out == NULL) {
    perror("Error: Could not rewrite .auth_keys");
    fclose(in);
    return;
  }

  size_t name_len = strlen(db_name);
  char line[256];
  while (fgets(line, sizeof(line), in)) {
    if (strncmp(line, db_name, name_len) == 0 && line[name_len] == ':')
      continue;
    fputs(line, out);
  }

  fclose(in);
  fclose(out);

  if (rename("origin_data/.auth_keys.tmp", "origin_data/.auth_keys") != 0) {
    perror("Error: Could not replace .auth_keys");
    remove("origin_data/.auth_keys.tmp");
  }
}

void save_api_key(const char *db_name, const char *api_key) {
  mkdir("origin_data", 0777);
  prune_api_key(db_name);

  FILE *fp = fopen("origin_data/.auth_keys", "a");
  if (fp != NULL) {
    fprintf(fp, "%s:%s\n", db_name, api_key);
    fclose(fp);
  } else {
    perror("Error: Could not save API key to disk");
  }
}
