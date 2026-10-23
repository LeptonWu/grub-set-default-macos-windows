#ifndef TEST_SETUPAPI_H
#define TEST_SETUPAPI_H
#include "windows.h"
typedef HANDLE HDEVINFO;
typedef struct { DWORD cbSize; DWORD index; } SP_DEVICE_INTERFACE_DATA;
typedef struct { DWORD cbSize; char DevicePath[1]; } SP_DEVICE_INTERFACE_DETAIL_DATA_A;
#define DIGCF_PRESENT 2
#define DIGCF_DEVICEINTERFACE 16
HDEVINFO SetupDiGetClassDevsA(const GUID *, const char *, void *, DWORD);
BOOL SetupDiEnumDeviceInterfaces(HDEVINFO, void *, const GUID *, DWORD, SP_DEVICE_INTERFACE_DATA *);
BOOL SetupDiGetDeviceInterfaceDetailA(HDEVINFO, SP_DEVICE_INTERFACE_DATA *,
                                     SP_DEVICE_INTERFACE_DETAIL_DATA_A *, DWORD, DWORD *, void *);
BOOL SetupDiDestroyDeviceInfoList(HDEVINFO);
#endif
