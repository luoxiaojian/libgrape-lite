#ifndef GRAPE_FRAGMENT_PROPERTY_FRAGMENT_H_
#define GRAPE_FRAGMENT_PROPERTY_FRAGMENT_H_

#include <iostream>

#include <glog/logging.h>

#include "grape/graph/id_indexer.h"
#include "grape/graph/immutable_csr.h"
#include "grape/property/types.h"
#include "grape/property/table.h"
#include "grape/graph/edge.h"

namespace grape {

class Schema {
 public:
  void add_vertex_label(const std::string& label, const std::vector<PropertyType>& properties) {
    uint8_t v_label_id = vertex_label_to_index(label);
    vproperties_[v_label_id] = properties;
  }

  void add_edge_label(const std::string& src_label, const std::string& dst_label, const std::string& edge_label,
                      const std::vector<PropertyType>& properties) {
    uint8_t src_label_id = vertex_label_to_index(src_label);
    uint8_t dst_label_id = vertex_label_to_index(dst_label);
    uint8_t edge_label_id = edge_label_to_index(edge_label);

    uint32_t label_id = generate_edge_label(src_label_id, dst_label_id, edge_label_id);
    eproperties_[label_id] = properties;
  }

  uint8_t vertex_label_num() const {
    return vlabel_indexer_.size();
  }

  uint8_t edge_label_num() const {
    return elabel_indexer_.size();
  }

  uint8_t get_vertex_label_id(const std::string& label) const {
    uint8_t ret;
    CHECK(vlabel_indexer_.get_index(label, ret));
    return ret;
  }

  const std::vector<PropertyType>& get_vertex_properties(const std::string& label) const {
    uint8_t index;
    CHECK(vlabel_indexer_.get_index(label, index));
    return vproperties_[index];
  }

  const std::vector<PropertyType>& get_edge_properties(const std::string& src_label, const std::string& dst_label, const std::string& label) const {
    uint8_t src, dst, edge;
    CHECK(vlabel_indexer_.get_index(src_label, src));
    CHECK(vlabel_indexer_.get_index(dst_label, dst));
    CHECK(elabel_indexer_.get_index(label, edge));
    uint32_t index = generate_edge_label(src, dst, edge);
    return eproperties_.at(index);
  }

  uint8_t get_edge_label_id(const std::string& label) const {
    uint8_t ret;
    CHECK(elabel_indexer_.get_index(label, ret));
    return ret;
  }

  std::string get_vertex_label_name(uint8_t index) const {
    std::string ret;
    vlabel_indexer_.get_key(index, ret);
    return ret;
  }

  std::string get_edge_label_name(uint8_t index) const {
    std::string ret;
    elabel_indexer_.get_key(index, ret);
    return ret;
  }

 private:
  uint8_t vertex_label_to_index(const std::string& label) {
    uint8_t ret;
    vlabel_indexer_.add(label, ret);
    if (vproperties_.size() <= ret) {
      vproperties_.resize(ret + 1);
    }
    return ret;
  }

  uint8_t edge_label_to_index(const std::string& label) {
    uint8_t ret;
    elabel_indexer_.add(label, ret);
    return ret;
  }

  uint32_t generate_edge_label(uint8_t src, uint8_t dst, uint8_t edge) const {
    uint32_t ret = 0;
    ret |= src;
    ret <<= 8;
    ret |= dst;
    ret <<= 8;
    ret |= edge;
    return ret;
  }
  IdIndexer<std::string, uint8_t> vlabel_indexer_;
  IdIndexer<std::string, uint8_t> elabel_indexer_;
  std::vector<std::vector<PropertyType>> vproperties_;
  std::map<uint32_t, std::vector<PropertyType>> eproperties_;
};

class SingleLabelSubGraph {
  using adj_list_t = AdjList<uint32_t, uint64_t>;
 public:
  SingleLabelSubGraph(
      IdIndexer<int64_t, uint32_t>& indexer,
      ImmutableCSR<uint32_t, Nbr<uint32_t, uint64_t>> &ie,
      ImmutableCSR<uint32_t, Nbr<uint32_t, uint64_t>> &oe)
      : indexer_(indexer), ie_(ie), oe_(oe) {}

  uint32_t GetVertex(int64_t id) const {
    uint32_t ret;
    CHECK(indexer_.get_index(id, ret));
    return ret;
  }

  adj_list_t GetIncomingAdjList(uint32_t v) {
    return adj_list_t(ie_.get_begin(v), ie_.get_end(v));
  }

  adj_list_t GetOutgoingAdjList(uint32_t v) {
    return adj_list_t(oe_.get_begin(v), oe_.get_end(v));
  }

