Project 3: MultiThreaded Image Matching Server
CSCI 4061 - Fall 2024
Group Members

    Taabish Khan: khan0882@umn.edu
    Omar Yasin: yasin023@umn.edu
    Michael Sharp: sharp474@umn.edu

Compilation

To clean and compile the project:

make clean  
make

    make clean: Removes compiled files and clears output directories.
    make: Compiles all source code and links the server and client executables.

Execution
Start the Server

./server <port> <path_to_image_database> <num_dispatchers> <num_workers> <queue_length>

    <port>: The port number where the server listens for client requests.
    <path_to_image_database>: Directory containing the image files to be loaded into the database.
    <num_dispatchers>: Number of dispatcher threads handling incoming connections.
    <num_workers>: Number of worker threads processing image requests.
    <queue_length>: Maximum size of the request queue.

Example:

./server 8000 ./database 2 4 10

    8000: Server listens on port 8000.
    ./database: Directory containing the image database.
    2: Two dispatcher threads.
    4: Four worker threads.
    10: Queue can hold a maximum of 10 requests.

Run the Client

./client <path_to_image_folder> <server_port> <output_folder>

    <path_to_image_folder>: Directory with images to be processed.
    <server_port>: Port number to connect to the server.
    <output_folder>: Directory to save the processed/matched images.

Example:

./client ./img 8000 ./output/img

    ./img: Folder containing images to be sent to the server.
    8000: Server port number.
    ./output: Processed images will be saved here.

File Descriptions

    server_log: A file that records details of requests, matches, and thread activities.
    output/img/: Directory where matched images are saved by the client.
    Makefile: Automates the compilation of the server and client programs.

Group Contributions

    Taabish Khan
    Implemented dispatcher and worker thread functions, handling thread-safe queue operations using mutexes and condition variables.
    Omar Yasin
    Designed and optimized the image_match function for accurate image comparison using Mean Squared Error (MSE). Debugged edge cases for database operations.
    Michael Sharp
    Handled file input/output operations, including loadDatabase and logging mechanisms. Ensured efficient memory management for large image buffers.