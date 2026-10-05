CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=gnu99 
OPENCV_CFLAGS = -L/usr/lib64 -lopencv_core -lopencv_imgcodecs -lopencv_objdetect -lopencv_imgproc -lpthread
LDFLAGS =

all: uqfaceclient uqfacedetect

uqfaceclient: client.c
	$(CC) -o uqfaceclient $(CFLAGS) client.c
uqfacedetect: detect.c
	$(CC) -o uqfacedetect $(CFLAGS) $(OPENCV_CFLAGS) detect.c
