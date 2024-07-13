//==============================================================
// Copyright (C) Intel Corporation
//
// SPDX-License-Identifier: MIT
// =============================================================

#ifndef PTI_SAMPLES_ZE_METRIC_STREAMER_ZE_METRIC_COLLECTOR_H_
#define PTI_SAMPLES_ZE_METRIC_STREAMER_ZE_METRIC_COLLECTOR_H_

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ze_utils.h"
#include "metric_utils.h"

struct MetricResult {
  uint64_t inst_alu0 = 0;
  uint64_t inst_alu1 = 0;
  uint64_t inst_xmx = 0;
  uint64_t inst_send = 0;
  uint64_t inst_ctrl = 0;
};

enum CollectorState {
  COLLECTOR_STATE_IDLE = 0,
  COLLECTOR_STATE_ENABLED = 1,
  COLLECTOR_STATE_DISABLED = 2
};

class ZeMetricCollector {
 public: // Interface
  static ZeMetricCollector* Create(
      ze_driver_handle_t driver,
      ze_device_handle_t device,
      const char* group_name) {
    PTI_ASSERT(driver != nullptr);
    PTI_ASSERT(device != nullptr);
    PTI_ASSERT(group_name != nullptr);

    if(!utils::metrics::SufficientPrivilegesForMetrics()) {
      std::cerr << "[WARNING] Insufficent or indeterminate privileges for perf "
        << "metrics" << std::endl;
    }

    zet_metric_group_handle_t group = utils::ze::FindMetricGroup(
        device, group_name, ZET_METRIC_GROUP_SAMPLING_TYPE_FLAG_TIME_BASED);
    if (group == nullptr) {
      std::cerr << "[WARNING] Unable to find target metric group: " <<
        group_name << std::endl;
      return nullptr;
    }

    ze_context_handle_t context = utils::ze::GetContext(driver);
    PTI_ASSERT(context != nullptr);

    return new ZeMetricCollector(device, context, group);
  }

  ~ZeMetricCollector() {
    ze_result_t status = ZE_RESULT_SUCCESS;
    PTI_ASSERT(collector_state_ == COLLECTOR_STATE_DISABLED);

    PTI_ASSERT(context_ != nullptr);
    status = zeContextDestroy(context_);
    PTI_ASSERT(status == ZE_RESULT_SUCCESS);
  }

  void DisableCollection() {
    DisableMetrics();
  }

  std::vector<MetricResult> GetMetricsList() const {
    return metric_results_;
  }

 private: // Implementation
  ZeMetricCollector(
      ze_device_handle_t device, ze_context_handle_t context,
      zet_metric_group_handle_t group)
      : device_(device), context_(context), metric_group_(group) {
    PTI_ASSERT(device_ != nullptr);
    PTI_ASSERT(context_ != nullptr);
    PTI_ASSERT(metric_group_ != nullptr);
    SetCollectionConfig();
    SetReportSize();
    SetMetricIndices();
    EnableMetrics();
  }

