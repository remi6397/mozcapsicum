/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This is free and unencumbered software released into the public domain.
 * 
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 * 
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 * 
 * For more information, please refer to <http://unlicense.org/>
 */

#pragma once
#include <string>
#include <sys/types.h>

#define MOZ_CAPSICUM_EXPORT __attribute__((weak, visibility("default")))

namespace mozilla {
namespace mozcapsicum {

enum class Rights { Unrestricted, DataFiles, IPCSockets, GPU };

MOZ_CAPSICUM_EXPORT bool precache_sysctl(std::string key);
MOZ_CAPSICUM_EXPORT bool precache_sysctl_by_mib(std::vector<int> key);
MOZ_CAPSICUM_EXPORT bool precache_devname(dev_t key, mode_t mode);
MOZ_CAPSICUM_EXPORT bool preopen_dir(std::string path,
                                     Rights rights = Rights::Unrestricted);
MOZ_CAPSICUM_EXPORT bool preopen_file(std::string path, int flags,
                                      Rights rights = Rights::Unrestricted);

int __sflags(const char* mode, int* optr);

}  // namespace mozilla::mozcapsicum
}  // namespace
