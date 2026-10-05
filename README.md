# Assignment-3
C multi-threaded face detection client and server using OpenCV

# uqface: Networked Face Detection Client and Server (C)

A client/server system for face detection and face replacement in images. The **server** (`uqfacedetect`) accepts several simultaneous clients over TCP and processes images with OpenCV. The **client** (`uqfaceclient`) sends images to the server and saves the results.

Built for **CSSE2310 (Computer Systems Principles and Programming)**, University of Queensland, Semester 1 2025.

## What it does

- **Face detection**: the server finds faces (and eyes) with Haar cascade classifiers and returns the image with the faces outlined.
- **Face replacement**: the server swaps detected faces with a replacement image.
- The client reads images from a file or from stdin, and writes the result to a file or stdout.
- A custom binary protocol is used between client and server, with a protocol prefix, operation codes (detect, replace, output image, error message) and little-endian sizes.
- The server enforces a **connection limit** and a **maximum image size**, and replies with error messages for bad requests.

## Usage

```
./uqfacedetect connectionlimit maxsize [portnum]
./uqfaceclient portnumber [--detect filename] [--replacefilename filename] [--output filename]
```

## Build

```
make
```

This builds both programs. The server needs OpenCV (core, imgcodecs, objdetect, imgproc) and pthreads. The cascade classifier files were supplied by the course and their paths are set near the top of `detect.c`, so adjust them to build elsewhere.

## Implementation notes

- **Multi-threaded server**: each accepted client is handled by its own `pthread`.
- **Semaphores** guard the shared temporary image file and the shared OpenCV classifiers, so concurrent clients don't corrupt each other's work.
- `read_exact` / `write_exact` helpers handle partial reads and writes on sockets.
- Little-endian integer helpers handle the size fields in the protocol.
- Different exit codes cover argument errors, file problems, classifier loading, and port errors.

## AI assistance

Claude was used during development for help with the networking, threading and OpenCV parts of the assignment.
