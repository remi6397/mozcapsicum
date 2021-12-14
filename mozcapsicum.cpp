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

#include <map>
#include <vector>
#include <tuple>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <sys/syslimits.h>
#include <sys/capsicum.h>
#include <sys/un.h>

#include <fcntl.h>
#include <dlfcn.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mozcapsicum.h"

using mozilla::mozcapsicum::Rights;

static std::map<std::string, std::vector<char>> sysctl_by_name_cache;
static std::map<std::vector<int>, std::vector<char>> sysctl_by_mib_cache;
static std::map<dev_t, std::string> devname_cache;
static std::map<std::string, int> dir_fds;
static std::map<std::string, int> file_fds;

MOZ_CAPSICUM_EXPORT bool mozilla::mozcapsicum::precache_sysctl(
    std::string key) {
  size_t mib_len = 10;
  std::vector<int> mib(mib_len, 0);
  if (sysctlnametomib(key.c_str(), &mib[0], &mib_len) == -1) return false;
  mib.resize(mib_len);

  if (!mozilla::mozcapsicum::precache_sysctl_by_mib(mib)) return false;

  sysctl_by_name_cache[key] = sysctl_by_mib_cache[mib];
  return true;
}

MOZ_CAPSICUM_EXPORT bool mozilla::mozcapsicum::precache_sysctl_by_mib(
    std::vector<int> mib) {
  size_t len = 0;
  if (sysctl(&mib[0], mib.size(), nullptr, &len, nullptr, 0) == -1)
    return false;

  std::vector<char> val(len, 0);
  if (sysctl(&mib[0], mib.size(), &val[0], &len, nullptr, 0) == -1)
    return false;

  sysctl_by_mib_cache[mib] = val;
  return true;
}

MOZ_CAPSICUM_EXPORT bool mozilla::mozcapsicum::precache_devname(
    dev_t dev, mode_t type) {
  char dname[SPECNAMELEN];

  if (!devname_r(dev, type, dname, sizeof(dname))) {
    return false;
  }

  devname_cache[dev + type] = std::string(dname);
  return true;
}

static bool apply_rights(int fd, Rights rights) {
  cap_rights_t r;
  switch (rights) {
    case Rights::Unrestricted:
      return true;
    case Rights::DataFiles:
      // CAP_FSTATFS is mostly used by opendir
      cap_rights_init(&r, CAP_LOOKUP, CAP_READ, CAP_SEEK, CAP_MMAP, CAP_FSTAT,
                      CAP_FSTATFS, CAP_FCNTL);
      // Mesa does this on e.g. amdgpu.ids (not critical but lack of this causes
      // a warning)
      cap_fcntls_limit(fd, CAP_FCNTL_GETFL);
      break;
    case Rights::IPCSockets:
      // libpulse tries to mkdir the pulse dir before connecting,
      // so CAP_MKDIRAT is required for it to get EEXIST
      cap_rights_init(&r, CAP_LOOKUP, CAP_CONNECTAT, CAP_READ, CAP_WRITE,
                      CAP_SEEK, CAP_FSTAT, CAP_FSTATFS, CAP_MKDIRAT);
      break;
    case Rights::GPU:
      cap_rights_init(&r, CAP_LOOKUP, CAP_READ, CAP_WRITE, CAP_SEEK, CAP_MMAP,
                      CAP_IOCTL, CAP_FCNTL, CAP_FSTAT, CAP_FSTATFS);
      break;
  }
  return cap_rights_limit(fd, &r) >= 0;
}

MOZ_CAPSICUM_EXPORT bool mozilla::mozcapsicum::preopen_dir(std::string path,
                                                           Rights rights) {
  int fd = openat(AT_FDCWD, path.c_str(), O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) return false;
  if (!apply_rights(fd, rights)) {
    close(fd);
    return false;
  }
  dir_fds.emplace(path, fd);
  return true;
}

