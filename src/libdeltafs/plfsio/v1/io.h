/*
 * Copyright (c) 2019 Carnegie Mellon University,
 * Copyright (c) 2019 Triad National Security, LLC, as operator of
 *     Los Alamos National Laboratory.
 *
 * All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file. See the AUTHORS file for names of contributors.
 */

#pragma once

#include "pdlfs-common/env.h"
#include "pdlfs-common/env_files.h"
#include "pdlfs-common/port.h"

#include <map>
#include <string>

// This module provides the abstraction for accessing data stored in
// an underlying storage using a log-structured format. Data is written,
// append-only, into a "sink", and is read from a "source".

namespace pdlfs {
namespace plfsio {

// Log types
enum LogType {
  // Default I/O type, for data blocks
  kDefIoType = 0x00,  // Optimized for random read accesses

  // For index logs consisting of table indexes, filters, and other index blocks
  kIdxIoType = 0x01  // Sequential reads expected
};

// Log rotation types.
// Store logs as separated files.
enum RotationType {
  // Do not rotate log files
  kNoRotation = 0x00,
  // Log rotation is controlled by external user code
  kRotationExtCtrl = 0x01
};

// Store a sequential log in multiple pieces.
class RollingLogFile;

// Abstraction for writing data to storage.
// Implementation is not thread-safe. External synchronization is needed for
// multi-threaded access.
class LogSink {
 public:
  // Options for monitoring, naming, write buffering,
  // and file rotation.
  struct LogOptions {
    LogOptions();

    // Rank # of the calling process
    int rank;

    // Sub-partition index # of the log
    // Set to "-1" to indicate there is no sub-partitions
    int sub_partition;

    // Max write buffering in bytes
    // Set to "0" to disable
    size_t max_buf;

    // Min write buffering in bytes
    // Set to "0" to disable
    size_t min_buf;

    // Log rotation
    RotationType rotation;

    // Type of the log
    LogType type;

    // Allow synchronization among multiple threads
    port::Mutex* mu;

    // Enable i/o stats monitoring
    WritableFileStats* stats;

    // Low-level storage abstraction
    Env* env;
  };

 private:
  LogSink(const LogOptions& opts, const std::string& p,  // Parent directory
          SynchronizableFile* buf, RollingLogFile* vf)
      : opts_(opts),
        prefix_(p),
        buf_file_(buf),
        rlog_(vf),
        mu_(opts_.mu),
        env_(opts_.env),
        buf_store_(NULL),
        buf_memory_usage_(0),
        prev_off_(0),
        off_(0),
        file_(NULL),  // Initialized by Open()
        refs_(0) {}

 public:
  // Create a log sink instance for writing data according to the given set of
  // options. Return OK on success, or a non-OK status on errors.
  static Status Open(const LogOptions& opts, const std::string& prefix,
                     LogSink** result);

  // Return the current logic write offset.
  // May be called after Lclose().
  uint64_t Ltell() const {
    if (mu_ != NULL) mu_->AssertHeld();
    return off_;
  }

  void Lock() {
    if (mu_ != NULL) {
      mu_->Lock();
    }
  }

  // Append data into the storage.
  // Return OK on success, or a non-OK status on errors.
  // May lose data until the next Lsync().
  // REQUIRES: Lclose() has not been called.
  Status Lwrite(const Slice& data) {
    if (file_ == NULL) {
      return Status::Disconnected("Log already closed", filename_);
    } else {
      if (mu_ != NULL) {
        mu_->AssertHeld();
      }
      Status result = file_->Append(data);
      if (result.ok()) {
        // File implementation may ignore the flush
        result = file_->Flush();
        if (result.ok()) {
          off_ += data.size();
        }
      }
      return result;
    }
  }

  // Force data to be written to storage.
  // Return OK on success, or a non-OK status on errors.
  // Data previously buffered will be forcefully flushed out.
  // REQUIRES: Lclose() has not been called.
  Status Lsync() {
    if (file_ == NULL) {
      return Status::Disconnected("Log already closed", filename_);
    } else {
      if (mu_ != NULL) mu_->AssertHeld();
      return file_->Sync();
    }
  }

  void Unlock() {
    if (mu_ != NULL) {
      mu_->Unlock();
    }
  }

