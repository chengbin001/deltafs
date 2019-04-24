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

#include "cuckoo.h"
#include "filter.h"
#include "types.h"

#include "pdlfs-common/testharness.h"
#include "pdlfs-common/testutil.h"

#include <set>

namespace pdlfs {
namespace plfsio {

class CuckooTest {
 public:
  static inline uint32_t KeyFringerprint(uint64_t ha) {
    return CuckooFingerprint(ha, keybits_);
  }

  static inline uint64_t KeyHash(uint32_t k) {
    char tmp[4];
    Slice key(tmp, sizeof(tmp));
    EncodeFixed32(tmp, k);
    return CuckooHash(key);
  }

  enum { keybits_ = 8 };
  enum { valbits_ = 24 };
  std::string data_;  // Final filter representation
  DirOptions options_;
};

class CuckooFtTest : public CuckooTest {
 public:
  CuckooFtTest() {
    // Ignore target occupation rate and always allocate the exact number of
    // cuckoo buckets
    options_.cuckoo_frac = -1;
    cf_ = new Filter(options_, 0);  // Do not reserve memory
  }

  ~CuckooFtTest() {
    if (cf_ != NULL) {
      delete cf_;
    }
  }

  bool KeyMayMatch(uint32_t k) {
    char tmp[4];
    Slice key(tmp, sizeof(tmp));
    EncodeFixed32(tmp, k);
    return CuckooKeyMayMatch(key, data_);
  }

  bool AddKey(uint32_t k) {
    char tmp[4];
    Slice key(tmp, sizeof(tmp));
    EncodeFixed32(tmp, k);
    return cf_->TEST_AddKey(key);
  }

  void Finish() { data_ = cf_->TEST_Finish(); }
  void Reset(uint32_t num_keys) { cf_->Reset(num_keys); }
  typedef CuckooBlock<keybits_, 0> Filter;
  Filter* cf_;
};

TEST(CuckooFtTest, BytesPerBucket) {
  fprintf(stderr, "%d\n", int(cf_->TEST_BytesPerCuckooBucket()));
}

TEST(CuckooFtTest, BitsPerKey) {
  fprintf(stderr, "%d\n", int(8 * cf_->TEST_BytesPerCuckooBucket() / 4));
}

TEST(CuckooFtTest, AltIndex) {
  for (uint32_t ki = 1; ki <= 1024; ki *= 2) {
    uint32_t num_keys = ki << 10;
    Reset(num_keys);
    size_t num_buckets = cf_->TEST_NumBuckets();
    uint32_t k = 0;
    for (; k < num_keys; k++) {
      uint64_t hash = KeyHash(k);
      uint32_t fp = KeyFringerprint(hash);
      size_t i1 = hash % num_buckets, i2 = CuckooAlt(i1, fp) % num_buckets;
      size_t i3 = CuckooAlt(i2, fp) % num_buckets;
      ASSERT_TRUE(i1 == i3);
    }
  }
}

TEST(CuckooFtTest, Empty) {
  for (uint32_t ki = 1; ki <= 1024; ki *= 2) {
    uint32_t num_keys = ki << 10;
    Reset(num_keys);
    Finish();
    uint32_t k = 0;
    for (; k < num_keys; k++) {
      ASSERT_FALSE(KeyMayMatch(k));
    }
  }
}

TEST(CuckooFtTest, AddAndMatch) {
  for (uint32_t ki = 1; ki <= 1024; ki *= 2) {
    uint32_t num_keys = ki << 10;
    fprintf(stderr, "%4u Ki keys: ", ki);
    Reset(num_keys);
    uint32_t k = 0;
    for (; k < num_keys; k++) {
      if (!AddKey(k)) {
        break;
      }
    }
    Finish();
    fprintf(stderr, "%.2f%% filled\n", 100.0 * k / num_keys);
    uint32_t j = 0;
    for (; j < k; j++) {
      ASSERT_TRUE(KeyMayMatch(j));
    }
  }
}

class CuckooAuxTest : public CuckooFtTest {
 public:
  void AddKey(uint32_t k) {
    char tmp[4];
    Slice key(tmp, sizeof(tmp));
    EncodeFixed32(tmp, k);
    cf_->AddKey(key);
  }
};

TEST(CuckooAuxTest, AuxiliaryTables) {
  for (uint32_t ki = 1; ki <= 1024; ki *= 2) {
    uint32_t num_keys = ki << 10;
    fprintf(stderr, "%4u Ki keys: ", ki);
    Reset(num_keys);
    uint32_t k = 0;
    for (; k < num_keys; k++) {
      AddKey(k);
    }
    Finish();
    fprintf(stderr, "%.2fx buckets, %+d aux tables\n",
            1.0 * cf_->TEST_NumBuckets() / ((num_keys + 3) / 4),
            int(cf_->TEST_NumCuckooTables()) - 1);
    uint32_t j = 0;
    for (; j < k; j++) {
      ASSERT_TRUE(KeyMayMatch(j));
    }
  }
}

class CuckooKvTest : public CuckooTest {
 public:
  CuckooKvTest() {
    // Ignore target occupation rate and always allocate the exact number of
    // cuckoo buckets
    options_.cuckoo_frac = -1;
    cf_ = new Filter(options_, 0);  // Do not reserve memory
  }

