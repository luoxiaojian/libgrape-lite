#ifndef GRAPE_PROPERTY_COLUMN_H_
#define GRAPE_PROPERTY_COLUMN_H_

#include <string>

#include "string_view/string_view.hpp"
#include "grape/utils/string_view_vector.h"
#include "grape/property/types.h"
#include "grape/property/date.h"

namespace grape {

class ColumnBase {
 public:
  virtual ~ColumnBase() {}

  virtual PropertyType type() const = 0;

  virtual AnyValue get_value(size_t index) const = 0;

  virtual Any get(size_t index) const = 0;

  virtual size_t size() const = 0;

  virtual void push_value(const AnyValue& value) = 0;
};

class IntColumn : public ColumnBase {
 public:
  IntColumn() {}
  ~IntColumn() {}

  void push_back(int val) {
    buffer_.push_back(val);
  }

  int get_view(size_t index) const {
    return buffer_[index];
  }

  size_t size() const override {
    return buffer_.size();
  }

  PropertyType type() const override {
    return PropertyType::kInt32;
  }

  AnyValue get_value(size_t index) const override {
    AnyValue ret;
    ret.i = buffer_[index];
    return ret;
  }

  Any get(size_t index) const override {
    Any ret;
    ret.set_integer(buffer_[index]);
    return ret;
  }

  void push_value(const AnyValue& value) override {
    buffer_.push_back(value.i);
  }

  template <typename IOADAPTOR_T>
  void Serialize(std::unique_ptr<IOADAPTOR_T>& writer) {
    size_t size = buffer_.size();
    CHECK(writer->Write(&size, sizeof(size_t)));
    CHECK(writer->Write(buffer_.data(), size * sizeof(int)));
  }

  template <typename IOADAPTOR_T>
  void Deserialize(std::unique_ptr<IOADAPTOR_T>& reader) {
    size_t size;
    CHECK(reader->Read(&size, sizeof(size_t)));
    buffer_.resize(size);
    CHECK(reader->Read(buffer_.data(), size * sizeof(int)));
  }

 private:
  std::vector<int> buffer_;
};

class Int64Column : public ColumnBase {
 public:
  Int64Column() {}
  ~Int64Column() {}

  void push_back(int64_t val) {
    buffer_.push_back(val);
  }

  int64_t get_view(size_t index) const {
    return buffer_[index];
  }

  size_t size() const override {
    return buffer_.size();
  }

  PropertyType type() const override {
    return PropertyType::kInt64;
  }

  AnyValue get_value(size_t index) const override {
    AnyValue ret;
    ret.i64 = buffer_[index];
    return ret;
  }

  Any get(size_t index) const override {
    Any ret;
    ret.set_int64(buffer_[index]);
    return ret;
  }

  void push_value(const AnyValue& value) override {
    buffer_.push_back(value.i64);
  }

  template <typename IOADAPTOR_T>
  void Serialize(std::unique_ptr<IOADAPTOR_T>& writer) {
    size_t size = buffer_.size();
    CHECK(writer->Write(&size, sizeof(size_t)));
    CHECK(writer->Write(buffer_.data(), size * sizeof(int64_t)));
  }

  template <typename IOADAPTOR_T>
  void Deserialize(std::unique_ptr<IOADAPTOR_T>& reader) {
    size_t size;
    CHECK(reader->Read(&size, sizeof(size_t)));
    buffer_.resize(size);
    CHECK(reader->Read(buffer_.data(), size * sizeof(int64_t)));
  }

 private:
  std::vector<int64_t> buffer_;
};

class FloatColumn : public ColumnBase {
 public:
  FloatColumn() {}
  ~FloatColumn() {}

  void push_back(float val) {
    buffer_.push_back(val);
  }

  float get_view(size_t index) const {
    return buffer_[index];
  }

  size_t size() const override {
    return buffer_.size();
  }

  PropertyType type() const override {
    return PropertyType::kFloat32;
  }

  AnyValue get_value(size_t index) const override {
    AnyValue ret;
    ret.f = buffer_[index];
    return ret;
  }

  Any get(size_t index) const override {
    Any ret;
    ret.set_float(buffer_[index]);
    return ret;
  }

  void push_value(const AnyValue& value) override {
    buffer_.push_back(value.f);
  }

  template <typename IOADAPTOR_T>
  void Serialize(std::unique_ptr<IOADAPTOR_T>& writer) {
    size_t size = buffer_.size();
    CHECK(writer->Write(&size, sizeof(size_t)));
    CHECK(writer->Write(buffer_.data(), size * sizeof(float)));
  }