  // Return the memory space for write buffering.
  size_t memory_usage() {
    if (file_ != NULL) {
      return (buf_store_ != NULL ? buf_store_->capacity() : 0);
    } else {
      return buf_memory_usage_;
    }
  }

  // Close the log so no further writes will be accepted.
  // Return OK on success, or a non-OK status on errors.
  // If sync is set to true, will force data sync before closing the log.
  Status Lclose(bool sync = false);
  // Flush and close the current log file and redirect
  // all future writes to a new log file.
  Status Lrotate(int index, bool sync = false);
  uint64_t Ptell() const;  // Return the current physical log offset
  void Ref() { refs_++; }
  void Unref();

 private:
  ~LogSink();  // Triggers Finish()
  // No copying allowed
  void operator=(const LogSink& s);
  LogSink(const LogSink&);
  // Invoked by Lclose() and the class destructor
  Status Finish();

  // Constant after construction
  const LogOptions opts_;
  const std::string prefix_;  // Parent directory name
  // NULL if write buffering is disabled
  SynchronizableFile* const buf_file_;  // Not valid after Finish()
  // NULL if log rotation is disabled
  RollingLogFile* const rlog_;  // Not Valid after Finish()
  port::Mutex* const mu_;
  Env* const env_;

  // State below is protected by mu_
  std::string* buf_store_;  // NULL after Finish() is called
  size_t buf_memory_usage_;
  Status finish_status_;
  uint64_t prev_off_;
  uint64_t off_;  // Logic write offset, monotonically increasing
  // NULL after Finish() is called
  WritableFile* file_;
  std::string filename_;  // Name of the current log file
  uint32_t refs_;
};

// Abstraction for reading data from a log file, which may
// consist of several pieces due to log rotation.
class LogSource {
 public:
  // Options for naming, pre-fetching, and monitoring.
  struct LogOptions {
    LogOptions();

    // Rank # of the calling process
    int rank;

    // Sub-partition index # of the log.
    // Set to "-1" to indicate there is no sub-partitions
    int sub_partition;

    // Number of log rotations performed.
    // Set to "-1" to indicate the log was never rotated
    int num_rotas;

    // Type of the log.
    // For index logs, the entire log data will be eagerly fetched
    // and cached in memory
    LogType type;

    // For i/o stats monitoring (sequential reads)
    SequentialFileStats* seq_stats;

    // For i/o stats monitoring
    RandomAccessFileStats* stats;

    // Bulk read size
    size_t io_size;

    // Low-level storage abstraction
    Env* env;
  };

  // Create a log source instance for reading data according to a given set of
  // options. Return OK on success, or a non-OK status on errors.
  static Status Open(const LogOptions& opts, const std::string& prefix,
                     LogSource** result);

  Status Read(uint64_t offset, size_t n, Slice* result, char* scratch,
              size_t index = 0) {
    Status status;
    if (index < num_files_) {
      RandomAccessFile* const f = files_[index].first;
      status = f->Read(offset, n, result, scratch);  // May return cached data
    } else {
      *result = Slice();  // Return empty data
    }
    return status;
  }

  // Return the size of a given file
  uint64_t Size(size_t index = 0) const {
    if (index < num_files_) {
      return files_[index].second;
    } else {
      return 0;
    }
  }

  size_t LastFileIndex() const {
    if (num_files_ == 0) {
      return ~static_cast<size_t>(0);  // Invalid
    } else {
      return num_files_ - 1;
    }
  }

  // Return accumulated file size (total data size)
  uint64_t TotalSize() const {
    uint64_t result = 0;
    for (size_t i = 0; i < num_files_; i++) {
      result += files_[i].second;
    }
    return result;
  }

  void Ref() { refs_++; }
  void Unref();

 private:
  LogSource(const LogOptions& opts, const std::string& p)
      : opts_(opts), prefix_(p), files_(NULL), num_files_(0), refs_(0) {}
  ~LogSource();
  // No copying allowed
  void operator=(const LogSource& s);
  LogSource(const LogSource&);

  // Constant after construction
  const LogOptions opts_;
  const std::string prefix_;  // Parent directory name
  std::pair<RandomAccessFile*, uint64_t>* files_;
  size_t num_files_;
  uint32_t refs_;
};

}  // namespace plfsio
}  // namespace pdlfs
