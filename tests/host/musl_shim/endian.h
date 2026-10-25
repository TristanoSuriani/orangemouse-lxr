/* Shim: musl's libm.h only needs the byte order (little-endian hosts only). */
#ifndef ENDIAN_SHIM_H
#define ENDIAN_SHIM_H
#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN 4321
#define __BYTE_ORDER __LITTLE_ENDIAN
#endif
