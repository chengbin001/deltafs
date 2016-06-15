#pragma once

/*
 * Copyright (c) 2014-2016 Carnegie Mellon University.
 *
 * All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file. See the AUTHORS file for names of contributors.
 */

#include <stdint.h>

#include "pdlfs-common/slice.h"

// We currently do not support dynamic changes of the total number of
// servers. However, we do support restarting indexfs with distinct sets
// of servers. For example, one can start an indexfs cluster using an initial
// set of servers, stop the cluster, and then restart it using a different
// set (and number) of servers. Indexfs will rebalance itself so that each new
// indexfs server will get an equal load compared to other indexfs servers.
namespace pdlfs {

// Common options shared among all directory indices.
struct DirIndexOptions {
  // The number of physical servers.
  // This option can change between indexfs restarts.
  // There is no default value.
  // Valid values are [1, 65536]
  int num_servers;

  // The number of virtual servers.
  // This option cannot change between indexfs restarts.
  // There is no default value.
  // Valid values are [num_servers, 65536]
  int num_virtual_servers;

  // If true, the implementation will do aggressive checking of the
  // data it is processing and will stop early if it detects any
  // errors.
  // Default: false
  bool paranoid_checks;

  DirIndexOptions() : paranoid_checks(false) {}
};

class DirIndex {
 public:
  // Create a new index using the specified settings.
  DirIndex(int64_t dir, int16_t server, const DirIndexOptions* options);
  ~DirIndex();

  // Discard the current index and override it with another index image.
  bool TEST_Reset(const Slice& other);

  // Update the index by merging another index of the same directory.
  bool Update(const Slice& other);

  // Update the index by merging another index of the same directory.
  bool Update(const DirIndex& other);

  // Return the server responsible for the given partition.
  int GetServerForIndex(int index) const;

  // Return the partition responsible for the given file.
  int GetIndex(const std::string& name) const;

  // Return the server responsible for the given file.
  int SelectServer(const std::string& name) const;

  // Return true iff the bit is set.
  bool GetBit(int index) const;

  // Set the bit at the given index.
  void SetBit(int index);

  // Clear the bit at the given index.
  void TEST_UnsetBit(int index);

  // Revert all bits and roll back to the initial state.
  void TEST_RevertAll();

  // Return true if the given partition can be further divided.
  bool IsSplittable(int index) const;

  // Return the next child partition for the given parent partition.
  int NewIndexForSplitting(int index) const;

  // Return the INODE number of the directory being indexed.
  int64_t FetchDirId() const;

  // Return the zeroth server of the directory being indexed.
  int16_t FetchZerothServer() const;

  // Return the internal bitmap radix of the index.
  int FetchBitmapRadix() const;

  // Return the in-memory representation of this index.
  Slice ToSlice() const;

  // Return true if the given hash will belong to the given child partition.
  static bool ToBeMigrated(int index, const char* hash);

  // Return the hash value of the given file.
  static size_t GetNameHash(const std::string& name, char* hash);

  // Return the server responsible for a given index.
  static int MapIndexToServer(int index, int zeroth_server, int num_servers);

 private:
  struct Ref;
  static bool ParseDirIndex(const Slice& input, bool checks, Ref* ref);
  const DirIndexOptions* options_;
  struct Rep;
  Rep* rep_;

  // No copying allowed
  DirIndex(const DirIndex&);
  void operator=(const DirIndex&);
};

}  // namespace pdlfs
