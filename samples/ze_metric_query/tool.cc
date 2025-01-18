//==============================================================
// Copyright (C) Intel Corporation
//
// SPDX-License-Identifier: MIT
// =============================================================

#include <iomanip>
#include <iostream>
#include <limits>
#include <set>

#include "utils.h"
#include "ze_metric_collector.h"

struct Kernel {
  uint64_t inst_alu0 = 0;
  uint64_t inst_alu1 = 0;
  uint64_t inst_xmx = 0;
  uint64_t inst_send = 0;
  uint64_t inst_ctrl = 0;
};

using KernelMap = std::map<std::string, Kernel>;

const uint32_t kKernelLength = 10;
const uint32_t kCallsLength = 12;
const uint32_t kTimeLength = 20;
const uint32_t kPercentLength = 16;

static ZeMetricCollector* collector = nullptr;
static std::chrono::steady_clock::time_point start;

// External Tool Interface ////////////////////////////////////////////////////

extern "C" PTI_EXPORT
void Usage() {
  std::cout <<
    "Usage: ./ze_metric_query[.exe] <application> <args>" <<
    std::endl;
}

extern "C" PTI_EXPORT
int ParseArgs(int argc, char* argv[]) {
  return 1;
}

extern "C" PTI_EXPORT
void SetToolEnv() {
  utils::SetEnv("ZE_ENABLE_TRACING_LAYER", "1");
  utils::SetEnv("ZET_ENABLE_METRICS", "1");
}

// Internal Tool Functionality ////////////////////////////////////////////////

static KernelMap GetKernelMap() {
  PTI_ASSERT(collector != nullptr);
  const KernelReportMap& kernel_report_map = collector->GetKernelReportMap();
  if (kernel_report_map.size() == 0) {
    return KernelMap();
  }

  int inst_alu0_id = collector->GetInstAlu0Id();
  PTI_ASSERT(inst_alu0_id >= 0);
  int inst_alu1_id = collector->GetInstAlu1Id();
  PTI_ASSERT(inst_alu1_id >= 0);
  int inst_xmx_id = collector->GetInstXmxId();
  PTI_ASSERT(inst_xmx_id >= 0);
  int inst_send_id = collector->GetInstSendId();
  PTI_ASSERT(inst_send_id >= 0);
  int inst_ctrl_id = collector->GetInstCtrlId();
  PTI_ASSERT(inst_ctrl_id >= 0);

  KernelMap kernel_map;
  for (auto& kernel : kernel_report_map) {
    std::string kernel_name = kernel.first;
    Kernel kernel_info;

    for (auto& report : kernel.second) {
      uint64_t gpu_time = 0;
      float eu_active = 0.0f, eu_stall = 0.0f;

      PTI_ASSERT(report[inst_alu0_id].type == ZET_VALUE_TYPE_UINT64);
      uint64_t inst_alu0 = report[inst_alu0_id].value.ui64;
      PTI_ASSERT(report[inst_alu1_id].type == ZET_VALUE_TYPE_UINT64);
      uint64_t inst_alu1 = report[inst_alu1_id].value.ui64;
      PTI_ASSERT(report[inst_xmx_id].type == ZET_VALUE_TYPE_UINT64);
      uint64_t inst_xmx = report[inst_xmx_id].value.ui64;
      PTI_ASSERT(report[inst_send_id].type == ZET_VALUE_TYPE_UINT64);
      uint64_t inst_send = report[inst_send_id].value.ui64;
      PTI_ASSERT(report[inst_ctrl_id].type == ZET_VALUE_TYPE_UINT64);
      uint64_t inst_ctrl = report[inst_ctrl_id].value.ui64;

      kernel_info.inst_alu0 += inst_alu0;
      kernel_info.inst_alu1 += inst_alu1;
      kernel_info.inst_xmx += inst_xmx;
      kernel_info.inst_send += inst_send;
      kernel_info.inst_ctrl += inst_ctrl;
    }

    kernel_map[kernel_name] = kernel_info;
  }

  return kernel_map;
}

static void PrintResults() {
  std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
  std::chrono::duration<uint64_t, std::nano> time = end - start;

  KernelMap kernel_map = GetKernelMap();
  if (kernel_map.size() == 0) {
    return;
  }

  std::cerr << std::endl;
  std::cerr << "=== Device Metrics: ===" << std::endl;
  std::cerr << std::endl;

  auto kInstructionLength = 20;
  std::cerr << std::setw(kInstructionLength) << "Kernel" << "," <<
    std::setw(kInstructionLength) << "Inst executed alu0" << "," <<
    std::setw(kInstructionLength) << "Inst executed alu1" << "," <<
    std::setw(kInstructionLength) << "Inst executed xmx" << "," <<
    std::setw(kInstructionLength) << "Inst executed send" << "," <<
    std::setw(kInstructionLength) << "Inst executed ctrl" << std::endl;

  for (auto &[name, val] : kernel_map) {
    std::cerr << std::setw(kInstructionLength) << name << "," <<
        std::setw(kInstructionLength) << val.inst_alu0 << "," <<
        std::setw(kInstructionLength) << val.inst_alu1 << "," <<
        std::setw(kInstructionLength) << val.inst_xmx << "," <<
        std::setw(kInstructionLength) << val.inst_send << "," <<
        std::setw(kInstructionLength) << val.inst_ctrl << std::endl;
  }

  std::cerr << std::endl;
}

// Internal Tool Interface ////////////////////////////////////////////////////

void EnableProfiling() {
  ze_result_t status = ZE_RESULT_SUCCESS;
  status = zeInit(ZE_INIT_FLAG_GPU_ONLY);
  PTI_ASSERT(status == ZE_RESULT_SUCCESS);

  ze_driver_handle_t driver = utils::ze::GetGpuDriver();
  ze_device_handle_t device = utils::ze::GetGpuDevice();
  if (device == nullptr || driver == nullptr) {
    std::cout << "[WARNING] Unable to find target device" << std::endl;
    return;
  }

  collector = ZeMetricCollector::Create(driver, device, "ComputeBasic");
  start = std::chrono::steady_clock::now();
}

void DisableProfiling() {
  if (collector != nullptr) {
    collector->DisableTracing();
    PrintResults();
    delete collector;
  }
}