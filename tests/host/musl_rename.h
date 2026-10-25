/*
 * musl_rename.h
 *
 * Force-included into the musl math sources the native host build compiles
 * (see CMakeLists.txt): the libm functions the LXR firmware calls at run time,
 * from the same musl sources Emscripten links into the browser build, so the
 * host computes them identically. macOS libm rounds some results differently,
 * which made 0.4 % of samples differ by up to 2 LSB between the two builds.
 *
 * They are renamed om_* so they cannot clash with the system libm; host_port.h
 * points the firmware's calls at them. musl is MIT licensed.
 */
#ifndef MUSL_RENAME_H_
#define MUSL_RENAME_H_

#include "features.h"  /* hidden, weak_alias: musl's own build includes it first */

#define sinf om_sinf
#define cosf om_cosf
#define expf om_expf
#define exp om_exp
#define logf om_logf

#endif /* MUSL_RENAME_H_ */