  ~CuckooKvTest() {
    if (cf_ != NULL) {
      delete cf_;
    }
  }

  bool GetValues(uint32_t k, std::vector<uint32_t>* values) {
    char tmp[4];
    Slice key(tmp, sizeof(tmp));
    EncodeFixed32(tmp, k);
    return CuckooValues(key, data_, values);
  }

  bool KeyMayMatch(uint32_t k) {
    char tmp[4];
    Slice key(tmp, sizeof(tmp));
    EncodeFixed32(tmp, k);
    return CuckooKeyMayMatch(key, data_);
  }

  bool AddKey(uint32_t k) {
    char tmp[4];
    Slice key(tmp, sizeof(tmp));
    EncodeFixed32(tmp, k);
    return cf_->TEST_AddKey(key, k);
  }

  void Finish() { data_ = cf_->TEST_Finish(); }
  void Reset(uint32_t num_keys) { cf_->Reset(num_keys); }
  typedef CuckooBlock<keybits_, valbits_> Filter;
  Filter* cf_;
};

TEST(CuckooKvTest, KvBytesPerBucket) {
  fprintf(stderr, "%d\n", int(cf_->TEST_BytesPerCuckooBucket()));
}

TEST(CuckooKvTest, KvBitsPerKey) {
  fprintf(stderr, "%d\n", int(8 * cf_->TEST_BytesPerCuckooBucket() / 4));
}

TEST(CuckooKvTest, KvEmpty) {
  for (uint32_t ki = 1; ki <= 1024; ki *= 2) {
    uint32_t num_keys = ki << 10;
    Reset(num_keys);
    Finish();
    uint32_t k = 0;
    for (; k < num_keys; k++) {
      ASSERT_FALSE(KeyMayMatch(k));
    }
  }
}

TEST(CuckooKvTest, KvAddAndMatch) {
  for (uint32_t ki = 1; ki <= 1024; ki *= 2) {
    uint32_t num_keys = ki << 10;
    fprintf(stderr, "%4u Ki keys: ", ki);
    Reset(num_keys);
    uint32_t k = 0;
    for (; k < num_keys; k++) {
      if (!AddKey(k)) {
        break;
      }
    }
    Finish();
    fprintf(stderr, "%.2f%% filled\n", 100.0 * k / num_keys);
    uint32_t j = 0;
    for (; j < k; j++) {
      ASSERT_TRUE(KeyMayMatch(j));
    }
  }
}

TEST(CuckooKvTest, KvAddAndGet) {
  for (uint32_t ki = 1; ki <= 1024; ki *= 2) {
    uint32_t num_keys = ki << 10;
    fprintf(stderr, "%4u Ki keys: ", ki);
    Reset(num_keys);
    uint32_t k = 0;
    for (; k < num_keys; k++) {
      if (!AddKey(k)) {
        break;
      }
    }
    Finish();
    fprintf(stderr, "%.2f%% filled\n", 100.0 * k / num_keys);
    uint32_t j = 0;
    std::vector<uint32_t> values;
    std::set<uint32_t> set;
    for (; j < k; j++) {
      ASSERT_TRUE(GetValues(j, &values));
      ASSERT_TRUE(!values.empty());
      set.insert(values.begin(), values.end());
      ASSERT_TRUE(set.count(j) != 0);
      values.resize(0);
      set.clear();
    }
  }
}

class CuckooKvAuxTest : public CuckooKvTest {
 public:
  void AddKey(uint32_t k) {
    char tmp[4];
    Slice key(tmp, sizeof(tmp));
    EncodeFixed32(tmp, k);
    cf_->AddKey(key, k);
  }
};

TEST(CuckooKvAuxTest, KvAuxiliaryTables) {
  for (uint32_t ki = 1; ki <= 1024; ki *= 2) {
    uint32_t num_keys = ki << 10;
    fprintf(stderr, "%4u Ki keys: ", ki);
    Reset(num_keys);
    uint32_t k = 0;
    for (; k < num_keys; k++) {
      AddKey(k);
    }
    Finish();
    fprintf(stderr, "%.2fx buckets, %+d aux tables\n",
            1.0 * cf_->TEST_NumBuckets() / ((num_keys + 3) / 4),
            int(cf_->TEST_NumCuckooTables()) - 1);
    uint32_t j = 0;
    std::vector<uint32_t> values;
    std::set<uint32_t> set;
    for (; j < k; j++) {
      ASSERT_TRUE(GetValues(j, &values));
      ASSERT_TRUE(!values.empty());
      set.insert(values.begin(), values.end());
      ASSERT_TRUE(set.count(j) != 0);
      values.resize(0);
      set.clear();
    }
  }
}

// Evaluate false positive rate under different filter configurations.
class PlfsFalsePositiveBench {
 protected:
  static int FromEnv(const char* key, int def) {
    const char* env = getenv(key);
    if (env && env[0]) {
      return atoi(env);
    } else {
      return def;
    }
  }