  void EnableMetrics() {
    PTI_ASSERT(collector_thread_ == nullptr);
    PTI_ASSERT(collector_state_ == COLLECTOR_STATE_IDLE);

    collector_state_.store(COLLECTOR_STATE_IDLE, std::memory_order_release);
    collector_thread_ = new std::thread(Collect, this);
    PTI_ASSERT(collector_thread_ != nullptr);

    while (collector_state_.load(std::memory_order_acquire) !=
          COLLECTOR_STATE_ENABLED) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  void DisableMetrics() {
    PTI_ASSERT(collector_thread_ != nullptr);
    collector_state_.store(
        COLLECTOR_STATE_DISABLED, std::memory_order_release);
    collector_thread_->join();
    delete collector_thread_;
  }

  void SetCollectionConfig() {
    const auto cni = utils::GetEnv("collector_notify_interval");
    if (!cni.empty()) collector_notify_interval = std::stoul(cni);
    const auto csp = utils::GetEnv("collector_sampling_period_ns");
    if (!csp.empty()) collector_sampling_period_ns = std::stoul(csp);
    const auto cd = utils::GetEnv("collector_delay_ns");
    if (!cd.empty()) collector_delay_ns = std::stoull(cd);
    std::cout << "collector_notify_interval: " << collector_notify_interval << "\n";
    std::cout << "collector_sampling_period_ns: " << collector_sampling_period_ns << "\n";
    std::cout << "collector_delay_ns: " << collector_delay_ns << std::endl;
  }

  void SetReportSize() {
    PTI_ASSERT(metric_group_ != nullptr);
    ze_result_t status = ZE_RESULT_SUCCESS;

    zet_metric_group_properties_t group_props{};
    group_props.stype = ZET_STRUCTURE_TYPE_METRIC_GROUP_PROPERTIES;
    status = zetMetricGroupGetProperties(metric_group_, &group_props);
    PTI_ASSERT(status == ZE_RESULT_SUCCESS);
    report_size_ = group_props.metricCount;
  }

  void SetMetricIndices() {
    PTI_ASSERT(metric_group_ != nullptr);
    inst_alu0_id_ = utils::ze::GetMetricId(metric_group_, "XVE_INST_EXECUTED_ALU0_ALL");
    PTI_ASSERT(inst_alu0_id_ > 0);
    inst_alu1_id_ = utils::ze::GetMetricId(metric_group_, "XVE_INST_EXECUTED_ALU1_ALL");
    PTI_ASSERT(inst_alu1_id_ > 0);
    inst_xmx_id_ = utils::ze::GetMetricId(metric_group_, "XVE_INST_EXECUTED_XMX_ALL");
    PTI_ASSERT(inst_xmx_id_ > 0);
    inst_send_id_ = utils::ze::GetMetricId(metric_group_, "XVE_INST_EXECUTED_SEND_ALL");
    PTI_ASSERT(inst_send_id_ > 0);
    inst_ctrl_id_ = utils::ze::GetMetricId(metric_group_, "XVE_INST_EXECUTED_CONTROL_ALL");
    PTI_ASSERT(inst_ctrl_id_ > 0);
  }

  void AppendCalculatedMetrics(const std::vector<uint8_t>& storage) {
    PTI_ASSERT(storage.size() > 0);

    ze_result_t status = ZE_RESULT_SUCCESS;
    std::vector<zet_typed_value_t> report_list;
    PTI_ASSERT(metric_group_ != nullptr);

    if (storage.size() == 0) {
      return;
    }

    uint32_t value_count = 0;
    status = zetMetricGroupCalculateMetricValues(
        metric_group_, ZET_METRIC_GROUP_CALCULATION_TYPE_METRIC_VALUES,
        storage.size(), storage.data(), &value_count, nullptr);
    PTI_ASSERT(status == ZE_RESULT_SUCCESS);
    PTI_ASSERT(value_count > 0);

    report_list.resize(value_count);
    status = zetMetricGroupCalculateMetricValues(
        metric_group_, ZET_METRIC_GROUP_CALCULATION_TYPE_METRIC_VALUES,
        storage.size(), storage.data(),
        &value_count, report_list.data());
    PTI_ASSERT(status == ZE_RESULT_SUCCESS);
    // TODO: Understand why I need to resize report_list after metric calculations.
    // It happens only once, at the beginning of execution.
    report_list.resize(value_count);

    MetricResult metric_result;

    const zet_typed_value_t* report = report_list.data();
    while (report < report_list.data() + report_list.size()) {
      PTI_ASSERT(report[inst_alu0_id_].type == ZET_VALUE_TYPE_UINT64);
      uint64_t inst_alu0 = report[inst_alu0_id_].value.ui64;
      PTI_ASSERT(report[inst_alu1_id_].type == ZET_VALUE_TYPE_UINT64);
      uint64_t inst_alu1 = report[inst_alu1_id_].value.ui64;
      PTI_ASSERT(report[inst_xmx_id_].type == ZET_VALUE_TYPE_UINT64);
      uint64_t inst_xmx = report[inst_xmx_id_].value.ui64;
      PTI_ASSERT(report[inst_send_id_].type == ZET_VALUE_TYPE_UINT64);
      uint64_t inst_send = report[inst_send_id_].value.ui64;
      PTI_ASSERT(report[inst_ctrl_id_].type == ZET_VALUE_TYPE_UINT64);
      uint64_t inst_ctrl = report[inst_ctrl_id_].value.ui64;

      metric_result.inst_alu0 += inst_alu0;
      metric_result.inst_alu1 += inst_alu1;
      metric_result.inst_xmx += inst_xmx;
      metric_result.inst_send += inst_send;
      metric_result.inst_ctrl += inst_ctrl;

      report += report_size_;
    }

    metric_results_.push_back(metric_result);
  }

  static void Collect(ZeMetricCollector* collector) {
    PTI_ASSERT(collector != nullptr);

    PTI_ASSERT(collector->context_ != nullptr);
    PTI_ASSERT(collector->device_ != nullptr);
    PTI_ASSERT(collector->metric_group_ != nullptr);

    ze_result_t status = ZE_RESULT_SUCCESS;
    status = zetContextActivateMetricGroups(
        collector->context_, collector->device_, 1, &collector->metric_group_);
    PTI_ASSERT(status == ZE_RESULT_SUCCESS);

    ze_event_pool_desc_t event_pool_desc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC, nullptr,
        ZE_EVENT_POOL_FLAG_HOST_VISIBLE, 1};
    ze_event_pool_handle_t event_pool = nullptr;
    status = zeEventPoolCreate(collector->context_, &event_pool_desc,
                               0, nullptr, &event_pool);
    PTI_ASSERT(status == ZE_RESULT_SUCCESS);

    ze_event_desc_t event_desc = {
        ZE_STRUCTURE_TYPE_EVENT_DESC, nullptr, 0,
        ZE_EVENT_SCOPE_FLAG_HOST, ZE_EVENT_SCOPE_FLAG_HOST};
    ze_event_handle_t event = nullptr;
    status = zeEventCreate(event_pool, &event_desc, &event);
    PTI_ASSERT(status == ZE_RESULT_SUCCESS);

    zet_metric_streamer_desc_t metric_streamer_desc = {
        ZET_STRUCTURE_TYPE_METRIC_STREAMER_DESC,
        nullptr,
        collector->collector_notify_interval,
        collector->collector_sampling_period_ns};
    zet_metric_streamer_handle_t metric_streamer = nullptr;
    status = zetMetricStreamerOpen(
        collector->context_, collector->device_, collector->metric_group_,
        &metric_streamer_desc, event, &metric_streamer);
    PTI_ASSERT(status == ZE_RESULT_SUCCESS);

    collector->collector_state_.store(
        COLLECTOR_STATE_ENABLED, std::memory_order_release);

    std::vector<uint8_t> storage;
    while (collector->collector_state_.load(std::memory_order_acquire) !=
           COLLECTOR_STATE_DISABLED) {
      status = zeEventHostSynchronize(
          event, collector->collector_delay_ns);
      PTI_ASSERT(status == ZE_RESULT_SUCCESS || status == ZE_RESULT_NOT_READY);

      size_t data_size = 0;
      status = zetMetricStreamerReadData(
          metric_streamer, UINT32_MAX, &data_size, nullptr);
      PTI_ASSERT(status == ZE_RESULT_SUCCESS);

      if (data_size > 0) {
        storage.resize(data_size);
        status = zetMetricStreamerReadData(
            metric_streamer, UINT32_MAX, &data_size, storage.data());
        PTI_ASSERT(status == ZE_RESULT_SUCCESS);
        storage.resize(data_size);
        if (storage.size() > 0) collector->AppendCalculatedMetrics(storage);
      }
    }

    status = zetMetricStreamerClose(metric_streamer);
    PTI_ASSERT(status == ZE_RESULT_SUCCESS);

    status = zeEventDestroy(event);
    PTI_ASSERT(status == ZE_RESULT_SUCCESS);
    status = zeEventPoolDestroy(event_pool);
    PTI_ASSERT(status == ZE_RESULT_SUCCESS);

    status = zetContextActivateMetricGroups(
        collector->context_, collector->device_, 0, nullptr);
    PTI_ASSERT(status == ZE_RESULT_SUCCESS);
  }

 private: // Data
  ze_device_handle_t device_ = nullptr;
  ze_context_handle_t context_ = nullptr;

  std::thread* collector_thread_ = nullptr;
  std::atomic<CollectorState> collector_state_{COLLECTOR_STATE_IDLE};

  zet_metric_group_handle_t metric_group_ = nullptr;
  std::vector<MetricResult> metric_results_;

  uint32_t report_size_;
  int inst_alu0_id_;
  int inst_alu1_id_;
  int inst_xmx_id_;
  int inst_send_id_;
  int inst_ctrl_id_;

 public:
  uint32_t collector_notify_interval = 32768;
  uint32_t collector_sampling_period_ns = 100000;
  uint64_t collector_delay_ns = 50000000;
};

#endif // PTI_SAMPLES_ZE_METRIC_STREAMER_ZE_METRIC_COLLECTOR_H_
