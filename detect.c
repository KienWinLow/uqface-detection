#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <stdint.h>
#include <opencv2/imgcodecs/imgcodecs_c.h>
#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/objdetect/objdetect_c.h>

// OpenCV parameters
const float haarScaleFactor = 1.1;
const int haarMinNeighbours = 4;
const int haarFlags = 0;
const int haarMinSize = 0;
const int haarMaxSize = 1000;
const int ellipseStartAngle = 0;
const int ellipseEndAngle = 360;
const int lineThickness = 4;
const int lineType = 8;
const int shift = 0;
const int bgraChannels = 4;
const int alphaIndex = 3;

// File locations
const char* const faceCascadeFilename = "/local/courses/csse2310/resources/a4/haarcascade_frontalface_alt2.xml";
const char* const eyesCascadeFilename = "/local/courses/csse2310/resources/a4/haarcascade_eye_tree_eyeglasses.xml";
const char* const imageFilename = "/tmp/imagefile.jpg";
const char* const responseFilename = "/local/courses/csse2310/resources/a4/responsefile";

// Protocol constants
#define PROTOCOL_PREFIX 0x23107231
#define OP_FACE_DETECT 0
#define OP_FACE_REPLACE 1
#define OP_OUTPUT_IMAGE 2
#define OP_ERROR_MESSAGE 3

// Global variables for shared resources
CvHaarClassifierCascade* faceCascade;
CvHaarClassifierCascade* eyesCascade;
sem_t fileSemaphore;
sem_t cascadeSemaphore;
uint32_t maxImageSize;

// Structure to pass data to client threads
typedef struct {
    int clientSocket;
} ClientData;

// Function prototypes
int validate_arguments(int argc, char** argv);
int check_image_file(void);
int check_cascade_files(void);
int setup_server(const char* portStr);
void* handle_client(void* arg);
int read_exact(int socket, void* buffer, size_t bytes);
int write_exact(int socket, const void* buffer, size_t bytes);
void send_error_response(int socket, const char* error);
void send_bad_request_response(int socket);
int process_face_detection(int socket, uint32_t imageSize);
int process_face_replacement(int socket, uint32_t image1Size, uint32_t image2Size);
uint32_t read_uint32_le(int socket);
void write_uint32_le(int socket, uint32_t value);

// main
//      Entry point that initializes the face detection server and handles client connections
int main(int argc, char** argv) {
    // Validate command line arguments
    if (validate_arguments(argc, argv) != 0) {
        fprintf(stderr, "Usage: ./uqfacedetect connectionlimit maxsize [portnum]\n");
        exit(8);
    }
    
    // Parse arguments
    int connectionLimit = atoi(argv[1]);
    maxImageSize = (uint32_t)strtoul(argv[2], NULL, 10);
    if (maxImageSize == 0) {
        maxImageSize = UINT32_MAX;
    }
    const char* portStr = (argc > 3) ? argv[3] : "0";
    
    // Check image file
    if (check_image_file() != 0) {
        fprintf(stderr, "uqfacedetect: unable to open image file for writing\n");
        exit(4);
    }
    
    // Check cascade files
    if (check_cascade_files() != 0) {
        fprintf(stderr, "uqfacedetect: unable to load a cascade classifier\n");
        exit(13);
    }
    
    // Initialize semaphores
    sem_init(&fileSemaphore, 0, 1);
    sem_init(&cascadeSemaphore, 0, 1);
    
    // Setup server
    int serverSocket = setup_server(portStr);
    if (serverSocket < 0) {
        fprintf(stderr, "uqfacedetect: cannot listen on given port \"%s\"\n", portStr);
        exit(16);
    }
    
    // Get and print actual port number
    struct sockaddr_in addr;
    socklen_t addrLen = sizeof(addr);
    getsockname(serverSocket, (struct sockaddr*)&addr, &addrLen);
    fprintf(stderr, "%d\n", ntohs(addr.sin_port));
    fflush(stderr);
    
    // Main server loop
    while (1) {
        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
        
        if (clientSocket < 0) {
            continue;
        }
        
        // Create thread to handle client
        pthread_t thread;
        ClientData* clientData = malloc(sizeof(ClientData));
        clientData->clientSocket = clientSocket;
        
        pthread_create(&thread, NULL, handle_client, clientData);
        pthread_detach(thread);
    }
    
    return 0;
}

