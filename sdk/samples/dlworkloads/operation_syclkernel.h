//==============================================================
// Copyright (C) Intel Corporation
//
// SPDX-License-Identifier: MIT
// =============================================================
#ifndef OPERATION_SYCLKERNEL_H_
#define OPERATION_SYCLKERNEL_H_

#include <sycl/sycl.hpp>
#include "tiny_tensor.h"

TinyTensor run_syclkernel_operation_scaledown(const TinyTensor& inp, sycl::queue *q);

#endif