 private:
  IdIndexer<int64_t, uint32_t>& indexer_;
  ImmutableCSR<uint32_t, Nbr<uint32_t, uint64_t>> &ie_;
  ImmutableCSR<uint32_t, Nbr<uint32_t, uint64_t>> &oe_;
};

class DoubleLabelSubGraph {
  using adj_list_t = AdjList<uint32_t, uint64_t>;
 public:
  DoubleLabelSubGraph(
      IdIndexer<int64_t, uint32_t>& src_indexer,
      IdIndexer<int64_t, uint32_t>& dst_indexer,
      ImmutableCSR<uint32_t, Nbr<uint32_t, uint64_t>> &ie,
      ImmutableCSR<uint32_t, Nbr<uint32_t, uint64_t>> &oe)
      : src_indexer_(src_indexer), dst_indexer_(dst_indexer),
        ie_(ie), oe_(oe) {}

  uint32_t GetSourceVertex(int64_t id) const {
    uint32_t ret;
    CHECK(src_indexer_.get_index(id, ret));
    return ret;
  }

  uint32_t GetDestinationVertex(int64_t id) const {
    uint32_t ret;
    CHECK(dst_indexer_.get_index(id, ret));
    return ret;
  }

  adj_list_t GetIncomingAdjList(uint32_t v) {
    return adj_list_t(ie_.get_begin(v), ie_.get_end(v));
  }

  adj_list_t GetOutgoingAdjList(uint32_t v) {
    return adj_list_t(oe_.get_begin(v), oe_.get_end(v));
  }

 private:
  IdIndexer<int64_t, uint32_t>& src_indexer_;
  IdIndexer<int64_t, uint32_t>& dst_indexer_;
  ImmutableCSR<uint32_t, Nbr<uint32_t, uint64_t>> &ie_;
  ImmutableCSR<uint32_t, Nbr<uint32_t, uint64_t>> &oe_;
};



class PropertyFragment {
 public:
  void Init(const Schema& schema, const std::vector<std::pair<std::string, std::string>>& vertex_files,
            const std::vector<std::tuple<std::string, std::string, std::string, std::string>>& edge_files) {
    schema_ = schema;
    size_t v_label_num = schema_.vertex_label_num();
    size_t e_label_num = schema_.edge_label_num();
    indexers_.resize(v_label_num);
    vertex_data_.resize(v_label_num);
    ie_.resize(v_label_num * v_label_num * e_label_num);
    oe_.resize(v_label_num * v_label_num * e_label_num);
    edge_data_.resize(v_label_num * v_label_num * e_label_num);

    for (size_t v_label_i = 0; v_label_i != v_label_num; ++v_label_i) {
      std::string v_label_name = schema_.get_vertex_label_name(static_cast<uint8_t>(v_label_i));
      std::vector<std::string> filenames;
      for (auto& pair : vertex_files) {
        if (pair.first == v_label_name) {
          filenames.push_back(pair.second);
        }
      }
      parseVertexFiles(v_label_name, filenames);
    }

    for (size_t src_label_i = 0; src_label_i != v_label_num; ++src_label_i) {
      std::string src_label_name = schema_.get_vertex_label_name(src_label_i);
      for (size_t dst_label_i = 0; dst_label_i != v_label_num; ++dst_label_i) {
        std::string dst_label_name = schema_.get_vertex_label_name(dst_label_i);
        for (size_t e_label_i = 0; e_label_i != e_label_num; ++e_label_i) {
          std::string e_label_name = schema_.get_edge_label_name(e_label_i);
          std::vector<std::string> filenames;
          for (auto& tup : edge_files) {
            if (std::get<0>(tup) == src_label_name && std::get<1>(tup) == dst_label_name && std::get<2>(tup) == e_label_name) {
              filenames.push_back(std::get<3>(tup));
            }
          }
          parseEdgeFiles(src_label_name, dst_label_name, e_label_name, filenames);
        }
      }
    }
  }

  const Schema& schema() const { return schema_; }

  uint32_t GetVertexNum(uint8_t label_id) const {
    return indexers_[label_id].size();
  }

  std::shared_ptr<ColumnBase> GetVertexDataColumn(uint8_t label_id, int col_id) {
    return vertex_data_[label_id].get_column_by_id(0);
  }

  SingleLabelSubGraph GetSubGraph(uint8_t vertex_label, uint8_t edge_label) {
    size_t v_label_num = schema_.vertex_label_num();
    size_t e_label_num = schema_.edge_label_num();
    size_t index = vertex_label * v_label_num * e_label_num + vertex_label * e_label_num + edge_label;
    return SingleLabelSubGraph(indexers_[vertex_label], ie_[index], oe_[index]);
  }

  DoubleLabelSubGraph GetSubGraph(uint8_t src_vertex_label, uint8_t dst_vertex_label, uint8_t edge_label) {
    size_t v_label_num = schema_.vertex_label_num();
    size_t e_label_num = schema_.edge_label_num();
    auto& ie = ie_[dst_vertex_label * v_label_num * e_label_num + src_vertex_label * e_label_num + edge_label];
    auto& oe = oe_[src_vertex_label * v_label_num * e_label_num + dst_vertex_label * e_label_num + edge_label];
    return DoubleLabelSubGraph(indexers_[src_vertex_label], indexers_[dst_vertex_label], ie, oe);
  }

 private:
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

