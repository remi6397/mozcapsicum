/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

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
