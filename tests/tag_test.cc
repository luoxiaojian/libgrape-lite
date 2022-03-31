#include <string>
#include <vector>

#include "grape/graph/id_indexer.h"
#include "grape/property/table.h"

void preprocessLine(char* line) {
  size_t len = strlen(line);
  while (len >= 0) {
    if (line[len] != '\0' && line[len] != '\n' &&
        line[len] != '\r' && line[len] != ' ' && line[len] != '\t') {
      break;
    } else {
      --len;
    }
  }
  line[len + 1] = '\0';
}

void parse_vertex_file(const std::string& filename,
                       grape::IdIndexer<int64_t, uint32_t>& indexer,
                       grape::Table& table,
                       const std::vector<grape::PropertyType>& property_types) {
  size_t col_num = property_types.size();
  std::vector<grape::Any> properties(col_num);
  for (size_t col_i = 0; col_i != col_num; ++col_i) {
    properties[col_i].type = property_types[col_i];
  }

  char line_buf[4096];
  int64_t oid;
  uint32_t v_index;
  std::vector<grape::Any> header(col_num + 1);
  for (auto& item : header) {
    item.type = grape::PropertyType::kString;
  }
  FILE* fin = fopen(filename.c_str(), "r");
  fgets(line_buf, 4096, fin);
  preprocessLine(line_buf);

  ParseRecord(line_buf, header);
  std::vector<std::string> col_names(col_num);
  for (size_t i = 0; i < col_num; ++i) {
    col_names[i] = header[i + 1].value.s.to_string();
  }
  table.init(col_names, property_types);

  while (fgets(line_buf, 4096, fin) != NULL) {
    preprocessLine(line_buf);
    ParseRecord(line_buf, oid, properties);
    if (indexer.add(oid, v_index)) {
      table.append(properties);
    }
  }
  fclose(fin);
}

int main(int argc, char** argv) {
  std::string tag_file_path = argv[1];
  std::string target_tag_name = argv[2];

  grape::IdIndexer<int64_t, uint32_t> indexer;
  grape::Table table;
  parse_vertex_file(tag_file_path, indexer, table, {grape::PropertyType::kString, grape::PropertyType::kString});

  size_t num = indexer.size();
  auto name_col = std::dynamic_pointer_cast<grape::StringColumn>(table.get_column_by_id(0));
  for (size_t i = 0; i < num; ++i) {
    if (name_col->get_view(i) == target_tag_name) {
      LOG(INFO) << "Got [" << target_tag_name << "] at " << i;
    }
  }

  return 0;
}