// validate_arguments
//      Validates command line arguments for connection limit, max size, and optional port number
int validate_arguments(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        return -1;
    }
    
    // Check connectionlimit
    char* endptr;
    long connectionLimit = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || connectionLimit < 0 || connectionLimit > 10000) {
        return -1;
    }
    
    // Check maxsize
    unsigned long maxSize = strtoul(argv[2], &endptr, 10);
    if (*endptr != '\0') {
        return -1;
    }
    
    // Check portnum if provided
    if (argc == 4 && strlen(argv[3]) == 0) {
        return -1;
    }
    
    return 0;
}

// check_image_file
//                 Verifies that the temporary image file can be created and written to
int check_image_file(void) {
    FILE* file = fopen(imageFilename, "w");
    if (!file) {
        return -1;
    }
    fclose(file);
    return 0;
}

// check_cascade_files
//      Loads and validates the Haar cascade classifiers for face and eye detection
int check_cascade_files(void) {
    faceCascade = (CvHaarClassifierCascade*)cvLoad(faceCascadeFilename, NULL, NULL, NULL);
    eyesCascade = (CvHaarClassifierCascade*)cvLoad(eyesCascadeFilename, NULL, NULL, NULL);
    
    if (!faceCascade || !eyesCascade) {
        return -1;
    }
    return 0;
}

// setup_server
//      Creates and configures a TCP server socket on the specified port or ephemeral port
int setup_server(const char* portStr) {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        return -1;
    }
    
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    
    if (strcmp(portStr, "0") == 0) {
        serverAddr.sin_port = 0; // ephemeral port
    } else {
        char* endptr;
        long port = strtol(portStr, &endptr, 10);
        if (*endptr == '\0' && port > 0 && port <= 65535) {
            serverAddr.sin_port = htons((uint16_t)port);
        } else {
            // Try as service name
            struct servent* service = getservbyname(portStr, "tcp");
            if (!service) {
                close(serverSocket);
                return -1;
            }
            serverAddr.sin_port = service->s_port;
        }
    }
    
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(serverSocket);
        return -1;
    }
    
    if (listen(serverSocket, 10) < 0) {
        close(serverSocket);
        return -1;
    }
    
    return serverSocket;
}

// handle_client
//      Thread function that processes client requests for face detection or replacement operations
void* handle_client(void* arg) {
    ClientData* clientData = (ClientData*)arg;
    int socket = clientData->clientSocket;
    free(clientData);
    
    while (1) {
        // Read prefix
        uint32_t prefix;
        if (read_exact(socket, &prefix, 4) != 0) {
            break;
        }
        
        if (prefix != PROTOCOL_PREFIX) {
            send_bad_request_response(socket);
            break;
        }
        
        // Read operation type
        uint8_t opType;
        if (read_exact(socket, &opType, 1) != 0) {
            send_error_response(socket, "invalid message");
            break;
        }
        
        if (opType != OP_FACE_DETECT && opType != OP_FACE_REPLACE) {
            send_error_response(socket, "invalid operation type");
            break;
        }
        
        // Read image 1 size
        uint32_t image1Size = read_uint32_le(socket);
        if (image1Size == 0) {
            send_error_response(socket, "image is 0 bytes");
            break;
        }
        
        if (image1Size > maxImageSize) {
            send_error_response(socket, "image too large");
            break;
        }
        
        if (opType == OP_FACE_DETECT) {
            if (process_face_detection(socket, image1Size) != 0) {
                break;
            }
        } else if (opType == OP_FACE_REPLACE) {
            // Read image 2 size
            uint32_t image2Size = read_uint32_le(socket);
            if (image2Size == 0) {
                send_error_response(socket, "image is 0 bytes");
                break;
            }
            
            if (image2Size > maxImageSize) {
                send_error_response(socket, "image too large");
                break;
            }
            
            if (process_face_replacement(socket, image1Size, image2Size) != 0) {
                break;
            }
        }
    }
    
    close(socket);
    return NULL;
}

