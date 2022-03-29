#ifndef GRAPE_PROPERTY_TYPES_H_
#define GRAPE_PROPERTY_TYPES_H_

#include <charconv>

#include "string_view/string_view.hpp"

namespace grape {

enum class PropertyType {
  kInt32,
  kInt64,
  kFloat32,
  kString,
};

union AnyValue {
  AnyValue() {}
  ~AnyValue() {}

  int i;
  int64_t i64;
  float f;
  nonstd::string_view s;
};

inline void ParseInt32(const nonstd::string_view& str, int& val) {
  sscanf(str.data(), "%d", &val);
}

inline void ParseInt64(const nonstd::string_view& str, int64_t& val) {
  sscanf(str.data(), "%lld", &val);
}

inline void ParseFloat32(const nonstd::string_view& str, float& val) {
  sscanf(str.data(), "%f", &val);
}

inline void ParseString(const nonstd::string_view& str, nonstd::string_view & val) {
  val = str;
}

struct Any {
  Any() {}
  ~Any() {}

  void set_integer(int v) {
    type = PropertyType::kInt32;
    value.i = v;
  }

  void set_int64(int64_t v) {
    type = PropertyType::kInt64;
    value.i64 = v;
  }

  void set_float(float v) {
    type = PropertyType::kFloat32;
    value.f = v;
  }

  void set_string(nonstd::string_view v) {
    type = PropertyType::kString;
    value.s = v;
  }

  PropertyType type;
  AnyValue value;
};

inline void ParseRecord(const char* line, std::vector<Any>& rec) {
  const char* cur = line;
  for (auto& item : rec) {
    const char* ptr = cur + 1;
    while (*ptr != '\0' && *ptr != '|') {
      ++ptr;
    }
    nonstd::string_view sv(cur, ptr - cur);
    if (item.type == PropertyType::kInt32) {
      ParseInt32(sv, item.value.i);
    } else if (item.type == PropertyType::kInt64) {
      ParseInt64(sv, item.value.i64);
    } else if (item.type == PropertyType::kFloat32) {
      ParseFloat32(sv, item.value.f);
    } else if (item.type == PropertyType::kString) {
      ParseString(sv, item.value.s);
    }
    cur = ptr;
  }
}

inline void ParseRecord(const char* line, int64_t& id, std::vector<Any>& rec) {
  const char* cur = line;
  {
    const char* ptr = cur + 1;
    while (*ptr != '\0' && *ptr != '|') {
      ++ptr;
    }
    nonstd::string_view sv(cur, ptr - cur);
    ParseInt64(sv, id);
    cur = ptr;
  }
  for (auto& item : rec) {
    const char* ptr = cur + 1;
    while (*ptr != '\0' && *ptr != '|') {
      ++ptr;
    }
    nonstd::string_view sv(cur, ptr - cur);
    if (item.type == PropertyType::kInt32) {
      ParseInt32(sv, item.value.i);
    } else if (item.type == PropertyType::kInt64) {
      ParseInt64(sv, item.value.i64);
    } else if (item.type == PropertyType::kFloat32) {
      ParseFloat32(sv, item.value.f);
    } else if (item.type == PropertyType::kString) {
      ParseString(sv, item.value.s);
    }
    cur = ptr;
  }
}

inline void ParseRecord(const char* line, int64_t& src, int64_t& dst, std::vector<Any>& rec) {
  const char* cur = line;
  {
    const char* ptr = cur + 1;
    while (*ptr != '\0' && *ptr != '|') {
      ++ptr;
    }
    nonstd::string_view sv(cur, ptr - cur);
    ParseInt64(sv, src);
    cur = ptr;
  }
  {
    const char* ptr = cur + 1;
    while (*ptr != '\0' && *ptr != '|') {
      ++ptr;
    }
    nonstd::string_view sv(cur, ptr - cur);
    ParseInt64(sv, dst);
    cur = ptr;
  }
  for (auto& item : rec) {
    const char* ptr = cur + 1;
    while (*ptr != '\0' && *ptr != '|') {
      ++ptr;
    }
    nonstd::string_view sv(cur, ptr - cur);
    if (item.type == PropertyType::kInt32) {
      ParseInt32(sv, item.value.i);
    } else if (item.type == PropertyType::kInt64) {
      ParseInt64(sv, item.value.i64);
    } else if (item.type == PropertyType::kFloat32) {
      ParseFloat32(sv, item.value.f);
    } else if (item.type == PropertyType::kString) {
      ParseString(sv, item.value.s);
    }
    cur = ptr;
  }
}

}  // namespace grape

#endif  // GRAPE_PROPERTY_TYPES_H_