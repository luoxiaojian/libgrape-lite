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

 private:
  std::vector<std::shared_ptr<ColumnBase>> columns_;
  IdIndexer<std::string, int> col_id_indexer_;
};

}  // namespace grape

#endif  // GRAPE_PROPERTY_TABLE_H_
