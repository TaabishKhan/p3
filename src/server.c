#include "../include/server.h"
#include <math.h>
#include <float.h>  // For DBL_MAX

// Global Variables
int num_dispatcher = 0; // Number of dispatcher threads
int num_worker = 0;     // Number of worker threads
FILE *logfile;          // Log file pointer
int queue_len = 0;      // Request queue length

// Queue structures and indices for tracking request addition/removal
request_t request_queue[100];
int queue_count = 0;           
int add_index = 0;             
int remove_index = 0;          

// Synchronization tools for request queue management
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_not_full = PTHREAD_COND_INITIALIZER;
pthread_cond_t queue_not_empty = PTHREAD_COND_INITIALIZER;

// Database to store preloaded images
database_entry_t database[100];
int database_count = 0;

// Thread arrays for dispatchers and workers
pthread_t dispatcher_threads[100];
pthread_t worker_threads[100];

/**********************************************
 * calculate_mse
   - Calculates Mean Squared Error (MSE) between
     two image buffers for similarity comparison
   - Parameters:
      - image1, image2: image buffers to compare
      - size: size of the images
   - Returns:
       - The MSE value between the two images
************************************************/
static double calculate_mse(char *image1, char *image2, int size) {
    double mse = 0.0;
    for (int i = 0; i < size; i++) {
        int diff = (unsigned char)image1[i] - (unsigned char)image2[i];
        mse += diff * diff;
    }
    return mse / size;
}

/**********************************************
 * image_match
   - Finds the closest match in the database to the input image
   - Parameters:
      - input_image: the image data to compare
      - size: the size of the image data
   - Returns:
       - database_entry_t of the closest match
************************************************/
database_entry_t image_match(char *input_image, int size) {
    database_entry_t best_match;
    double min_mse = DBL_MAX;

    // Initialize best_match with empty values to indicate no match if not found
    best_match.file_name[0] = '\0';
    best_match.file_size = 0;
    best_match.buffer = NULL;

    // Iterate over each image in the database to find the closest match
    for (int i = 0; i < database_count; i++) {
        database_entry_t *db_entry = &database[i];
        
        // Compare only images of the same size
        if (db_entry->file_size == size) {
            double mse = calculate_mse(input_image, db_entry->buffer, size);

            // Update best match if the current image has a lower MSE
            if (mse < min_mse) {
                min_mse = mse;
                best_match = *db_entry;
            }
        }
    }
    return best_match;
}

/**********************************************
 * LogPrettyPrint
   - Logs request details to a file or console
   - Parameters:
      - to_write: file pointer for the log file or NULL for stdout
      - threadId, requestNumber, file_name, file_size: log details
************************************************/
void LogPrettyPrint(FILE *to_write, int level, int threadId, int requestNumber, char *file_name, int file_size, double mse) {
    fprintf(to_write ? to_write : stdout, "[%d][%d][%d][%s][%d bytes][%.2f MSE]\n", 
            level, threadId, requestNumber, file_name, file_size, mse);
}


/**********************************************
 * loadDatabase
   - Loads images from a directory into the database
   - Parameters:
      - path: path to the directory containing images
   - This function traverses the specified directory,
     reads each image into memory, and adds it to the
     global database for fast lookup.
************************************************/
void loadDatabase(char *path) {
    DIR *dir = opendir(path);
    struct dirent *entry;

    if (!dir) {
        perror("Failed to open database directory");
        return;
    }

    // Read each file in the directory
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {  // Only process regular files
            char file_path[1024];
            snprintf(file_path, sizeof(file_path), "%s/%s", path, entry->d_name);

            FILE *file = fopen(file_path, "rb");
            if (!file) continue;

            // Read file size
            fseek(file, 0, SEEK_END);
            int file_size = ftell(file);
            fseek(file, 0, SEEK_SET);

            // Allocate buffer to store file content
            char *buffer = malloc(file_size);
            if (!buffer) {
                fclose(file);
                continue;
            }

            // Read file content into buffer
            fread(buffer, 1, file_size, file);
            fclose(file);

            // Store file details in database
            database_entry_t *db_entry = &database[database_count++];
            strcpy(db_entry->file_name, entry->d_name);
            db_entry->file_size = file_size;
            db_entry->buffer = buffer;
        }
    }
    closedir(dir);
}

