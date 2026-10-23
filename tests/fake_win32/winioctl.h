#ifndef TEST_WINIOCTL_H
#define TEST_WINIOCTL_H
#include "windows.h"
#define IOCTL_DISK_GET_DRIVE_GEOMETRY 1
#define IOCTL_DISK_GET_LENGTH_INFO 2
#define IOCTL_DISK_GET_DRIVE_LAYOUT_EX 3
#define IOCTL_STORAGE_GET_DEVICE_NUMBER 4
#define PARTITION_STYLE_GPT 1
#define PARTITION_STYLE_MBR 0
typedef struct { DWORD BytesPerSector; } DISK_GEOMETRY;
typedef struct { LARGE_INTEGER Length; } GET_LENGTH_INFORMATION;
typedef struct { DWORD DeviceType, DeviceNumber, PartitionNumber; } STORAGE_DEVICE_NUMBER;
typedef struct {
    DWORD PartitionStyle;
    LARGE_INTEGER PartitionLength;
    DWORD PartitionNumber;
    struct { GUID PartitionType, PartitionId; } Gpt;
} PARTITION_INFORMATION_EX;
typedef struct {
    DWORD PartitionStyle, PartitionCount;
    PARTITION_INFORMATION_EX PartitionEntry[1];
} DRIVE_LAYOUT_INFORMATION_EX;
#endif
