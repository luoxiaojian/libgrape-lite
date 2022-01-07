#ifndef EXAMPLES_ANALYTICAL_APPS_BENCHMARK_FLAGS_H_
#define EXAMPLES_ANALYTICAL_APPS_BENCHMARK_FLAGS_H_

#include <gflags/gflags_declare.h>

DECLARE_double(pr_d);
DECLARE_int32(pr_mr);
DECLARE_bool(directed);
DECLARE_string(efile);
DECLARE_string(vfile);
DECLARE_string(out_prefix);
DECLARE_string(delta_efile_prefix);
DECLARE_int32(delta_efile_part_num);
DECLARE_int64(sssp_source);

#endif  // EXAMPLES_ANALYTICAL_APPS_BENCHMARK_FLAGS_H_