MOZ_CAPSICUM_EXPORT bool mozilla::mozcapsicum::preopen_file(std::string path,
                                                            int flags,
                                                            Rights rights) {
  int fd = openat(AT_FDCWD, path.c_str(), flags);
  if (fd < 0) return false;
  if (!apply_rights(fd, rights)) {
    close(fd);
    return false;
  }
  file_fds.emplace(path, fd);
  return true;
}

static inline bool ends_with(std::string const& value,
                             std::string const& ending) {
  if (ending.size() > value.size()) return false;
  return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

static inline bool starts_with(std::string const& value,
                               std::string const& starting) {
  if (starting.size() > value.size()) return false;
  return std::equal(starting.begin(), starting.end(), value.begin());
}

static std::pair<int, std::string> find_relative(std::string const& path) {
  for (auto val : dir_fds) {
    // Allow reopening preopened directories (e.g. /dev/dri for mesa)
    if (path == val.first) return std::make_pair(val.second, ".");
    std::string dirprefix = val.first;
    if (!ends_with(dirprefix, "/")) dirprefix += "/";
    if (starts_with(path, dirprefix)) {
      std::string relpath = path.substr(dirprefix.size());
      char link[PATH_MAX + 1] = {0};
      if (readlinkat(val.second, relpath.c_str(), link, PATH_MAX) != -1) {
        // We have to support relative links e.g. /dev/dri/card0 -> ../drm/0
        if (link[0] == '.' && link[1] == '.') {
          std::string dirpath = val.first;
          if (ends_with(dirprefix, "/"))
            dirpath = dirpath.substr(0, dirpath.size() - 1);
          return find_relative(dirpath.substr(0, dirpath.find_last_of('/')) +
                               std::string(link + 2));
        }
        return find_relative(std::string(link));
      }
      return std::make_pair(val.second, relpath);
    }
  }
  return std::make_pair(AT_FDCWD, path);
}

extern "C" {

#define FIND_RELATIVE(fd, relpath, path) \
  int fd = -1;                           \
  std::string relpath;                   \
  std::tie(fd, relpath) = find_relative(std::string(path));

int _open(const char* path, int flags, ...) {
  va_list args;
  va_start(args, flags);
  int mode = va_arg(args, int);

  // Preopened files are special: only used for opening.
  // Sadly, dup doesn't really "reopen" the file, but we don't need this much
  std::string s_path(path);
  if (file_fds.count(s_path)) {
    return dup(file_fds[s_path]);
  }

  FIND_RELATIVE(fd, relpath, path);
  return openat(fd, relpath.c_str(), flags, mode);
}

int open(const char* path, int flags, ...) {
  va_list args;
  va_start(args, flags);
  int mode = va_arg(args, int);
  return _open(path, flags, mode);
}

// Used by Mesa on /dev/dri
DIR* opendir(const char* filename) {
  return fdopendir(
      _open(filename, O_RDONLY | O_NONBLOCK | O_DIRECTORY | O_CLOEXEC));
}

// Used for dependentlibs.list, libdrm/amdgpu.ids, gtk immodules.cache
FILE* fopen(const char* __restrict file, const char* __restrict mode) {
  int f = -1, oflags = 0;
  if ((mozilla::mozcapsicum::__sflags(mode, &oflags)) == 0) return (NULL);
  if ((f = _open(file, oflags, DEFFILEMODE)) < 0) {
    return (NULL);
  }

  return fdopen(f, mode);
}

typedef int (*sysctlbyname_t)(const char*, void*, size_t*, const void*, size_t);
static sysctlbyname_t real_sysctlbyname = nullptr;

int sysctlbyname(const char* name, void* oldp, size_t* oldlenp,
                 const void* newp, size_t newlen) {
  if (!real_sysctlbyname)
    real_sysctlbyname = (sysctlbyname_t)dlsym(RTLD_NEXT, "sysctlbyname");
  if (newp == nullptr && name != nullptr) {
    std::string key(name);
    if (sysctl_by_name_cache.count(key)) {
      auto& val = sysctl_by_name_cache[key];
      if (oldp != nullptr)
        memcpy(oldp, &val[0], std::max(*oldlenp, val.size()));
      else
        *oldlenp = val.size();
      return 0;
    }
  }
  return real_sysctlbyname(name, oldp, oldlenp, newp, newlen);
}

typedef int (*sysctl_t)(const int*, unsigned int, void*, size_t*, const void*,
                        size_t);
static sysctl_t real_sysctl = nullptr;

int sysctl(const int* name, unsigned int namelen, void* oldp, size_t* oldlenp,
           const void* newp, size_t newlen) {
  if (!real_sysctl) real_sysctl = (sysctl_t)dlsym(RTLD_NEXT, "sysctl");
  if (real_sysctl(name, namelen, oldp, oldlenp, newp, newlen) == 0) return 0;
  if (newp == nullptr && name != nullptr && namelen > 1 && oldlenp != nullptr) {
    std::vector<int> key(name, name + namelen);
    if (sysctl_by_mib_cache.count(key)) {
      auto& val = sysctl_by_mib_cache[key];
      if (oldp != nullptr)
        memcpy(oldp, &val[0], std::max(*oldlenp, val.size()));
      else
        *oldlenp = val.size();
      return 0;
    }
  }
  errno = EPERM;
  return -1;
}

typedef char* (*devname_r_t)(dev_t, mode_t, char*, int);
static devname_r_t real_devname_r = nullptr;

char *
devname_r(dev_t dev, mode_t type, char *buf, int len)
{
  if (!real_devname_r) real_devname_r = (devname_r_t)dlsym(RTLD_NEXT, "devname_r");

  if (devname_cache.count(dev + type)) {
    strlcpy(buf, devname_cache[dev + type].c_str(), len);
    return buf;
  }

  return real_devname_r(dev, type, buf, len);
}

typedef void* (*dlopen_t)(const char*, int);
static dlopen_t real_dlopen = nullptr;

void* dlopen(const char* path, int mode) {
  if (!real_dlopen) real_dlopen = (dlopen_t)dlsym(RTLD_NEXT, "dlopen");
  // Handle special path we set for the Mesa driver loader - make it use
  // relative paths
  if (path != nullptr && path[0] == '*') return real_dlopen(path + 2, mode);
  return real_dlopen(path, mode);
}

int access(const char* path, int mode) {
  FIND_RELATIVE(fd, relpath, path);
  return faccessat(fd, relpath.c_str(), mode, 0);
}

int connect(int s, const struct sockaddr* name, socklen_t namelen) {
  if (name->sa_family == AF_UNIX) {
    struct sockaddr_un* usock = (struct sockaddr_un*)name;
    FIND_RELATIVE(fd, relpath, usock->sun_path);
    strlcpy(usock->sun_path, relpath.c_str(), sizeof(usock->sun_path));
    return connectat(fd, s, name, namelen);
  }

  return connectat(AT_FDCWD, s, name, namelen);
}

int eaccess(const char* path, int mode) {
  FIND_RELATIVE(fd, relpath, path);
  return faccessat(fd, relpath.c_str(), mode, 0);
}

int lstat(const char* path, struct stat* st) {
  FIND_RELATIVE(fd, relpath, path);
  return fstatat(fd, relpath.c_str(), st, 0);
}

int stat(const char* path, struct stat* st) { return lstat(path, st); }

int mkdir(const char* path, mode_t mode) {
  FIND_RELATIVE(fd, relpath, path);
  return mkdirat(fd, relpath.c_str(), mode);
}

int unlink(const char* path) {
  FIND_RELATIVE(fd, relpath, path);
  return unlinkat(fd, relpath.c_str(), 0);
}
}
