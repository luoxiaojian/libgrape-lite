#ifndef GRAPE_IO_IO_ADAPTOR_FACTORY_H_
#define GRAPE_IO_IO_ADAPTOR_FACTORY_H_

#include "grape/io/io_adaptor_base.h"
#include "grape/io/local_io_adaptor.h"

namespace grape {

std::unique_ptr<IOAdaptorBase> create_io_adaptor(const std::string& path) {
  return std::unique_ptr<IOAdaptorBase>(new LocalIOAdaptor(path));
}

}  // namespace grape

#endif  // GRAPE_IO_IO_ADAPTOR_FACTORY_H_