  static inline int GetOption(const char* key, int def) {
    int opt = FromEnv(key, def);
    fprintf(stderr, "%s=%d\n", key, opt);
    return opt;
  }

  void Report(uint32_t hits, uint32_t n) {
    const double ki = 1024.0;
    fprintf(stderr, "------------------------------------------------\n");
    fprintf(stderr, "          Bits per k: %d\n", int(keybits_));
    fprintf(stderr, "       Keys inserted: %.3g Mi\n", n / ki / ki);
    const uint32_t num_queries = 1u << qlg_;
    fprintf(stderr, "             Queries: %.3g Mi (ALL neg)\n",
            num_queries / ki / ki);
    fprintf(stderr, "                Hits: %u\n", hits);
    fprintf(stderr, "                  FP: %.4g%%\n",
            100.0 * hits / num_queries);
  }

  DirOptions options_;
  std::string filterdata_;
  size_t keybits_;
  // Number of keys to query is 1u << qlg_
  int qlg_;
  int nlg_;
};

class PlfsBloomBench : protected PlfsFalsePositiveBench {
 public:
  PlfsBloomBench() {
    keybits_ = GetOption("BLOOM_KEY_BITS", 12);
    nlg_ = GetOption("LG_KEYS", 20);
    assert(nlg_ < 30);
    qlg_ = GetOption("LG_QUERIES", nlg_);
    assert(qlg_ < 30);
  }

  // Store filter data in *dst. Return number of keys inserted.
  uint32_t BuildFilter(std::string* const dst) {
    char tmp[4];
    Slice key(tmp, sizeof(tmp));
    options_.bf_bits_per_key = keybits_;
    BloomBlock ft(options_, 0);  // Do not reserve memory for it
    const uint32_t num_keys = 1u << nlg_;
    ft.Reset(num_keys);
    uint32_t i = 0;
    for (; i < num_keys; i++) {
      EncodeFixed32(tmp, i);
      ft.AddKey(key);
    }
    *dst = ft.TEST_Finish();
    return i;
  }

  void LogAndApply() {
    uint32_t n = BuildFilter(&filterdata_);
    uint32_t hits = 0;
    char tmp[4];
    Slice key(tmp, sizeof(tmp));
    const uint32_t num_queries = 1u << qlg_;
    uint32_t i = n;
    for (; i < n + num_queries; i++) {
      EncodeFixed32(tmp, i);
      if (BloomKeyMayMatch(key, filterdata_)) {
        hits++;
      }
    }

    Report(hits, n);
  }
};

class PlfsCuckoBench : protected PlfsFalsePositiveBench {
 public:
  PlfsCuckoBench() {
    use_auxtables_ = GetOption("CUCKOO_ENABLE_AUX", 1);
    keybits_ = GetOption("CUCKOO_KEY_BITS", 12);
    nlg_ = GetOption("LG_KEYS", 20);
    assert(nlg_ < 30);
    qlg_ = GetOption("LG_QUERIES", nlg_);
    assert(qlg_ < 30);
  }

