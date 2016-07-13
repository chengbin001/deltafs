/*
 * Copyright (c) 2011 The LevelDB Authors.
 * Copyright (c) 2015-2016 Carnegie Mellon University.
 *
 * All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file. See the AUTHORS file for names of contributors.
 */

#include <ctype.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "pdlfs-common/slice.h"
#include "pdlfs-common/strutil.h"

namespace pdlfs {

void AppendSignedNumberTo(std::string* str, int64_t num) {
  char buf[30];
  snprintf(buf, sizeof(buf), "%+lld", (long long)num);
  str->append(buf);
}

void AppendNumberTo(std::string* str, uint64_t num) {
  char buf[30];
  snprintf(buf, sizeof(buf), "%llu", (unsigned long long)num);
  str->append(buf);
}

void AppendEscapedStringTo(std::string* str, const Slice& value) {
  for (size_t i = 0; i < value.size(); i++) {
    char c = value[i];
    if (c >= ' ' && c <= '~') {
      str->push_back(c);
    } else {
      char buf[10];
      snprintf(buf, sizeof(buf), "\\x%02x",
               static_cast<unsigned int>(c) & 0xff);
      str->append(buf);
    }
  }
}

std::string NumberToString(uint64_t num) {
  std::string r;
  AppendNumberTo(&r, num);
  return r;
}

std::string EscapeString(const Slice& value) {
  std::string r;
  AppendEscapedStringTo(&r, value);
  return r;
}

bool ConsumeDecimalNumber(Slice* in, uint64_t* val) {
  uint64_t v = 0;
  int digits = 0;
  while (!in->empty()) {
    char c = (*in)[0];
    if (c >= '0' && c <= '9') {
      ++digits;
      const uint64_t delta = (c - '0');
      static const uint64_t kMaxUint64 = ~static_cast<uint64_t>(0);
      if (v > kMaxUint64 / 10 ||
          (v == kMaxUint64 / 10 && delta > kMaxUint64 % 10)) {
        // Overflow
        return false;
      }
      v = (v * 10) + delta;
      in->remove_prefix(1);
    } else {
      break;
    }
  }
  *val = v;
  return (digits > 0);
}

bool ParsePrettyBool(const Slice& value) {
  if (value == "t" || value == "y") {
    return true;
  } else if (value.starts_with("true") || value.starts_with("yes")) {
    return true;
  } else {
    return false;
  }
}

uint64_t ParsePrettyNumber(const Slice& value) {
  Slice input = value;
  uint64_t base;
  if (!ConsumeDecimalNumber(&input, &base)) {
    return 0;
  } else {
    if (input.empty()) {
      return base;
    } else if (input.starts_with("k")) {
      return base * 1024;
    } else if (input.starts_with("m")) {
      return base * 1024 * 1024;
    } else if (input.starts_with("g")) {
      return base * 1024 * 1024 * 1024;
    } else {
      return 0;
    }
  }
}

static Slice Trim(const Slice& v) {
  Slice input = v;
  while (!input.empty()) {
    if (isspace(input[0])) {
      input.remove_prefix(1);
    } else if (isspace(input[-1])) {
      input.remove_suffix(1);
    } else {
      break;
    }
  }
  return input;
}

size_t SplitString(const Slice& value, char delim,
                   std::vector<std::string>* v) {
  size_t count = 0;
  Slice input = value;
  while (!input.empty()) {
    const char* start = input.data();
    const char* limit = strchr(start, delim);
    if (limit != NULL) {
      input.remove_prefix(limit - start + 1);
      if (limit - start != 0) {
        Slice sub = Trim(Slice(start, limit - start));
        if (!sub.empty()) {
          v->push_back(sub.ToString());
          count++;
        }
      }
    } else {
      break;
    }
  }
  Slice sub = Trim(input);
  if (!sub.empty()) {
    v->push_back(sub.ToString());
    count++;
  }
  return count;
}

}  // namespace pdlfs
