#ifndef GRAPE_COMMUNICATION_COMM_H_
#define GRAPE_COMMUNICATION_COMM_H_

#include "grape/communication/mpi_comm.h"

namespace grape {

using CommType = MPIComm;
using CommAllocatorType = MPICommAllocator;

}  // namespace grape

#endif  // GRAPE_COMMUNICATION_COMM_H_