/**********************************************
 * dispatch
   - Dispatcher thread function to accept client connections,
     receive image requests, and add requests to the queue
   - Each dispatcher thread will continuously accept new client
     requests, read image data, and queue it for worker threads.
************************************************/
void *dispatch(void *thread_id) {
    while (1) {
        size_t file_size;
        int fd = accept_connection();  // Get client connection file descriptor
        if (fd < 0) continue;

        // Read image data from the client
        char *buffer = get_request_server(fd, &file_size);
        if (!buffer) {
            close(fd);
            continue;
        }

        // Lock the queue before adding a new request
        pthread_mutex_lock(&queue_mutex);

        // Wait if the queue is full
        while (queue_count == queue_len) {
            pthread_cond_wait(&queue_not_full, &queue_mutex);
        }

        // Add request to queue at the next available slot
        request_t *req = &request_queue[add_index];
        req->file_descriptor = fd;
        req->buffer = buffer;
        req->file_size = file_size;

        add_index = (add_index + 1) % queue_len;
        queue_count++;

        // Signal that the queue is not empty and unlock
        pthread_cond_signal(&queue_not_empty);
        pthread_mutex_unlock(&queue_mutex);
    }
    return NULL;
}

/**********************************************
 * worker
   - Worker thread function to retrieve requests from the queue,
     match images, send responses to clients, and log requests
   - Each worker retrieves requests from the queue, matches the
     image to the best match in the database, and sends the
     result back to the client.
************************************************/
void *worker(void *thread_id) {
    int thread_id_num = *(int *)thread_id;
    int request_num = 0;

    while (1) {
        // Lock the queue before removing a request
        pthread_mutex_lock(&queue_mutex);

        // Wait if the queue is empty
        while (queue_count == 0) {
            pthread_cond_wait(&queue_not_empty, &queue_mutex);
        }

        // Retrieve the next request from the queue
        request_t req = request_queue[remove_index];
        remove_index = (remove_index + 1) % queue_len;
        queue_count--;

        // Signal that the queue has space and unlock
        pthread_cond_signal(&queue_not_full);
        pthread_mutex_unlock(&queue_mutex);

        // Initialize mse to 0 before performing the match
        double mse = 0.0;

        // Perform image matching on the request buffer
        database_entry_t match = image_match(req.buffer, req.file_size);

        // If a matching image is found, send it to the client and log the transaction
        if (match.buffer) {
            send_file_to_client(req.file_descriptor, match.buffer, match.file_size);
            printf("Logging request %d by thread %d\n", request_num, thread_id_num);
            LogPrettyPrint(logfile, 0, thread_id_num, ++request_num, match.file_name, match.file_size, mse);
            printf("Logged successfully\n");
        }

        // Clean up resources for the request
        close(req.file_descriptor);
        free(req.buffer);
    }
}



/**********************************************
 * main
   - Initializes the server, loads the database, creates threads,
     and waits for threads to complete
   - Parses command-line arguments, initializes global variables,
     and starts dispatcher and worker threads to handle requests.
************************************************/
int main(int argc, char *argv[]) {
    if (argc != 6) {
        printf("usage: %s port path num_dispatcher num_workers queue_length\n", argv[0]);
        return -1;
    }

    // Parse command-line arguments
    int port = atoi(argv[1]);
    char *path = argv[2];
    num_dispatcher = atoi(argv[3]);
    num_worker = atoi(argv[4]);
    queue_len = atoi(argv[5]);

    // Open log file
    logfile = fopen("server_log", "w");
    if (!logfile) {
        perror("Failed to open log file");
        return -1;
    }

    init(port);            // Start server on specified port
    loadDatabase(path);    // Load image files into memory

    // Create dispatcher and worker threads
    int thread_ids[num_worker];
    for (int i = 0; i < num_dispatcher; i++) {
        pthread_create(&dispatcher_threads[i], NULL, dispatch, NULL);
    }

    for (int i = 0; i < num_worker; i++) {
        thread_ids[i] = i;
        pthread_create(&worker_threads[i], NULL, worker, &thread_ids[i]);
    }

    // Join dispatcher and worker threads
    for (int i = 0; i < num_dispatcher; i++) {
        pthread_join(dispatcher_threads[i], NULL);
    }

    for (int i = 0; i < num_worker; i++) {
        pthread_join(worker_threads[i], NULL);
    }

    // Clean up and close log file
    fclose(logfile);
    return 0;
}