  template <size_t k>
  uint32_t CuckooBuildFilter(uint32_t* num_buckets, std::string* const dst) {
    char tmp[4];
    Slice key(tmp, sizeof(tmp));
    options_.cuckoo_frac = -1;
    CuckooBlock<k, 0> ft(options_, 0);  // Do not reserve memory for it
    const uint32_t num_keys = 1u << nlg_;
    ft.Reset(num_keys);
    uint32_t i = 0;
    for (; i < num_keys; i++) {
      EncodeFixed32(tmp, i);
      if (use_auxtables_) {
        ft.AddKey(key);
      } else if (!ft.TEST_AddKey(key)) {
        break;
      }
    }
    *dst = ft.TEST_Finish();
    *num_buckets = ft.TEST_NumBuckets();
    return i;
  }

  void LogAndApply() {
    uint32_t num_buckets = 0;
    uint32_t n;
    switch (keybits_) {
#define CASE(k)                                           \
  case k:                                                 \
    n = CuckooBuildFilter<k>(&num_buckets, &filterdata_); \
    break
      CASE(1);
      CASE(2);
      CASE(4);
      CASE(8);
      CASE(12);
      CASE(16);
      CASE(24);
      CASE(32);
      default:
        fprintf(stderr, "!! FILTER CONF NOT SUPPORTED\n");
        exit(1);
    }
    uint32_t hits = 0;
    char tmp[4];
    Slice key(tmp, sizeof(tmp));
    const uint32_t num_queries = 1u << qlg_;
    uint32_t i = n;
    for (; i < n + num_queries; i++) {
      EncodeFixed32(tmp, i);
      if (CuckooKeyMayMatch(key, filterdata_)) {
        hits++;
      }
    }
#undef CASE
    Report(num_buckets, hits, n);
  }

  void Report(uint32_t num_buckets, uint32_t hits, uint32_t n) {
    PlfsFalsePositiveBench::Report(hits, n);
    const double ki = 1024.0;
    fprintf(stderr, "   Cuckoo bits per k: %.2f\n",
            1.0 * keybits_ * 4 * num_buckets / n);
    fprintf(stderr, "             Buckets: %.3g Ki = %.3g Mi keys\n",
            1.0 * num_buckets / ki, 4.0 * num_buckets / ki / ki);
    fprintf(stderr, "                Util: %.2f%%\n",
            100.0 * n / num_buckets / 4);
  }

  // If aux tables should be used
  int use_auxtables_;
};

// Evaluate the accuracy of a cuckoo filter
// when used as a hash table.
class PlfsTableBench : public PlfsCuckoBench {
 public:
  PlfsTableBench() {  // Use a random generator to get random values
    fprintf(stderr, "LG_QUERIES IS IGNORED AND ONLY LG_KEYS MATTERS\n");
    rndseed_ = GetOption("RANDOM_SEED", 301);
  }

  template <size_t k>
  uint32_t CuckooBuildTable(uint32_t* num_buckets, uint32_t* num_tables,
                            std::string* const dst) {
    Random rnd(rndseed_);
    char tmp[4];
    Slice key(tmp, sizeof(tmp));
    options_.cuckoo_frac = -1;
    CuckooBlock<k, 32> ft(options_, 0);  // Do not reserve memory for it
    const uint32_t num_keys = 1u << nlg_;
    ft.Reset(num_keys);
    uint32_t i = 0;
    fprintf(stderr, "Building ...\n");
    for (; i < num_keys; i++) {
      if ((i & 0x7FFFFu) == 0)
        fprintf(stderr, "\r%.2f%%", 100.0 * i / num_keys);
      EncodeFixed32(tmp, i);
      if (use_auxtables_) {
        ft.AddKey(key, rnd.Next());
      } else if (!ft.TEST_AddKey(key, rnd.Next())) {
        break;
      }
    }
    fprintf(stderr, "\r100.00%%");
    fprintf(stderr, "\n");
    *dst = ft.TEST_Finish();
    *num_tables = ft.TEST_NumCuckooTables();
    *num_buckets = ft.TEST_NumBuckets();
    return i;
  }

