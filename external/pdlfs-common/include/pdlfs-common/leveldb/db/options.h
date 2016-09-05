#pragma once

/*
 * Copyright (c) 2011 The LevelDB Authors.
 * Copyright (c) 2015-2016 Carnegie Mellon University.
 *
 * All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file. See the AUTHORS file for names of contributors.
 */

#include <stddef.h>

#include "pdlfs-common/leveldb/db/dbformat.h"

namespace pdlfs {

class Cache;
class Comparator;
class Env;
class FilterPolicy;
class Logger;
class Snapshot;

// DB contents are stored in a set of blocks, each of which holds a
// sequence of key,value pairs.  Each block may be compressed before
// being stored in a file.  The following enum describes which
// compression method (if any) is used to compress a block.
enum CompressionType {
  // NOTE: do not change the values of existing entries, as these are
  // part of the persistent format on disk.
  kNoCompression = 0x0,
  kSnappyCompression = 0x1
};

// Options to control the behavior of a database (passed to DB::Open)
struct DBOptions {
  // -------------------
  // Parameters that affect behavior

  // Comparator used to define the order of keys in the table.
  // Default: a comparator that uses lexicographic byte-wise ordering
  //
  // REQUIRES: The client must ensure that the comparator supplied
  // here has the same name and orders keys *exactly* the same as the
  // comparator provided to previous open calls on the same DB.
  const Comparator* comparator;

  // If true, the database will be created if it is missing.
  // Default: false
  bool create_if_missing;

  // If true, an error is raised if the database already exists.
  // Default: false
  bool error_if_exists;

  // If true, the implementation will do aggressive checking of the
  // data it is processing and will stop early if it detects any
  // errors.  This may have unforeseen ramifications: for example, a
  // corruption of one DB entry may cause a large number of entries to
  // become unreadable or for the entire DB to become unopenable.
  // Default: false
  bool paranoid_checks;

  // Use the specified object to interact with the environment,
  // e.g. to read/write files, schedule background work, etc.
  // Default: Env::Default()
  Env* env;

  // Any internal progress/error information generated by the db will
  // be written to info_log if it is non-NULL, or to a file stored
  // in the same directory as the DB contents if info_log is NULL.
  // Default: NULL
  Logger* info_log;

  // -------------------
  // Parameters that affect performance

  // Amount of data to build up in memory (backed by an unsorted log
  // on disk) before converting to a sorted on-disk file.
  //
  // Larger values increase performance, especially during bulk loads.
  // Up to two write buffers may be held in memory at the same time,
  // so you may wish to adjust this parameter to control memory usage.
  // Also, a larger write buffer will result in a longer recovery time
  // the next time the database is opened.
  //
  // Default: 4MB
  size_t write_buffer_size;

  // Control over open tables (max number of tables that can be opened).
  // You may need to increase this if your database has a large working set (
  // budget one open file per 2MB of working set).
  //
  // If non-NULL, use the specified cache for tables.
  // If NULL, leveldb will automatically create and use an internal cache
  // capable of holding 1000 tables.
  // Default: NULL
  Cache* table_cache;

  // Control over blocks (user data is stored in a set of blocks, and
  // a block is the unit of reading from disk).

  // If non-NULL, use the specified cache for blocks.
  // If NULL, leveldb will automatically create and use an 8MB internal cache.
  // Default: NULL
  Cache* block_cache;

  // Approximate size of user data packed per block.  Note that the
  // block size specified here corresponds to uncompressed data.  The
  // actual size of the unit read from disk may be smaller if
  // compression is enabled.  This parameter can be changed dynamically.
  //
  // Default: 4K
  size_t block_size;

  // Number of keys between restart points for delta encoding of keys.
  // This parameter can be changed dynamically.  Most clients should
  // leave this parameter alone.
  //
  // Default: 16
  int block_restart_interval;

  // Compress blocks using the specified compression algorithm.  This
  // parameter can be changed dynamically.
  //
  // Default: kSnappyCompression, which gives lightweight but fast
  // compression.
  //
  // Typical speeds of kSnappyCompression on an Intel(R) Core(TM)2 2.4GHz:
  //    ~200-500MB/s compression
  //    ~400-800MB/s decompression
  // Note that these speeds are significantly faster than most
  // persistent storage speeds, and therefore it is typically never
  // worth switching to kNoCompression.  Even if the input data is
  // incompressible, the kSnappyCompression implementation will
  // efficiently detect that and will switch to uncompressed mode.
  CompressionType compression;

  // If non-NULL, use the specified filter policy to reduce disk reads.
  // Many applications will benefit from passing the result of
  // NewBloomFilterPolicy() here.
  //
  // Default: NULL
  const FilterPolicy* filter_policy;

