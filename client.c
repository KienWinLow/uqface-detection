#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/stat.h>

#define BUFFER_SIZE 1024
#define EXIT_3 3
#define EXIT_6 6
#define EXIT_7 7
#define EXIT_8 8
#define EXIT_13 13
#define EXIT_17 17
#define OPERATION_SIZE 32
#define HEADER_SIZE 256

// Argument struct to hold
typedef struct {
    char* port;
    char* detectFile;
    char* replaceFile;
    char* outputFile;
    bool detect;
    bool replace;
    bool output;
    bool useStdin;
    bool useStdout;
} Arguments;

// Function Prototype
Arguments* init_arguments(void);
void free_arguments(Arguments* args);
void print_usage(void);
void print_file_error(const char* filename);
void print_file_output_error(const char* filename);
bool parse_arguments(int argc, char* argv[], Arguments* args);
int check_input_files(Arguments* args);
int check_output_file(Arguments* args);

// init_option
//      Function that initializes the Option struct with default values.
Arguments* init_arguments(void)
{
    Arguments* args = (Arguments*)malloc(sizeof(Arguments));

    args->port = NULL;
    args->detectFile = NULL;
    args->replaceFile = NULL;
    args->outputFile = NULL;
    args->detect = false;
    args->replace = false;
    args->output = false;
    args->useStdin = true;
    args->useStdout = true;

    return args;
}

// free_Arguments
//      Function to free the Arguments struct memory
void free_arguments(Arguments* args)
{
    if (args != NULL) {
        free(args);
    }
}

// print_usage
//      Prints the usage message to stderr
void print_usage(void)
{
    fprintf(stderr,
            "Usage: ./uqfaceclient portnumber [--detect filename] "
            "[--replacefilename filename] [--output filename]\n");
}

// print_file_error
//      Prints the file error message to stderr
void print_file_error(const char* filename)
{
    fprintf(stderr,
            "uqfaceclient: cannot open the input file \"%s\" for reading\n",
            filename);
}

// print_file_output_error
//      Prints the output error message to stderr
void print_file_output_error(const char* filename)
{
    fprintf(stderr,
            "uqfaceclient: unable to open the output file \"%s\" for writing\n",
            filename);
}

// parse_arguments
//      Parses command line arguments and fills the Arguments struct
//      Returns true if arguments are valid, false otherwise
bool parse_arguments(int argc, char* argv[], Arguments* args)
{
    // Need at least program name and port
    if (argc < 2) {
        return false;
    }

    // Check if port is empty
    if (strlen(argv[1]) == 0) {
        return false;
    }

    args->port = argv[1];

    // Parse remaining arguments
    for (int i = 2; i < argc; i++) {
        // Check for empty argument
        if (strlen(argv[i]) == 0) {
            return false;
        }

        if (strcmp(argv[i], "--detect") == 0) {
            if (args->detect) {
                return false; // Duplicate --detect
            }
            if (i + 1 >= argc || strlen(argv[i + 1]) == 0) {
                return false; // No filename or empty filename
            }
            args->detect = true;
            args->detectFile = argv[i + 1];
            args->useStdin = false;
            i++; // Skip filename
        } else if (strcmp(argv[i], "--replacefilename") == 0) {
            if (args->replace) {
                return false; // Duplicate --replacefilename
            }
            if (i + 1 >= argc || strlen(argv[i + 1]) == 0) {
                return false; // No filename or empty filename
            }
            args->replace = true;
            args->replaceFile = argv[i + 1];
            i++; // Skip filename
        } else if (strcmp(argv[i], "--output") == 0) {
            if (args->output) {
                return false; // Duplicate --output
            }
            if (i + 1 >= argc || strlen(argv[i + 1]) == 0) {
                return false; // No filename or empty filename
            }
            args->output = true;
            args->outputFile = argv[i + 1];
            args->useStdout = false;
            i++; // Skip filename
        } else {
            return false; // Unknown argument
        }
    }

    return true;
}

// check_input_files
//      Checks if input files can be opened for reading
//      Returns 0 if successful, error code otherwise
int check_input_files(Arguments* args)
{
    FILE* file;

    // Check detect file first
    if (args->detect) {
        file = fopen(args->detectFile, "rb");
        if (file == NULL) {
            print_file_error(args->detectFile);
            return EXIT_8;
        }
        fclose(file);
    }

    // Check replace file second
    if (args->replace) {
        file = fopen(args->replaceFile, "rb");
        if (file == NULL) {
            print_file_error(args->replaceFile);
            return EXIT_8;
        }
        fclose(file);
    }

    return 0;
}