// process_face_detection
//      Performs face and eye detection on an image and returns the annotated result
int process_face_detection(int socket, uint32_t imageSize) {
    // Read image data
    char* imageData = malloc(imageSize);
    if (read_exact(socket, imageData, imageSize) != 0) {
        free(imageData);
        return -1;
    }
    
    sem_wait(&fileSemaphore);
    
    // Save image to file
    FILE* file = fopen(imageFilename, "wb");
    if (!file) {
        sem_post(&fileSemaphore);
        free(imageData);
        send_error_response(socket, "invalid image");
        return -1;
    }
    fwrite(imageData, 1, imageSize, file);
    fclose(file);
    free(imageData);
    
    sem_wait(&cascadeSemaphore);
    
    // Load image
    IplImage* frame = cvLoadImage(imageFilename, CV_LOAD_IMAGE_COLOR);
    if (!frame) {
        sem_post(&cascadeSemaphore);
        sem_post(&fileSemaphore);
        send_error_response(socket, "invalid image");
        return -1;
    }
    
    // Process image for face detection
    IplImage* frameGray = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 1);
    cvCvtColor(frame, frameGray, CV_BGR2GRAY);
    cvEqualizeHist(frameGray, frameGray);
    
    CvMemStorage* storage = cvCreateMemStorage(0);
    cvClearMemStorage(storage);
    
    CvSeq* faces = cvHaarDetectObjects(frameGray, faceCascade, storage,
        haarScaleFactor, haarMinNeighbours, haarFlags,
        cvSize(haarMinSize, haarMinSize), cvSize(haarMaxSize, haarMaxSize));
    
    if (faces->total == 0) {
        cvReleaseImage(&frame);
        cvReleaseImage(&frameGray);
        cvReleaseMemStorage(&storage);
        sem_post(&cascadeSemaphore);
        sem_post(&fileSemaphore);
        send_error_response(socket, "no faces detected in image");
        return -1;
    }
    
    // Draw ellipses around faces and circles around eyes
    for (int i = 0; i < faces->total; i++) {
        CvRect* face = (CvRect*)cvGetSeqElem(faces, i);
        CvPoint center = {face->x + face->width / 2, face->y + face->height / 2};
        const CvScalar magenta = cvScalar(255, 0, 255, 0);
        const CvScalar blue = cvScalar(255, 0, 0, 0);
        
        cvEllipse(frame, center, cvSize(face->width / 2, face->height / 2), 0,
            ellipseStartAngle, ellipseEndAngle, magenta, lineThickness,
            lineType, shift);
        
        IplImage* faceROI = cvCreateImage(cvGetSize(frameGray), IPL_DEPTH_8U, 1);
        cvCopy(frameGray, faceROI, NULL);
        cvSetImageROI(faceROI, *face);
        
        CvMemStorage* eyeStorage = cvCreateMemStorage(0);
        cvClearMemStorage(eyeStorage);
        
        CvSeq* eyes = cvHaarDetectObjects(faceROI, eyesCascade, eyeStorage,
            haarScaleFactor, haarMinNeighbours, haarFlags,
            cvSize(haarMinSize, haarMinSize),
            cvSize(haarMaxSize, haarMaxSize));
        
        if (eyes->total == 2) {
            for (int j = 0; j < eyes->total; j++) {
                CvRect* eye = (CvRect*)cvGetSeqElem(eyes, j);
                CvPoint eyeCenter = {face->x + eye->x + eye->width / 2,
                    face->y + eye->y + eye->height / 2};
                int radius = cvRound((eye->width / 2 + eye->height / 2) / 2);
                cvCircle(frame, eyeCenter, radius, blue, lineThickness, lineType, shift);
            }
        }
        
        cvReleaseImage(&faceROI);
        cvReleaseMemStorage(&eyeStorage);
    }
    
    // Save processed image
    cvSaveImage(imageFilename, frame, 0);
    
    cvReleaseImage(&frame);
    cvReleaseImage(&frameGray);
    cvReleaseMemStorage(&storage);
    sem_post(&cascadeSemaphore);
    
    // Read processed image and send to client
    file = fopen(imageFilename, "rb");
    fseek(file, 0, SEEK_END);
    long outputSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* outputData = malloc(outputSize);
    fread(outputData, 1, outputSize, file);
    fclose(file);
    sem_post(&fileSemaphore);
    
    // Send response
    uint32_t prefix = PROTOCOL_PREFIX;
    uint8_t opType = OP_OUTPUT_IMAGE;
    uint32_t size = (uint32_t)outputSize;
    
    write_exact(socket, &prefix, 4);
    write_exact(socket, &opType, 1);
    write_uint32_le(socket, size);
    write_exact(socket, outputData, outputSize);
    
    free(outputData);
    return 0;
}

