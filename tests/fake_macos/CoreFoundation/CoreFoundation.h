/* Minimal CoreFoundation declarations for host-side disk-list mapping tests. */
#ifndef TEST_COREFOUNDATION_H
#define TEST_COREFOUNDATION_H
#include <stddef.h>
typedef const void *CFTypeRef;
typedef const void *CFDictionaryRef;
typedef const void *CFArrayRef;
typedef const void *CFStringRef;
typedef long CFIndex;
typedef unsigned long CFTypeID;
#define CFSTR(text) ((CFStringRef)(text))
#define kCFStringEncodingUTF8 1
CFTypeID CFGetTypeID(CFTypeRef value);
CFTypeID CFDictionaryGetTypeID(void);
CFTypeID CFArrayGetTypeID(void);
CFTypeID CFStringGetTypeID(void);
CFTypeRef CFDictionaryGetValue(CFDictionaryRef dict, const void *key);
CFIndex CFArrayGetCount(CFArrayRef array);
const void *CFArrayGetValueAtIndex(CFArrayRef array, CFIndex index);
int CFStringGetCString(CFStringRef string, char *buffer, CFIndex size, unsigned int encoding);
#endif