  void parseVertexFiles(const std::string& vertex_label,
                        const std::vector<std::string>& filenames) {
    if (filenames.empty()) {
      return ;
    }

    size_t label_index = schema_.get_vertex_label_id(vertex_label);
    auto& indexer = indexers_[label_index];
    auto& table = vertex_data_[label_index];
    auto& property_types = schema_.get_vertex_properties(vertex_label);
    size_t col_num = property_types.size();
    std::vector<Any> properties(col_num);
    for (size_t col_i = 0; col_i != col_num; ++col_i) {
      properties[col_i].type = property_types[col_i];
    }

    char line_buf[4096];
    int64_t oid;
    uint32_t v_index;
    bool first_file = true;
    std::vector<Any> header(col_num + 1);
    for (auto& item : header) {
      item.type = PropertyType::kString;
    }
    for (auto filename : filenames) {
      FILE* fin = fopen(filename.c_str(), "r");
      fgets(line_buf, 4096, fin);
      preprocessLine(line_buf);
      if (first_file) {
        ParseRecord(line_buf, header);
        std::vector<std::string> col_names(col_num);
        for (size_t i = 0; i < col_num; ++i) {
          col_names[i] = header[i + 1].value.s.to_string();
        }
        table.init(col_names, property_types);
        first_file = false;
      }

      while (fgets(line_buf, 4096, fin) != NULL) {
        preprocessLine(line_buf);
        ParseRecord(line_buf, oid, properties);
        if (indexer.add(oid, v_index)) {
          table.append(properties);
        }
      }
      fclose(fin);
    }
  }

  void parseEdgeFiles(const std::string& src_label, const std::string& dst_label, const std::string& edge_label,
                      const std::vector<std::string>& filenames) {
    if (filenames.empty()) {
      return ;
    }

    size_t src_label_index = schema_.get_vertex_label_id(src_label);
    size_t dst_label_index = schema_.get_vertex_label_id(dst_label);
    size_t v_label_num = schema_.vertex_label_num();
    size_t edge_label_index = schema_.get_edge_label_id(edge_label);
    size_t e_label_num = schema_.edge_label_num();
    auto& src_indexer = indexers_[src_label_index];
    auto& dst_indexer = indexers_[dst_label_index];
    size_t index = src_label_index * v_label_num * e_label_num + dst_label_index * e_label_num + edge_label_index;
    auto& table = edge_data_[index];
    auto& in_csr = ie_[index];
    auto& out_csr = oe_[index];
    auto& property_types = schema_.get_edge_properties(src_label, dst_label, edge_label);
    size_t col_num = property_types.size();
    std::vector<Any> properties(col_num);
    for (size_t col_i = 0; col_i != col_num; ++col_i) {
      properties[col_i].type = property_types[col_i];
    }

    ImmutableCSRBuild<uint32_t, Nbr<uint32_t, uint64_t>> ie_builder, oe_builder;
    ie_builder.init(dst_indexer.size());
    oe_builder.init(src_indexer.size());

    std::vector<Edge<uint32_t, uint64_t>> parsed_edges;

    char line_buf[4096];
    int64_t src, dst;
    uint32_t src_index, dst_index;
    uint64_t row_id = 0;

    bool first_file = true;
    std::vector<Any> header(col_num + 2);
    for (auto& item : header) {
      item.type = PropertyType::kString;
    }
    for (auto filename : filenames) {
      FILE* fin = fopen(filename.c_str(), "r");
      fgets(line_buf, 4096, fin);
      preprocessLine(line_buf);
      if (first_file) {
        ParseRecord(line_buf, header);
        std::vector<std::string> col_names(col_num);
        for (size_t i = 0; i < col_num; ++i) {
          col_names[i] = header[i + 2].value.s.to_string();
        }
        table.init(col_names, property_types);
        first_file = false;
      }

      while (fgets(line_buf, 4096, fin) != NULL) {
        preprocessLine(line_buf);
        ParseRecord(line_buf, src, dst, properties);
        src_indexer.add(src, src_index);
        dst_indexer.add(dst, dst_index);
        ie_builder.inc_degree(dst_index);
        oe_builder.inc_degree(src_index);
        parsed_edges.emplace_back(src_index, dst_index, row_id++);
        table.append(properties);
      }
      fclose(fin);
    }
    ie_builder.build_offsets();
    oe_builder.build_offsets();

    for (auto& edge : parsed_edges) {
      ie_builder.add_edge(edge.dst, Nbr<uint32_t, uint64_t>(edge.src, edge.edata));
      oe_builder.add_edge(edge.src, Nbr<uint32_t, uint64_t>(edge.dst, edge.edata));
    }

    ie_builder.finish(in_csr);
    oe_builder.finish(out_csr);
  }

  Schema schema_;
  std::vector<IdIndexer<int64_t, uint32_t>> indexers_;
  std::vector<ImmutableCSR<uint32_t, Nbr<uint32_t, uint64_t>>> ie_, oe_;
  std::vector<Table> vertex_data_, edge_data_;
};

}  // namespace grape
#endif  // GRAPE_FRAGMENT_PROPERTY_FRAGMENT_H_