// process_face_replacement
//      Replaces detected faces in the first image with content from the second image
int process_face_replacement(int socket, uint32_t image1Size, uint32_t image2Size) {
    // Read both images
    char* image1Data = malloc(image1Size);
    char* image2Data = malloc(image2Size);
    
    if (read_exact(socket, image1Data, image1Size) != 0 ||
        read_exact(socket, image2Data, image2Size) != 0) {
        free(image1Data);
        free(image2Data);
        return -1;
    }
    
    sem_wait(&fileSemaphore);
    sem_wait(&cascadeSemaphore);
    
    // Save and load first image
    FILE* file = fopen(imageFilename, "wb");
    if (!file) {
        sem_post(&cascadeSemaphore);
        sem_post(&fileSemaphore);
        free(image1Data);
        free(image2Data);
        send_error_response(socket, "invalid image");
        return -1;
    }
    fwrite(image1Data, 1, image1Size, file);
    fclose(file);
    free(image1Data);
    
    IplImage* frame = cvLoadImage(imageFilename, CV_LOAD_IMAGE_COLOR);
    if (!frame) {
        sem_post(&cascadeSemaphore);
        sem_post(&fileSemaphore);
        free(image2Data);
        send_error_response(socket, "invalid image");
        return -1;
    }
    
    // Save and load second image
    file = fopen(imageFilename, "wb");
    fwrite(image2Data, 1, image2Size, file);
    fclose(file);
    free(image2Data);
    
    IplImage* replace = cvLoadImage(imageFilename, CV_LOAD_IMAGE_UNCHANGED);
    if (!replace) {
        cvReleaseImage(&frame);
        sem_post(&cascadeSemaphore);
        sem_post(&fileSemaphore);
        send_error_response(socket, "invalid image");
        return -1;
    }
    
    // Process face replacement (similar to detection example)
    IplImage* frameGray = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 1);
    cvCvtColor(frame, frameGray, CV_BGR2GRAY);
    cvEqualizeHist(frameGray, frameGray);
    
    CvMemStorage* storage = cvCreateMemStorage(0);
    cvClearMemStorage(storage);
    
    CvSeq* faces = cvHaarDetectObjects(frameGray, faceCascade, storage,
        haarScaleFactor, haarMinNeighbours, haarFlags,
        cvSize(haarMinSize, haarMinSize), cvSize(haarMaxSize, haarMaxSize));
    
    if (faces->total == 0) {
        cvReleaseImage(&frame);
        cvReleaseImage(&replace);
        cvReleaseImage(&frameGray);
        cvReleaseMemStorage(&storage);
        sem_post(&cascadeSemaphore);
        sem_post(&fileSemaphore);
        send_error_response(socket, "no faces detected in image");
        return -1;
    }
    
    // Replace faces
    for (int i = 0; i < faces->total; i++) {
        CvRect* face = (CvRect*)cvGetSeqElem(faces, i);
        IplImage* resized = cvCreateImage(cvSize(face->width, face->height),
            IPL_DEPTH_8U, replace->nChannels);
        
        cvResize(replace, resized, CV_INTER_AREA);
        
        char* frameData = frame->imageData;
        char* faceData = resized->imageData;
        
        for (int y = 0; y < face->height; y++) {
            for (int x = 0; x < face->width; x++) {
                int faceIndex = (resized->widthStep * y) + (x * resized->nChannels);
                
                if ((resized->nChannels == bgraChannels) &&
                    (faceData[faceIndex + alphaIndex] == 0)) {
                    continue;
                }
                
                int frameIndex = (frame->widthStep * (face->y + y)) +
                    ((face->x + x) * frame->nChannels);
                frameData[frameIndex + 0] = faceData[faceIndex + 0];
                frameData[frameIndex + 1] = faceData[faceIndex + 1];
                frameData[frameIndex + 2] = faceData[faceIndex + 2];
            }
        }
        
        cvReleaseImage(&resized);
    }
    
    // Save processed image
    cvSaveImage(imageFilename, frame, 0);
    
    cvReleaseImage(&frame);
    cvReleaseImage(&replace);
    cvReleaseImage(&frameGray);
    cvReleaseMemStorage(&storage);
    sem_post(&cascadeSemaphore);
    
    // Read and send processed image
    file = fopen(imageFilename, "rb");
    fseek(file, 0, SEEK_END);
    long outputSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* outputData = malloc(outputSize);
    fread(outputData, 1, outputSize, file);
    fclose(file);
    sem_post(&fileSemaphore);
    
    // Send response
    uint32_t prefix = PROTOCOL_PREFIX;
    uint8_t opType = OP_OUTPUT_IMAGE;
    uint32_t size = (uint32_t)outputSize;
    
    write_exact(socket, &prefix, 4);
    write_exact(socket, &opType, 1);
    write_uint32_le(socket, size);
    write_exact(socket, outputData, outputSize);
    
    free(outputData);
    return 0;
}

