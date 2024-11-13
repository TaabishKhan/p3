#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <dirent.h>
#include <string.h>
#include "client.h"  // Assuming client.h has necessary function declarations

#define MAX_FILENAME_LEN 1024

// Struct to pass parameters to each client thread
typedef struct {
    char filename[MAX_FILENAME_LEN];
    int server_port;
    char *output_dir;
} client_thread_arg_t;

void *client_thread_func(void *arg) {
    client_thread_arg_t *thread_arg = (client_thread_arg_t *)arg;
    int fd = setup_connection(thread_arg->server_port);  // setup_connection is a provided function

    if (fd < 0) {
        perror("Connection setup failed");
        pthread_exit(NULL);
    }

    // Open image file
    FILE *file = fopen(thread_arg->filename, "rb");
    if (!file) {
        perror("Failed to open image file");
        close(fd);
        pthread_exit(NULL);
    }

    // Get image size and send to server
    fseek(file, 0, SEEK_END);
    size_t img_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    send_file_to_server(fd, file, img_size);  // send_file_to_server is a provided function

    // Allocate buffer for server response
    char *response_buf = (char *)malloc(img_size);
    if (!response_buf) {
        perror("Failed to allocate buffer");
        fclose(file);
        close(fd);
        pthread_exit(NULL);
    }

    // Read server response and save output
    read(fd, response_buf, img_size);  // Assuming server response is equal in size
    char output_path[MAX_FILENAME_LEN];
    snprintf(output_path, MAX_FILENAME_LEN, "%s/%s", thread_arg->output_dir, strrchr(thread_arg->filename, '/'));

    FILE *out_file = fopen(output_path, "wb");
    if (out_file) {
        fwrite(response_buf, 1, img_size, out_file);
        fclose(out_file);
    } else {
        perror("Failed to open output file");
    }

    // Cleanup
    free(response_buf);
    fclose(file);
    close(fd);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <directory> <server_port> <output_dir>\n", argv[0]);
        exit(1);
    }

    char *dir_path = argv[1];
    int server_port = atoi(argv[2]);
    char *output_dir = argv[3];

    DIR *dir = opendir(dir_path);
    if (!dir) {
        perror("Failed to open directory");
        exit(1);
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {  // Only regular files
            pthread_t thread;
            client_thread_arg_t *arg = malloc(sizeof(client_thread_arg_t));
            snprintf(arg->filename, MAX_FILENAME_LEN, "%s/%s", dir_path, entry->d_name);
            arg->server_port = server_port;
            arg->output_dir = output_dir;

            pthread_create(&thread, NULL, client_thread_func, (void *)arg);
            pthread_detach(thread);  // Detach to auto-cleanup
        }
    }

    closedir(dir);
    pthread_exit(NULL);
}
