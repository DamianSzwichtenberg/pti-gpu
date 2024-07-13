//==============================================================
// Copyright (C) Intel Corporation
//
// SPDX-License-Identifier: MIT
// =============================================================


#include <iomanip>
#include <iostream>

#include "ze_metric_collector.h"

const uint32_t kInstructionLength = 20;

static ZeMetricCollector* metric_collector = nullptr;

static std::chrono::steady_clock::time_point start;

// External Tool Interface ////////////////////////////////////////////////////

extern "C" PTI_EXPORT
void Usage() {
  std::cout <<
    "Usage: ./ze_metric_streamer[.exe] <application> <args>" <<
    std::endl;
}

extern "C" PTI_EXPORT
int ParseArgs(int argc, char* argv[]) {
  return 1;
}

extern "C" PTI_EXPORT
void SetToolEnv() {
  utils::SetEnv("ZET_ENABLE_METRICS", "1");
  utils::SetEnv("PTI_DEVICE_ID", "0");
  utils::SetEnv("PTI_SUB_DEVICE_ID", "0");
}

// Internal Tool Functionality ////////////////////////////////////////////////

static MetricResult GetMetricResult() {
  PTI_ASSERT(metric_collector != nullptr);

  std::vector<MetricResult> metric_list =
    metric_collector->GetMetricsList();
  std::cout << "Performed " << metric_list.size() << " samplings." << std::endl;

  MetricResult metric_result;

  for (const auto& metric : metric_list) {
    metric_result.inst_alu0 += metric.inst_alu0;
    metric_result.inst_alu1 += metric.inst_alu1;
    metric_result.inst_xmx += metric.inst_xmx;
    metric_result.inst_send += metric.inst_send;
    metric_result.inst_ctrl += metric.inst_ctrl;
  }

  return metric_result;
}

static void PrintResults() {
  std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
  std::chrono::duration<uint64_t, std::nano> time = end - start;

  MetricResult metric_result = GetMetricResult();

  std::cerr << std::endl;
  std::cerr << "=== Device Metrics: ===" << std::endl;
  std::cerr << std::endl;

  std::cerr << std::setw(kInstructionLength) << "Inst executed alu0" << "," <<
    std::setw(kInstructionLength) << "Inst executed alu1" << "," <<
    std::setw(kInstructionLength) << "Inst executed xmx" << "," <<
    std::setw(kInstructionLength) << "Inst executed send" << "," <<
    std::setw(kInstructionLength) << "Inst executed ctrl" << std::endl;

  std::cerr << std::setw(kInstructionLength) << metric_result.inst_alu0 << "," <<
    std::setw(kInstructionLength) << metric_result.inst_alu1 << "," <<
    std::setw(kInstructionLength) << metric_result.inst_xmx << "," <<
    std::setw(kInstructionLength) << metric_result.inst_send << "," <<
    std::setw(kInstructionLength) << metric_result.inst_ctrl << std::endl;

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

  metric_collector =
    ZeMetricCollector::Create(driver, device, "ComputeBasic");
  if (metric_collector == nullptr) {
    return;
  }

  start = std::chrono::steady_clock::now();
}

void DisableProfiling() {
  if (metric_collector != nullptr) {
    PTI_ASSERT(metric_collector != nullptr);
    metric_collector->DisableCollection();
    PrintResults();
    delete metric_collector;
  }
}