// read_exact
//      Reads exactly the specified number of bytes from a socket, handling partial reads
// REF: this code is reference to a responds from claude.
int read_exact(int socket, void* buffer, size_t bytes) {
    size_t totalRead = 0;
    char* buf = (char*)buffer;
    
    while (totalRead < bytes) {
        ssize_t bytesRead = read(socket, buf + totalRead, bytes - totalRead);
        if (bytesRead <= 0) {
            return -1;
        }
        totalRead += bytesRead;
    }
    return 0;
}

// write_exact
//      Writes exactly the specified number of bytes to a socket, handling partial writes
// REF: this code is a reference to a responds from claude.
int write_exact(int socket, const void* buffer, size_t bytes) {
    size_t totalWritten = 0;
    const char* buf = (const char*)buffer;
    
    while (totalWritten < bytes) {
        ssize_t bytesWritten = write(socket, buf + totalWritten, bytes - totalWritten);
        if (bytesWritten <= 0) {
            return -1;
        }
        totalWritten += bytesWritten;
    }
    return 0;
}

// read_uint32_le
//      Reads a 32-bit unsigned integer in little-endian format from a socket
uint32_t read_uint32_le(int socket) {
    uint32_t value;
    read_exact(socket, &value, 4);
    return value; // Assuming little-endian system
}

// write_uint32_le
//      Writes a 32-bit unsigned integer in little-endian format to a socket
void write_uint32_le(int socket, uint32_t value) {
    write_exact(socket, &value, 4); // Assuming little-endian system
}

// send_error_response
//      Sends a formatted error message response to the client using the protocol format
// REF: this code references a respond from claude.
void send_error_response(int socket, const char* error) {
    uint32_t prefix = PROTOCOL_PREFIX;
    uint8_t opType = OP_ERROR_MESSAGE;
    uint32_t errorLen = strlen(error);
    
    write_exact(socket, &prefix, 4);
    write_exact(socket, &opType, 1);
    write_uint32_le(socket, errorLen);
    write_exact(socket, error, errorLen);
}

// send_bad_request_response
//      Sends a predefined bad request response file to the client for invalid protocol requests
void send_bad_request_response(int socket) {
    FILE* file = fopen(responseFilename, "rb");
    if (!file) {
        return;
    }
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* data = malloc(size);
    fread(data, 1, size, file);
    fclose(file);
    
    write_exact(socket, data, size);
    free(data);
}