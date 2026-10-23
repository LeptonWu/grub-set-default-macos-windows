/* Minimal test-only Win32 declarations. Never included by the Windows build. */
#ifndef TEST_WIN32_H
#define TEST_WIN32_H
#include <stddef.h>
#include <stdint.h>
typedef void *HANDLE;
typedef uint32_t DWORD;
typedef struct {
    uint32_t Data1;
    uint16_t Data2, Data3;
    unsigned char Data4[8];
} GUID;
typedef int BOOL;
typedef int64_t LONGLONG;
typedef struct { LONGLONG QuadPart; } LARGE_INTEGER;
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define GENERIC_READ 1
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define OPEN_EXISTING 3
#define FILE_ATTRIBUTE_NORMAL 0
#define FILE_FLAG_NO_BUFFERING 4
#define FILE_BEGIN 0
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_INVALID_DATA 13
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_MORE_DATA 234
#define ERROR_NO_MORE_ITEMS 259
#define ERROR_NOT_READY 21
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_DEVICE_NOT_CONNECTED 1167
#define ERROR_ACCESS_DENIED 5
DWORD GetLastError(void);
void SetLastError(DWORD);
HANDLE CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
BOOL CloseHandle(HANDLE);
BOOL SetFilePointerEx(HANDLE, LARGE_INTEGER, LARGE_INTEGER *, DWORD);
BOOL ReadFile(HANDLE, void *, DWORD, DWORD *, void *);
BOOL GetFileSizeEx(HANDLE, LARGE_INTEGER *);
BOOL DeviceIoControl(HANDLE, DWORD, void *, DWORD, void *, DWORD, DWORD *, void *);
void *_aligned_malloc(size_t, size_t);
void _aligned_free(void *);
#endif
