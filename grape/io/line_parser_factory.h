#ifndef GRAPE_IO_LINE_PARSER_FACTORY_H_
#define GRAPE_IO_LINE_PARSER_FACTORY_H_

#include "grape/io/line_parser_base.h"
#include "grape/io/tsv_line_parser.h"

namespace grape {

template <typename OID_T, typename VDATA_T, typename EDATA_T>
struct LineParserFactory {
  static std::unique_ptr<LineParserBase<OID_T, VDATA_T, EDATA_T>> create() {
    return std::unique_ptr<LineParserBase<OID_T, VDATA_T, EDATA_T>>(
        new TSVLineParser<OID_T, VDATA_T, EDATA_T>());
  }
};

}  // namespace grape

#endif  // GRAPE_IO_LINE_PARSER_FACTORY_H_
