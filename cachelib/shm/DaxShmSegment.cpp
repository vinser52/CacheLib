/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cachelib/shm/DaxShmSegment.h"

#include <fcntl.h>
#include <folly/logging/xlog.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "cachelib/common/Utils.h"

namespace facebook {
namespace cachelib {

DaxShmSegment::DaxShmSegment(ShmAttachT,
                                 const std::string& name,
                                 ShmSegmentOpts opts)
    : ShmBase(std::move(opts), name),
      fd_(getExisting(getPath(), opts_)) {
  XDCHECK_NE(fd_, kInvalidFD);
  markActive();
  createReferenceMapping();
}

DaxShmSegment::DaxShmSegment(ShmNewT,
                                 const std::string& name,
                                 size_t size,
                                 ShmSegmentOpts opts)
    : ShmBase(std::move(opts), name),
      fd_(getExisting(getPath(), opts_)), size_(size) {
  markActive();
  XDCHECK(isActive());
  XDCHECK_NE(fd_, kInvalidFD);
  XDCHECK(getSize(), size);
  // this ensures that the segment lives while the object lives.
  createReferenceMapping();
}

DaxShmSegment::~DaxShmSegment() {
  try {
    // delete the reference mapping so the segment can be deleted if its
    // marked to be.
    deleteReferenceMapping();
  } catch (const std::system_error& e) {
  }

  // need to close the fd without throwing any exceptions. so we call close
  // directly.
  if (fd_ != kInvalidFD) {
    const int ret = close(fd_);
    if (ret != 0) {
      XDCHECK_NE(errno, EIO);
      XDCHECK_NE(errno, EINTR);
      XDCHECK_EQ(errno, EBADF);
      XDCHECK(!errno);
    }
  }
}

int DaxShmSegment::createNewSegment(const std::string& name) {
  constexpr static int createFlags = O_RDWR | O_CREAT | O_EXCL;
  detail::open_func_t open_func = std::bind(open, name.c_str(), createFlags);
  return detail::openImpl(open_func, createFlags);
}

int DaxShmSegment::getExisting(const std::string& name,
                                 const ShmSegmentOpts& opts) {
  int flags = opts.readOnly ? O_RDONLY : O_RDWR;
  detail::open_func_t open_func = std::bind(open, name.c_str(), flags);
  return detail::openImpl(open_func, flags);
}

void DaxShmSegment::markForRemoval() {
  if (isActive()) {
    // we still have the fd open. so we can use it to perform ftruncate
    // even after marking for removal through unlink. The fd does not get
    // recycled until we actually destroy this object.
    removeByPath(getPath());
    markForRemove();
  } else {
    XDCHECK(false);
  }
}

bool DaxShmSegment::removeByPath(const std::string& path) {
  return true;
}

std::string DaxShmSegment::getPath() const {
  return std::get<DaxShmSegmentOpts>(opts_.typeOpts).path;
}

size_t DaxShmSegment::getSize() const {
  if (isActive() || isMarkedForRemoval()) {
    return size_;
  } else {
    throw std::runtime_error(folly::sformat(
        "Trying to get size of segment with name {} in an invalid state",
        getName()));
  }
  return 0;
}

void* DaxShmSegment::mapAddress(void* addr) const {
  size_t size = getSize();
  if (!detail::isPageAlignedSize(size, opts_.pageSize) ||
      !detail::isPageAlignedAddr(addr, opts_.pageSize)) {
    util::throwSystemError(EINVAL, "Address/size not aligned");
  }

  int flags = MAP_SHARED;

  // If users pass in an address, they must make sure that address is unused.
  if (addr != nullptr) {
    flags |= MAP_FIXED;
  }

  const int prot = opts_.readOnly ? PROT_READ : PROT_WRITE | PROT_READ;

  void* retAddr = detail::mmapImpl(addr, size, prot, flags, fd_, 0);
  // if there was hint for mapping, then fail if we cannot respect this
  // because we want to be specific about mapping to exactly that address.
  if (retAddr != nullptr && addr != nullptr && retAddr != addr) {
    util::throwSystemError(EINVAL, "Address already mapped");
  }
  XDCHECK(retAddr == addr || addr == nullptr);

  return retAddr;
}

void DaxShmSegment::unMap(void* addr) const {
  detail::munmapImpl(addr, getSize());
}

void DaxShmSegment::createReferenceMapping() {
  // create a mapping that lasts the life of this object. mprotect it to
  // ensure there are no actual accesses.
  referenceMapping_ = detail::mmapImpl(
      nullptr, detail::getPageSize(opts_.pageSize), PROT_NONE, MAP_SHARED, fd_, 0);
  XDCHECK(referenceMapping_ != nullptr);
}

void DaxShmSegment::deleteReferenceMapping() const {
  if (referenceMapping_ != nullptr) {
    detail::munmapImpl(referenceMapping_, detail::getPageSize());
  }
}
} // namespace cachelib
} // namespace facebook