  void LogAndApply() {
    uint32_t num_buckets = 0, num_tables = 0;
    uint32_t n;
    switch (keybits_) {
#define CASE(k)                                                       \
  case k:                                                             \
    n = CuckooBuildTable<k>(&num_buckets, &num_tables, &filterdata_); \
    break
      CASE(1);
      CASE(2);
      CASE(4);
      CASE(8);
      CASE(12);
      CASE(16);
      CASE(24);
      CASE(32);
      default:
        fprintf(stderr, "!! FILTER CONF NOT SUPPORTED\n");
        exit(1);
    }
    std::vector<uint32_t> values;
    size_t hits_sum = 0;
    size_t hits_max = 0;
    char tmp[4];
    Slice key(tmp, sizeof(tmp));
    uint32_t i = 0;
    fprintf(stderr, "Querying ...\n");
    for (; i < n; i++) {
      if ((i & 0x7FFu) == 0) fprintf(stderr, "\r%.2f%%", 100.0 * i / n);
      EncodeFixed32(tmp, i);
      CuckooValues(key, filterdata_, &values);
      hits_max = std::max(hits_max, values.size());
      hits_sum += values.size();
      values.resize(0);
    }
    fprintf(stderr, "\r100.00%%");
    fprintf(stderr, "\n");
#undef CASE
    Report(hits_sum, hits_max, num_buckets, num_tables, n);
  }

  void Report(size_t hits_sum, size_t hits_max, uint32_t num_buckets,
              uint32_t num_tables, uint32_t n) {
    const double ki = 1024.0;
    fprintf(stderr, "-------------------------------------------------\n");
    fprintf(stderr, "              Bits per k: %d\n", int(keybits_));
    fprintf(stderr, "           Keys inserted: %.3g Mi\n", n / ki / ki);
    fprintf(stderr, "                 Queries: %.3g Mi\n", n / ki / ki);
    fprintf(stderr, " Num cuckoo tables built: %d\n", int(num_tables));
    fprintf(stderr, "        Max hits per key: %d\n", int(hits_max));
    fprintf(stderr, "                Avg hits: %.3g\n", 1.0 * hits_sum / n);
    fprintf(stderr, "                    Util: %.2f%%\n",
            100.0 * n / num_buckets / 4);
  }

 private:
  int rndseed_;
};

}  // namespace plfsio
}  // namespace pdlfs

#if defined(PDLFS_GFLAGS)
#include <gflags/gflags.h>
#endif
#if defined(PDLFS_GLOG)
#include <glog/logging.h>
#endif

static void BM_Usage() {
  fprintf(stderr, "Use --bench=[bf,cf,kv] to run benchmark.\n");
  fprintf(stderr, "\n");
}

static void BM_Main(int* argc, char*** argv) {
#if defined(PDLFS_GFLAGS)
  google::ParseCommandLineFlags(argc, argv, true);
#endif
#if defined(PDLFS_GLOG)
  google::InitGoogleLogging((*argv)[0]);
  google::InstallFailureSignalHandler();
#endif
  pdlfs::Slice bench_name;
  if (*argc > 1) {
    bench_name = pdlfs::Slice((*argv)[*argc - 1]);
  } else {
    BM_Usage();
  }
  if (bench_name.starts_with("--bench=bf")) {
    typedef pdlfs::plfsio::PlfsBloomBench BM_Bench;
    BM_Bench bench;
    bench.LogAndApply();
  } else if (bench_name.starts_with("--bench=cf")) {
    typedef pdlfs::plfsio::PlfsCuckoBench BM_Bench;
    BM_Bench bench;
    bench.LogAndApply();
  } else if (bench_name.starts_with("--bench=kv")) {
    typedef pdlfs::plfsio::PlfsTableBench BM_Bench;
    BM_Bench bench;
    bench.LogAndApply();
  } else {
    BM_Usage();
  }
}

int main(int argc, char* argv[]) {
  pdlfs::Slice token;
  if (argc > 1) {
    token = pdlfs::Slice(argv[argc - 1]);
  }
  if (!token.starts_with("--bench")) {
    return pdlfs::test::RunAllTests(&argc, &argv);
  } else {
    BM_Main(&argc, &argv);
    return 0;
  }
}
