#ifndef BASE_BUILD_MACROS_H
#define BASE_BUILD_MACROS_H

// A set of macros to use for platform detection.
#if defined(__native_client__)
// __native_client__ must be first, so that other OS_ defines are not set.
#define __OS_NACL__ 1
#elif defined(ANDROID)
#define __OS_ANDROID__ 1
#elif defined(__APPLE__)
// Only include TargetConditionals after testing ANDROID as some Android builds
// on the Mac have this header available and it's not needed unless the target
// is really an Apple platform.
#include <TargetConditionals.h>
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#define __OS_IOS__ 1
// Catalyst is the technology that allows running iOS apps on macOS. These
// builds are both OS_IOS and OS_IOS_MACCATALYST.
#if defined(TARGET_OS_MACCATALYST) && TARGET_OS_MACCATALYST
#define __OS_IOS_MACCATALYST__
#endif  // defined(TARGET_OS_MACCATALYST) && TARGET_OS_MACCATALYST
#else
#define __OS_MAC__ 1
#endif  // defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#elif defined(__linux__)
#if !defined(OS_CHROMEOS)
// Do not define OS_LINUX on Chrome OS build.
// The OS_CHROMEOS macro is defined in GN.
#define __OS_LINUX__ 1
#endif  // !defined(OS_CHROMEOS)
// Include a system header to pull in features.h for glibc/uclibc macros.
#include <assert.h>
#if defined(__GLIBC__) && !defined(__UCLIBC__)
// We really are using glibc, not uClibc pretending to be glibc.
#define LIBC_GLIBC 1
#endif
#elif defined(_WIN32)
#define __OS_WIN__ 1
#elif defined(__Fuchsia__)
#define __OS_FUCHSIA__ 1
#elif defined(__FreeBSD__)
#define __OS_FREEBSD__ 1
#elif defined(__NetBSD__)
#define __OS_NETBSD__ 1
#elif defined(__OpenBSD__)
#define __OS_OPENBSD__ 1
#elif defined(__sun)
#define __OS_SOLARIS__ 1
#elif defined(__QNXNTO__)
#define __OS_QNX__ 1
#elif defined(_AIX)
#define __OS_AIX__ 1
#elif defined(__asmjs__) || defined(__wasm__)
#define __OS_ASMJS__ 1
#elif defined(__MVS__)
#define __OS_ZOS__ 1
#else
#error Please add support for your platform in build/build_config.h
#endif
// NOTE: Adding a new port? Please follow
// https://chromium.googlesource.com/chromium/src/+/main/docs/new_port_policy.md

#if defined(__OS_MAC__) || defined(__OS_IOS__)
#define __OS_APPLE__ 1
#endif

// For access to standard BSD features, use OS_BSD instead of a
// more specific macro.
#if defined(__OS_FREEBSD__) || defined(__OS_NETBSD__) || defined(__OS_OPENBSD__)
#define __OS_BSD__ 1
#endif

// For access to standard POSIXish features, use OS_POSIX instead of a
// more specific macro.
#if defined(__OS_AIX__) || defined(__OS_ANDROID__) || defined(__OS_ASMJS__) || \
    defined(__OS_FREEBSD__) || defined(__OS_IOS__) || defined(__OS_LINUX__) || \
    defined(__OS_CHROMEOS__) || defined(__OS_MAC__) || defined(__OS_NACL__) || \
    defined(__OS_NETBSD__) || defined(__OS_OPENBSD__) ||                       \
    defined(__OS_QNX__) || defined(__OS_SOLARIS__) || defined(__OS_ZOS__)
#define __OS_POSIX__ 1
#endif

#endif
