#ifndef GRAPE_PROPERTY_TABLE_H_
#define GRAPE_PROPERTY_TABLE_H_

#include <map>
#include <memory>

#include "grape/property/column.h"
#include "grape/graph/id_indexer.h"

namespace grape {

class Table {
 public:
  Table() {}
  ~Table() {}

  void init(const std::vector<std::string>& col_name, const std::vector<PropertyType>& types) {
    size_t col_num = col_name.size();
    columns_.resize(col_num);
    for (size_t i = 0; i < col_num; ++i) {
      int col_id;
      col_id_indexer_.add(col_name[i], col_id);
      columns_[col_id] = CreateColumn(types[i]);
    }
    columns_.resize(col_id_indexer_.size());
  }

  std::shared_ptr<ColumnBase> get_column(const std::string& name) {
    int col_id;
    if (col_id_indexer_.get_index(name, col_id)) {
      if (static_cast<size_t>(col_id) < columns_.size()) {
        return columns_[col_id];
      }
    }
    return nullptr;
  }

  std::shared_ptr<ColumnBase> get_column_by_id(size_t index) {
    if (index >= columns_.size()) {
      return nullptr;
    } else {
      return columns_[index];
    }
  }

  size_t row_num() const {
    if (columns_.empty()) {
      return 0;
    } else {
      return columns_[0]->size();
    }
  }

  void append_values(const std::vector<AnyValue>& values) {
    assert(values.size() == columns_.size());
    size_t col_num = columns_.size();
    for (size_t i = 0; i < col_num; ++i) {
      columns_[i]->push_value(values[i]);
    }
  }

  void append(const std::vector<Any>& values) {
    assert(values.size() == columns_.size());
    size_t col_num = columns_.size();
    for (size_t i = 0; i < col_num; ++i) {
      assert(columns_[i]->type() == values[i].type);
      columns_[i]->push_value(values[i].value);
    }
  }

  template <typename IOADAPTOR_T>
  void Serialize(std::unique_ptr<IOADAPTOR_T>& writer) {
    col_id_indexer_.Serialize(writer);
    std::vector<PropertyType> types;
    for (auto col : columns_) {
      types.push_back(col->type());
    }
    CHECK_EQ(types.size(), col_id_indexer_.size());
    if (types.empty()) {
      return ;
    }
    CHECK(writer->Write(types.data(), sizeof(PropertyType) * types.size()));
    for (auto col : columns_) {
      auto type = col->type();
      if (type == PropertyType::kInt32) {
        std::dynamic_pointer_cast<IntColumn>(col)->Serialize(writer);
      } else if (type == PropertyType::kFloat32) {
        std::dynamic_pointer_cast<FloatColumn>(col)->Serialize(writer);
      } else if (type == PropertyType::kDate) {
        std::dynamic_pointer_cast<DateColumn>(col)->Serialize(writer);
      } else if (type == PropertyType::kInt64) {
        std::dynamic_pointer_cast<Int64Column>(col)->Serialize(writer);
      } else if (type == PropertyType::kString) {
        std::dynamic_pointer_cast<StringColumn>(col)->Serialize(writer);
      }
    }
  }

  template <typename IOADAPTOR_T>
  void Deserialize(std::unique_ptr<IOADAPTOR_T>& reader) {
    col_id_indexer_.Deserialize(reader);
    std::vector<PropertyType> types(col_id_indexer_.size());
    if (types.empty()) {
      return ;
    }
    CHECK(reader->Read(types.data(), sizeof(PropertyType) * types.size()));
    columns_.resize(types.size());
    for (size_t i = 0; i < types.size(); ++i) {
      auto type = types[i];
      if (type == PropertyType::kInt32) {
        auto ptr = std::make_shared<IntColumn>();
        ptr->Deserialize(reader);
        columns_[i] = ptr;
      } else if (type == PropertyType::kFloat32) {
        auto ptr = std::make_shared<FloatColumn>();
        ptr->Deserialize(reader);
        columns_[i] = ptr;
      } else if (type == PropertyType::kDate) {
        auto ptr = std::make_shared<DateColumn>();
        ptr->Deserialize(reader);
        columns_[i] = ptr;
      } else if (type == PropertyType::kInt64) {
        auto ptr = std::make_shared<Int64Column>();
        ptr->Deserialize(reader);
        columns_[i] = ptr;
      } else if (type == PropertyType::kString) {
        auto ptr = std::make_shared<StringColumn>();
        ptr->Deserialize(reader);
        columns_[i] = ptr;
      }
    }
  }

 private:
  std::vector<std::shared_ptr<ColumnBase>> columns_;
  IdIndexer<nonstd::string_view, int> col_id_indexer_;
};

}  // namespace grape

#endif  // GRAPE_PROPERTY_TABLE_H_
