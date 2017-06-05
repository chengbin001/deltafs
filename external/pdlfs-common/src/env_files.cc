/*
 * Copyright (c) 2015-2017 Carnegie Mellon University.
 *
 * All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file. See the AUTHORS file for names of contributors.
 */

#include "pdlfs-common/env_files.h"
#include "pdlfs-common/mutexlock.h"
#include "pdlfs-common/port.h"

#if __cplusplus >= 201103L
#include <atomic>
#endif

namespace pdlfs {

Status WholeFileBufferedRandomAccessFile::Load() {
  Status status;
  assert(base_ != NULL);
  buf_size_ = 0;
  while (buf_size_ < max_buf_size_) {  // Reload until we reach max_buf_size_
    size_t n = io_size_;
    if (n > max_buf_size_ - buf_size_) {
      n = max_buf_size_ - buf_size_;
    }
    Slice sli;
    char* p = buf_ + buf_size_;
    status = base_->Read(n, &sli, p);
    if (!status.ok()) break;  // Error
    if (sli.empty()) break;   // EOF
    if (sli.data() != p) {
      // File implementation gave us pointer to some other data.
      // Explicitly copy it into our buffer.
      memcpy(p, sli.data(), sli.size());
    }
    buf_size_ += sli.size();
  }

  delete base_;
  base_ = NULL;
  return status;
}

#if __cplusplus >= 201103L
struct AtomicMeasuredRandomAccessFile::Rep {
  Rep() : bytes(0), ops(0) {}
  std::atomic_uint_fast64_t bytes;
  std::atomic_uint_fast64_t ops;

  void AcceptRead(uint64_t n) {
    bytes += n;
    ops += 1;
  }
};
#else
struct AtomicMeasuredRandomAccessFile::Rep {
  Rep() : bytes(0), ops(0) {}
  port::Mutex mutex;
  uint64_t bytes;
  uint64_t ops;

  void AcceptRead(uint64_t n) {
    MutexLock ml(&mutex);
    bytes += n;
    ops += 1;
  }
};
#endif

uint64_t AtomicMeasuredRandomAccessFile::TotalBytes() const {
  return static_cast<uint64_t>(rep_->bytes);
}

uint64_t AtomicMeasuredRandomAccessFile::TotalOps() const {
  return static_cast<uint64_t>(rep_->ops);
}

Status AtomicMeasuredRandomAccessFile::Read(uint64_t offset, size_t n,
                                            Slice* result,
                                            char* scratch) const {
  Status status = base_->Read(offset, n, result, scratch);
  if (status.ok()) {
    rep_->AcceptRead(result->size());
    return status;
  } else {
    return status;
  }
}

AtomicMeasuredRandomAccessFile::~AtomicMeasuredRandomAccessFile() {
  delete rep_;
}

AtomicMeasuredRandomAccessFile::AtomicMeasuredRandomAccessFile(
    RandomAccessFile* base)
    : base_(base) {
  rep_ = new Rep;
}

}  // namespace pdlfs