  template <typename IOADAPTOR_T>
  void Deserialize(std::unique_ptr<IOADAPTOR_T>& reader) {
    size_t size;
    CHECK(reader->Read(&size, sizeof(size_t)));
    buffer_.resize(size);
    CHECK(reader->Read(buffer_.data(), size * sizeof(float)));
  }

 private:
  std::vector<float> buffer_;
};

class DateColumn : public ColumnBase {
 public:
  DateColumn() {}
  ~DateColumn() {}

  void push_back(Date val) {
    buffer_.push_back(val);
  }

  Date get_view(size_t index) const {
    return buffer_[index];
  }

  size_t size() const override {
    return buffer_.size();
  }

  PropertyType type() const override {
    return PropertyType::kDate;
  }

  AnyValue get_value(size_t index) const override {
    AnyValue ret;
    ret.d = buffer_[index];
    return ret;
  }

  Any get(size_t index) const override {
    Any ret;
    ret.set_date(buffer_[index]);
    return ret;
  }

  void push_value(const AnyValue& value) override {
    buffer_.push_back(value.d);
  }

  template <typename IOADAPTOR_T>
  void Serialize(std::unique_ptr<IOADAPTOR_T>& writer) {
    size_t size = buffer_.size();
    CHECK(writer->Write(&size, sizeof(size_t)));
    CHECK(writer->Write(buffer_.data(), size * sizeof(Date)));
  }

  template <typename IOADAPTOR_T>
  void Deserialize(std::unique_ptr<IOADAPTOR_T>& reader) {
    size_t size;
    CHECK(reader->Read(&size, sizeof(size_t)));
    buffer_.resize(size);
    CHECK(reader->Read(buffer_.data(), size * sizeof(Date)));
  }

 private:
  std::vector<Date> buffer_;
};

class StringColumn : public ColumnBase {
 public:
  StringColumn() {}
  ~StringColumn() {}

  void push_back(const nonstd::string_view& val) {
    buffer_.push_back(val);
  }

  nonstd::string_view get_view(size_t index) const {
    return buffer_[index];
  }

  size_t size() const override {
    return buffer_.size();
  }

  PropertyType type() const override {
    return PropertyType::kString;
  }

  AnyValue get_value(size_t index) const override {
    AnyValue ret;
    ret.s = buffer_[index];
    return ret;
  }

  Any get(size_t index) const override {
    Any ret;
    ret.set_string(buffer_[index]);
    return ret;
  }

  void push_value(const AnyValue& value) override {
    buffer_.push_back(value.s);
  }

  StringViewVector& buffer() { return buffer_; }

  template <typename IOADAPTOR_T>
  void Serialize(std::unique_ptr<IOADAPTOR_T>& writer) {
    size_t sizes[2];
    sizes[0] = buffer_.offset_buffer().size();
    sizes[1] = buffer_.content_buffer().size();
    CHECK(writer->Write(sizes, 2 * sizeof(size_t)));
    CHECK(writer->Write(buffer_.offset_buffer().data(), sizes[0] * sizeof(size_t)));
    CHECK(writer->Write(buffer_.content_buffer().data(), sizes[1]));
  }

  template <typename IOADAPTOR_T>
  void Deserialize(std::unique_ptr<IOADAPTOR_T>& reader) {
    size_t sizes[2];
    CHECK(reader->Read(sizes, 2 * sizeof(size_t)));
    buffer_.offset_buffer().resize(sizes[0]);
    buffer_.content_buffer().resize(sizes[1]);
    CHECK(reader->Read(buffer_.offset_buffer().data(), sizes[0] * sizeof(size_t)));
    CHECK(reader->Read(buffer_.content_buffer().data(), sizes[1]));
  }

 private:
  StringViewVector buffer_;
};

inline std::shared_ptr<ColumnBase> CreateColumn(PropertyType type) {
  if (type == PropertyType::kInt32) {
    return std::make_shared<IntColumn>();
  } else if (type == PropertyType::kFloat32) {
    return std::make_shared<FloatColumn>();
  } else if (type == PropertyType::kDate) {
    return std::make_shared<DateColumn>();
  } else if (type == PropertyType::kInt64) {
    return std::make_shared<Int64Column>();
  } else if (type == PropertyType::kString) {
    return std::make_shared<StringColumn>();
  } else {
    return nullptr;
  }
}


}  // namespace grape

#endif  // GRAPE_PROPERTY_COLUMN_H_