// check_output_file
//      Checks if output file can be created/opened for writing
//      Returns 0 if successful, error code otherwise
int check_output_file(Arguments* args)
{
    if (args->output) {
        FILE* file = fopen(args->outputFile, "wb");
        if (file == NULL) {
            print_file_output_error(args->outputFile);
            return EXIT_3;
        }
        fclose(file);

        // Set file permissions (rw for owner)
        chmod(args->outputFile, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    }
    return 0;
}

// connect_to_server
//      Connects to the server on localhost at the specified port
//      Returns socket file descriptor if successful, -1 otherwise
// REF: this code reference something I ask claude.
int connect_to_server(char* port)
{
    int sockfd;
    struct sockaddr_in serverAddr;
    struct hostent* server;
    int portNum;

    // Try to convert port to number
    portNum = atoi(port);
    if (portNum == 0) {
        // Might be a service name
        struct servent* service = getservbyname(port, "tcp");
        if (service == NULL) {
            return -1;
        }
        portNum = ntohs(service->s_port);
    }

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return -1;
    }

    // Get localhost
    server = gethostbyname("localhost");
    if (server == NULL) {
        close(sockfd);
        return -1;
    }

    // Setup server address
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(portNum);
    memcpy(&serverAddr.sin_addr.s_addr, server->h_addr, server->h_length);

    // Connect
    if (connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr))
            < 0) {
        close(sockfd);
        return -1;
    }

    return sockfd;
}

// read_file_data
//      Reads binary data from a file into memory
//      Returns file size if successful, -1 otherwise
// REF: This block of code reference something I ask Claude
long read_file_data(char* filename, unsigned char** data)
{
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        return -1;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Allocate memory
    *data = malloc(size);
    if (*data == NULL) {
        fclose(file);
        return -1;
    }

    // Read file
    if (fread(*data, 1, (size_t)size, file) != (size_t)size) {
        free(*data);
        *data = NULL;
        fclose(file);
        return -1;
    }

    fclose(file);
    return size;
}

// read_stdin_data
//      Reads binary data from stdin into memory
//      Returns data size if successful, -1 otherwise
long read_stdin_data(unsigned char** data)
{
    long size = 0;
    long capacity = BUFFER_SIZE;
    unsigned char buffer[BUFFER_SIZE];

    *data = malloc(capacity);
    if (*data == NULL) {
        return -1;
    }

    size_t bytesRead;
    while ((bytesRead = fread(buffer, 1, BUFFER_SIZE, stdin)) > 0) {
        // Resize if needed
        if (size + (long)bytesRead > capacity) {
            capacity *= 2;
            unsigned char* newData = realloc(*data, capacity);
            if (newData == NULL) {
                free(*data);
                *data = NULL;
                return -1;
            }
            *data = newData;
        }

        // Copy data
        memcpy(*data + size, buffer, bytesRead);
        size += (long)bytesRead;
    }

    return size;
}

// send_all_data
//      Sends all data through socket (handles partial sends)
//      Returns 0 if successful, -1 otherwise
// REF: this code references something I ask Claude
int send_all_data(int sockfd, void* data, int size)
{
    int totalSent = 0;
    int bytesSent;

    while (totalSent < size) {
        bytesSent = send(sockfd, (char*)data + totalSent, size - totalSent, 0);
        if (bytesSent <= 0) {
            return -1;
        }
        totalSent += bytesSent;
    }
    return 0;
}

// recv_all_data
//      Receives all data from socket (handles partial receives)
//      Returns 0 if successful, -1 otherwise
int recv_all_data(int sockfd, void* data, int size)
{
    int totalReceived = 0;
    int bytesReceived;

    while (totalReceived < size) {
        bytesReceived = recv(
                sockfd, (char*)data + totalReceived, size - totalReceived, 0);
        if (bytesReceived <= 0) {
            return -1;
        }
        totalReceived += bytesReceived;
    }
    return 0;
}