  // -------------------
  // Dangerous zone - parameters for experts

  // Set to true to skip garbage collection at the end of each compaction run.
  // Default: false
  bool gc_skip_deletion;

  // If true, no background compaction will be performed except for
  // those triggered by MemTable dumps.
  // All Tables will stay in Level-0 forever.
  // This facilitates fast insertion speed at the expense of read performance.
  // Default: false
  bool disable_compaction;

  // If true, compaction is no longer triggered by reads that have looked
  // multiple Tables at different levels.
  // In other words, all compaction jobs are direct results of insertions that
  // make some levels to contain too many Table files.
  // Default: false
  bool disable_seek_compaction;

  // The targeted size of each Table file before compression.
  // Default: 2MB
  size_t table_file_size;

  // The size ratio between two consecutive levels.
  // Default: 10
  int level_factor;

  // Number of files in Level-1 until compaction starts.
  // Default: 5
  int l1_compaction_trigger;

  // Number of files in Level-0 until compaction starts.
  // Default: 4
  int l0_compaction_trigger;

  // Number of files in Level-0 until writes are slowed down.
  // Default: 8
  int l0_soft_limit;

  // Number of files in Level-0 until writes are entirely stalled.
  // Default: 12
  int l0_hard_limit;

  DBOptions();
};

// Options that control read operations
struct ReadOptions {
  // If true, all data read from underlying storage will be
  // verified against corresponding checksums.
  // Default: false
  bool verify_checksums;

  // Should the data read for this iteration be cached in memory?
  // Callers may wish to set this field to false for bulk scans.
  // Default: true
  bool fill_cache;

  // Only fetch the first "limit" bytes of value
  // (instead of fetching the value in its entirety).
  // This is useful when the caller only needs a small prefix of the value,
  // or don't want the value at all.
  // If the total size of a value is smaller than specified limit, the
  // entire value will be returned.
  // Default: 1 << 31
  size_t limit;

  // If "snapshot" is non-NULL, read as of the supplied snapshot
  // (which must belong to the DB that is being read and which must
  // not have been released).  If "snapshot" is NULL, use an implicit
  // snapshot of the state at the beginning of this read operation.
  // Default: NULL
  const Snapshot* snapshot;

  ReadOptions();
};

// Options that control write operations
struct WriteOptions {
  // If true, the write will be flushed from the operating system
  // buffer cache (by calling WritableFile::Sync()) before the write
  // is considered complete.  If this flag is true, writes will be
  // slower.
  //
  // If this flag is false, and the machine crashes, some recent
  // writes may be lost.  Note that if it is just the process that
  // crashes (i.e., the machine does not reboot), no writes will be
  // lost even if sync==false.
  //
  // In other words, a DB write with sync==false has similar
  // crash semantics as the "write()" system call.  A DB write
  // with sync==true has similar crash semantics to a "write()"
  // system call followed by "fsync()".
  //
  // Default: false
  bool sync;

  WriteOptions();
};

// Options that control flush operations
struct FlushOptions {
  // Wait synchronously until the flush operation finishes.
  // Default: true
  bool wait;

  FlushOptions();
};

// During each bulk insertion, a set of table files are injected into
// the database. The following are possible operations that can be
// used to achieve this data injection.
enum InsertMethod {
  kRename = 0x0,  // May not supported by some underlying storage
  kCopy = 0x1
};

// Options that control bulk insertion operations
struct InsertOptions {
  // Set to true to disable auto sequence number translation.
  // This is useful when the keys bulk inserted are known to
  // not conflict with any existing keys in the target database.
  // Consider setting "suggested_max_seq" to an appropriate value
  // when auto sequence number translation is disabled.
  // Default: false
  bool no_seq_adjustment;

  // Request to forward the database's internal sequence number
  // to at least "suggested_max_seq".
  // Default: 0
  SequenceNumber suggested_max_seq;

  // If true, all data read from underlying storage will be
  // verified against corresponding checksums.
  // Default: false
  bool verify_checksums;

  // Which file system action should get performed when a
  // table file is to be bulk inserted into the database.
  // Default: kRename
  InsertMethod method;

  InsertOptions();
};

// Options that control dump operations
struct DumpOptions {
  // If true, all data read from underlying storage will be
  // verified against corresponding checksums.
  // Default: false
  bool verify_checksums;

  // If "snapshot" is non-NULL, read as of the supplied snapshot
  // (which must belong to the DB that is being read and which must
  // not have been released).  If "snapshot" is NULL, use an implicit
  // snapshot of the state at the beginning of this read operation.
  // Default: NULL
  const Snapshot* snapshot;

  DumpOptions();
};

}  // namespace pdlfs
