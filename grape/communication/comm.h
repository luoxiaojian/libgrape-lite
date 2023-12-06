#ifndef GRAPE_COMMUNICATION_COMM_H_
#define GRAPE_COMMUNICATION_COMM_H_

namespace grape {

#include "grape/communication/mpi_comm.h"

using CommType = MPIComm;
using CommAllocatorType = MPICommAllocator;

}  // namespace grape

#endif  // GRAPE_COMMUNICATION_COMM_H_