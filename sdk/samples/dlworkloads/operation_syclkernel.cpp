//==============================================================
// Copyright (C) Intel Corporation
//
// SPDX-License-Identifier: MIT
// =============================================================
#include <iostream>
#include "operation_syclkernel.h"

TinyTensor run_syclkernel_operation_scaledown(const TinyTensor& inp, sycl::queue *q)
{
    TinyTensor outp(inp.N, inp.C, inp.H / 2, inp.W / 2);

    float *src = inp.data;
    float *dst = outp.data;

    q->submit([&](sycl::handler &h) {
        h.parallel_for(outp.count(), [=](sycl::item<1> item) {
            int idx = item.get_id(0);
            dst[idx] = src[idx*4];
        });
    });

    return outp;
}
