#include "../include/api-key-generate.h"

void generate_api_key(char *out_key) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        perror("Fatal Error: Could not access /dev/urandom");
        exit(1);
    }
    
    unsigned char rand_bytes[32];
    read(fd, rand_bytes, sizeof(rand_bytes));
    close(fd);
    
    // Convert the 32 raw bytes into a 64-character hex string
    for (int i = 0; i < 32; i++) {
        sprintf(out_key + (i * 2), "%02x", rand_bytes[i]);
    }
    out_key[64] = '\0';
}

void save_api_key(const char *db_name, const char *api_key) {
    // Ensure the root origin_data folder exists before writing auth file
    mkdir("origin_data", 0777); 
    
    FILE *fp = fopen("origin_data/.auth_keys", "a");
    if (fp != NULL) {
        // Store as a simple mapping: db_name:api_key
        fprintf(fp, "%s:%s\n", db_name, api_key);
        fclose(fp);
    } else {
        perror("Error: Could not save API key to disk");
    }
}
