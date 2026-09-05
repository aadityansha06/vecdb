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

void save_api_key(const char *db_name, const char *api_key) {
  mkdir("origin_data", 0777);

  FILE *fp = fopen("origin_data/.auth_keys", "a");
  if (fp != NULL) {
    fprintf(fp, "%s:%s\n", db_name, api_key);
    fclose(fp);
  } else {
    perror("Error: Could not save API key to disk");
  }
}