// communicate_with_server
//      Handles the full communication protocol with the server
//      Returns appropriate exit code
int communicate_with_server(int sockfd, Arguments* args)
{
    unsigned char* detectData = NULL;
    unsigned char* replaceData = NULL;
    long detectSize = 0;
    long replaceSize = 0;

    // Read detect image
    if (args->detect) {
        detectSize = read_file_data(args->detectFile, &detectData);
    } else {
        detectSize = read_stdin_data(&detectData);
    }

    if (detectSize < 0) {
        return EXIT_13;
    }

    // Read replace image if specified
    if (args->replace) {
        replaceSize = read_file_data(args->replaceFile, &replaceData);
        if (replaceSize < 0) {
            free(detectData);
            return EXIT_13;
        }
    }

    // Send protocol prefix (0x23107231)
    uint32_t prefix = 0x23107231;
    if (send_all_data(sockfd, &prefix, 4) < 0) {
        free(detectData);
        if (replaceData) free(replaceData);
        return EXIT_13;
    }

    // Send operation type (0 for detect, 1 for replace)
    uint8_t opType = args->replace ? 1 : 0;
    if (send_all_data(sockfd, &opType, 1) < 0) {
        free(detectData);
        if (replaceData) free(replaceData);
        return EXIT_13;
    }

    // Send detect image size (little-endian)
    uint32_t detectSizeLE = (uint32_t)detectSize;
    if (send_all_data(sockfd, &detectSizeLE, 4) < 0) {
        free(detectData);
        if (replaceData) free(replaceData);
        return EXIT_13;
    }

    // Send replace image size if doing replacement
    if (args->replace) {
        uint32_t replaceSizeLE = (uint32_t)replaceSize;
        if (send_all_data(sockfd, &replaceSizeLE, 4) < 0) {
            free(detectData);
            free(replaceData);
            return EXIT_13;
        }
    }

    // Send detect image data
    if (send_all_data(sockfd, detectData, detectSize) < 0) {
        free(detectData);
        if (replaceData) free(replaceData);
        return EXIT_13;
    }

    // Send replace image data if we have it
    if (replaceData != NULL) {
        if (send_all_data(sockfd, replaceData, replaceSize) < 0) {
            free(detectData);
            free(replaceData);
            return EXIT_13;
        }
    }

    // Clean up send data
    free(detectData);
    if (replaceData) free(replaceData);

    // Read response prefix
    uint32_t responsePrefix;
    if (recv_all_data(sockfd, &responsePrefix, 4) < 0) {
        return EXIT_13;
    }

    if (responsePrefix != 0x23107231) {
        return EXIT_13;
    }

    // Read response operation type
    uint8_t responseOpType;
    if (recv_all_data(sockfd, &responseOpType, 1) < 0) {
        return EXIT_13;
    }

    // Read response data size
    uint32_t responseSize;
    if (recv_all_data(sockfd, &responseSize, 4) < 0) {
        return EXIT_13;
    }

    if (responseOpType == 2) { // OP_OUTPUT_IMAGE
        // Receive image data
        unsigned char* imageData = malloc(responseSize);
        if (imageData == NULL) {
            return EXIT_13;
        }

        if (recv_all_data(sockfd, imageData, responseSize) < 0) {
            free(imageData);
            return EXIT_13;
        }

        // Write to file or stdout
        if (args->output) {
            FILE* outfile = fopen(args->outputFile, "wb");
            if (outfile == NULL) {
                free(imageData);
                return EXIT_3;
            }
            fwrite(imageData, 1, responseSize, outfile);
            fclose(outfile);
        } else {
            fwrite(imageData, 1, responseSize, stdout);
        }

        free(imageData);
        return 0;
    }
    else if (responseOpType == 3) { // OP_ERROR_MESSAGE
        // Receive error message
        char* errorMsg = malloc(responseSize + 1);
        if (errorMsg == NULL) {
            return EXIT_13;
        }

        if (recv_all_data(sockfd, errorMsg, responseSize) < 0) {
            free(errorMsg);
            return EXIT_13;
        }
        errorMsg[responseSize] = '\0';

        fprintf(stderr, "uqfaceclient: got the following error message: \"%s\"\n", errorMsg);
        free(errorMsg);
        return EXIT_17;
    }

    return EXIT_13;
}

int main(int argc, char* argv[])
{
    Arguments* args = init_arguments();
    int result;

    // Parse command line arguments
    if (!parse_arguments(argc, argv, args)) {
        print_usage();
        free_arguments(args);
        exit(EXIT_6);
    }

    // Check input files
    result = check_input_files(args);
    if (result != 0) {
        free_arguments(args);
        exit(result);
    }

    // Check output file
    result = check_output_file(args);
    if (result != 0) {
        free_arguments(args);
        exit(result);
    }

    // Connect to server
    int sockfd = connect_to_server(args->port);
    if (sockfd < 0) {
        fprintf(stderr,
                "uqfaceclient: unable to connect to the server on port "
                "\"%s\"\n",
                args->port);
        free_arguments(args);
        exit(EXIT_7);
    }

    // Communicate with server
    result = communicate_with_server(sockfd, args);

    // Clean up
    close(sockfd);
    free_arguments(args);
    exit(result);
}
