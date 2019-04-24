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

#include "pdb.h"

namespace pdlfs {
namespace plfsio {

BufferedBlockWriter::BufferedBlockWriter(const DirOptions& options,
                                         WritableFile* dst, size_t buf_size,
                                         size_t n)
    : DoubleBuffering(&mu_, &bg_cv_),
      options_(options),
      dst_(dst),  // Not owned by us
      bg_cv_(&mu_),
      buf_threshold_(buf_size),
      buf_reserv_(8 + buf_size),
      offset_(0),
      bbs_(NULL),
      n_(n) {
  if (n_ < 2) {
    n_ = 2;  // We need at least two buffers
  }
  bbs_ = new BlockBuf*[n_];  // Allocate a requested amount of block buffers
  for (size_t i = 0; i < n_; i++) {
    bbs_[i] = new BlockBuf(options_, true);  // Force an unordered fmt
    bbs_[i]->Reserve(buf_reserv_);
    if (i != 0) {  // bbs_[0] will act as membuf_
      bufs_.push_back(bbs_[i]);
    }
  }

  bloomfilter_.reserve(4 << 20);
  membuf_ = bbs_[0];
}

// Wait for all outstanding compactions to clear.
BufferedBlockWriter::~BufferedBlockWriter() {
  mu_.Lock();
  while (num_bg_compactions_) {
    bg_cv_.Wait();
  }
  mu_.Unlock();
  for (size_t i = 0; i < n_; i++) {
    delete bbs_[i];
  }
  delete[] bbs_;
}

// Insert data into the writer.
// REQUIRES: Finish() has NOT been called.
Status BufferedBlockWriter::Add(const Slice& k, const Slice& v) {
  MutexLock ml(&mu_);
  return __Add<BufferedBlockWriter>(k, v, false);
}

// Force an epoch flush.
// REQUIRES: Finish() has NOT been called.
Status BufferedBlockWriter::EpochFlush() {
  return Flush();  // TODO
}

// Force a compaction but do NOT wait for the compaction to clear.
// REQUIRES: Finish() has NOT been called.
Status BufferedBlockWriter::Flush() {
  MutexLock ml(&mu_);
  return __Flush<BufferedBlockWriter>(false);
}

// Sync data to storage. Data still buffered in memory is NOT sync'ed.
// REQUIRES: Finish() has NOT been called.
Status BufferedBlockWriter::Sync() {
  MutexLock ml(&mu_);
  return __Sync<BufferedBlockWriter>(false);
}

// Wait until there is no outstanding compactions.
// REQUIRES: Finish() has NOT been called.
Status BufferedBlockWriter::Wait() {
  MutexLock ml(&mu_);  // Wait until !has_bg_compaction_
  return __Wait();
}

// Finalize the writer. Expected to be called ONLY once.
Status BufferedBlockWriter::Finish() {
  MutexLock ml(&mu_);
  return __Finish<BufferedBlockWriter>();
}

// REQUIRES: mu_ has been LOCKed.
Status BufferedBlockWriter::Compact(uint32_t const compac_seq, void* immbuf) {
  mu_.AssertHeld();
  assert(dst_);
  BlockBuf* const bb = static_cast<BlockBuf*>(immbuf);
  // Skip empty buffers
  if (bb->empty() && compac_seq == num_compac_completed_ + 1)
    return Status::OK();
  mu_.Unlock();  // Unlock as compaction is expensive
  Slice block_contents;
  if (!bb->empty()) {
    block_contents = bb->Finish(kNoCompression);
  }
  BloomBuilder bf(options_);  // Filter is built only when requested
  Slice filter_contents;
  if (!bb->empty() && options_.bf_bits_per_key != 0) {
    bf.Reset(bb->NumEntries());
    BlockContents bc;
    bc.data = block_contents;
    bc.heap_allocated = false;
    bc.cachable = false;
    Block b(bc);
    IteratorWrapper it(b.NewIterator(NULL));
    it.SeekToFirst();
    for (; it.Valid();) {
      bf.AddKey(it.key());
      it.Next();
    }
    filter_contents = bf.Finish();
  }
  mu_.Lock();  // All writes are serialized through compac_seq
  assert(num_compac_completed_ < compac_seq);
  while (compac_seq != num_compac_completed_ + 1) {
    bg_cv_.Wait();
  }
  mu_.Unlock();
  PutFixed64(&indexes_, bloomfilter_.size());
  if (!filter_contents.empty())
    bloomfilter_.append(filter_contents.data(), filter_contents.size());
  PutFixed64(&indexes_, offset_);
  Status status;
  if (!block_contents.empty()) {
    status = dst_->Append(block_contents);
  }
  if (status.ok()) {
    offset_ += block_contents.size();
    status = dst_->Flush();
  }
  mu_.Lock();
  return status;
}

// REQUIRES: no outstanding background compactions.
// REQUIRES: mu_ has been LOCKed.
Status BufferedBlockWriter::DumpIndexesAndFilters() {
  PutFixed64(&indexes_, bloomfilter_.size());
  PutFixed64(&indexes_, offset_);
  Status status;

  if (status.ok()) {
    bloomfilter_handle_.set_size(bloomfilter_.size());
    bloomfilter_handle_.set_offset(offset_);
    if (!bloomfilter_.empty()) status = dst_->Append(bloomfilter_);
    if (status.ok()) {
      offset_ += bloomfilter_.size();
    }
  }

  if (status.ok()) {
    index_handle_.set_size(indexes_.size());
    index_handle_.set_offset(offset_);
    if (!indexes_.empty()) status = dst_->Append(indexes_);
    if (status.ok()) {
      offset_ += indexes_.size();
    }
  }

  return status;
}

// REQUIRES: no outstanding background compactions.
// REQUIRES: mu_ has been LOCKed.
Status BufferedBlockWriter::Close() {
  assert(!num_bg_compactions_);
  mu_.AssertHeld();
  assert(dst_);
  Status status = DumpIndexesAndFilters();

  if (status.ok()) {
    std::string footer;
    bloomfilter_handle_.EncodeTo(&footer);
    index_handle_.EncodeTo(&footer);
    footer.resize(2 * BlockHandle::kMaxEncodedLength);
    status = dst_->Append(footer);
  }

  if (status.ok()) {
    status = dst_->Sync();
    dst_->Close();
  }

  return status;
}

// REQUIRES: no outstanding background compactions.
// REQUIRES: mu_ has been LOCKed.
Status BufferedBlockWriter::SyncBackend(bool close) {
  assert(!num_bg_compactions_);
  mu_.AssertHeld();
  assert(dst_);
  if (!close) {
    return dst_->Sync();
  } else {
    return Close();
  }
}

namespace {  // State for each compaction
struct State {
  BufferedBlockWriter* writer;
  uint32_t compac_seq;
  void* immbuf;
};
}  // namespace

// REQUIRES: mu_ has been LOCKed.
void BufferedBlockWriter::ScheduleCompaction(uint32_t const compac_seq,
                                             void* immbuf) {
  mu_.AssertHeld();

  assert(num_bg_compactions_);

  State* s = new State;
  s->compac_seq = compac_seq;
  s->immbuf = immbuf;
  s->writer = this;

  if (options_.compaction_pool) {
    options_.compaction_pool->Schedule(BufferedBlockWriter::BGWork, s);
  } else if (options_.allow_env_threads) {
    Env::Default()->Schedule(BufferedBlockWriter::BGWork, s);
  } else {
    DoCompaction<BufferedBlockWriter>(compac_seq, immbuf);
    delete s;
  }
}

void BufferedBlockWriter::BGWork(void* arg) {
  State* const s = reinterpret_cast<State*>(arg);
  MutexLock ml(&s->writer->mu_);
  s->writer->DoCompaction<BufferedBlockWriter>(s->compac_seq, s->immbuf);
  delete s;
}

BufferedBlockReader::BufferedBlockReader(const DirOptions& options,
                                         RandomAccessFile* src, uint64_t src_sz)
    : options_(options), src_(src), src_sz_(src_sz) {}

bool BufferedBlockReader::GetFrom(Status* status, const Slice& k,
                                  std::string* result, uint64_t offset,
                                  size_t n) {
  BlockContents contents;
  contents.heap_allocated = false;
  contents.cachable = false;
  std::string buf;
  buf.resize(n);
  *status = src_->Read(offset, n, &contents.data, &buf[0]);
  if (status->ok()) {
    if (contents.data.size() != n) {
      *status = Status::IOError("Read ret partial data");
    }
  }

  if (status->ok()) {
    Block block(contents);
    const Comparator* comp = NULL;  // Force linear search
    IteratorWrapper iter(block.NewIterator(comp));
    iter.Seek(k);
    if (iter.Valid()) {
      *result = iter.value().ToString();
      return true;
    }
  }

  return false;
}

// Get the value for a specific key.
Status BufferedBlockReader::Get(const Slice& k, std::string* result) {
  Status status = MaybeLoadCache();
  if (!status.ok()) {
    return status;
  }

  assert(indexes_.size() >= 16);
  uint64_t bloomoffset = DecodeFixed64(&indexes_[0]);
  uint64_t offset = DecodeFixed64(&indexes_[8]);
  uint64_t next_bloomoffset;
  uint64_t next_offset;
  const size_t limit = indexes_.size();
  size_t off = 16;
  for (; off + 15 < limit; off += 16) {
    next_bloomoffset = DecodeFixed64(&indexes_[off]);
    next_offset = DecodeFixed64(&indexes_[off + 8]);
    Slice bf(bloomfilter_.data() + bloomoffset, next_bloomoffset - bloomoffset);
    if (BloomKeyMayMatch(k, bf)) {
      if (GetFrom(&status, k, result, offset, next_offset - offset)) {
        break;
      } else if (!status.ok()) {
        break;
      }
    }

    bloomoffset = next_bloomoffset;
    offset = next_offset;
  }

  return status;
}

Status BufferedBlockReader::LoadIndexesAndFilters(Slice* footer) {
  BlockHandle bloomfilter_handle;
  BlockHandle index_handle;
  cache_status_ = bloomfilter_handle.DecodeFrom(footer);
  if (cache_status_.ok()) {
    cache_status_ = index_handle.DecodeFrom(footer);
  }
  if (!cache_status_.ok()) {
    return cache_status_;
  }

  uint64_t start = bloomfilter_handle.offset();
  assert(start + bloomfilter_handle.size() == index_handle.offset());
  size_t totalbytes = bloomfilter_handle.size() + index_handle.size();
  cache_.resize(totalbytes);
  cache_status_ = src_->Read(start, totalbytes, &cache_contents_, &cache_[0]);
  if (cache_status_.ok()) {
    if (cache_contents_.size() != totalbytes) {
      cache_status_ = Status::IOError("Read ret partial data");
    }
  }
  if (!cache_status_.ok()) {
    return cache_status_;
  }

  indexes_ = bloomfilter_ = cache_contents_;
  indexes_.remove_prefix(bloomfilter_handle.size());
  bloomfilter_.remove_suffix(index_handle.size());
  if (indexes_.size() < 16) {
    cache_status_ = Status::Corruption("Indexes too short to be valid");
  }

  return cache_status_;
}

// Read and cache all indexes and filters.
// Return OK on success, or a non-OK status on errors.
Status BufferedBlockReader::MaybeLoadCache() {
  if (!cache_status_.ok() ||
      !cache_contents_.empty()) {  // Do not repeat previous efforts
    return cache_status_;
  }

  const size_t footer_sz = 2 * BlockHandle::kMaxEncodedLength;
  std::string footer_stor;
  footer_stor.resize(footer_sz);
  Slice footer;
  if (src_sz_ < footer_stor.size()) {
    cache_status_ = Status::Corruption("Input file too short for a footer");
  } else {
    cache_status_ = src_->Read(src_sz_ - footer_stor.size(), footer_stor.size(),
                               &footer, &footer_stor[0]);
    if (cache_status_.ok()) {
      if (footer.size() != footer_stor.size()) {
        cache_status_ = Status::IOError("Read ret partial data");
      }
    }
  }

  if (cache_status_.ok()) {
    return LoadIndexesAndFilters(&footer);
  } else {
    return cache_status_;
  }
}

}  // namespace plfsio
}  // namespace pdlfs
