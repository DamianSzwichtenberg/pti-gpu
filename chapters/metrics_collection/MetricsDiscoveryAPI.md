# Metrics Collection with Intel(R) Metrics Discovery Application Programming Interface

**WARNING: this is NOT a recommended way for metrics collection, use [oneAPI L0 Metrics API](./LevelZero.md) instead**

## Overview
[Intel(R) Metrics Discovery Application Programming Interface](https://github.com/intel/metrics-discovery) is an open-source solution for hardware metrics collection on Intel(R) Processor Graphics.

Each **metric** represents one aspect of GPU performance behavior and has a unique name, e.g. "EuActive" metric shows the percentage of time GPU execution units were active, "L3Misses" gives the number of L3 cache misses, and "GtiReadThroughput" collects an average throughput while reading data through GTI.

Different metrics are combined into **metric sets**. A metric set is a unit of collection, so one can collect only a set of metrics instead of each metric separately.

Metric sets in their turn belongs to **metric groups**. Metric sets from different groups can be collected simultaneously, while only a single set from a group can be enabled at a time.

**Supported OS**:
- Linux
- Windows

**Supported HW**:
- Intel(R) Processor Graphics GEN9+

**Needed Headers**:
- [metrics_discovery_api.h](https://github.com/intel/metrics-discovery/blob/master/inc/common/instrumentation/api/metrics_discovery_api.h)

**Needed Libraries**:
- Intel(R) Metrics Discovery Application Programming Interface [library](https://github.com/intel/metrics-discovery)

## How To Use
Recommended flow while working with Metrics Discovery (MD) API is the following:
1. Load MD library dynamically using `dlopen`/`LoadLibrary`, get `OpenMetricsDevice` symbol with `dlsym`/`GetProcAddress`, call it and create a device;
2. Using device interfaces, enumerate all the metrics and choose target metric sets to collect;
3. Enable collection and store raw results into memory;
4. After collection finished, calculate and show actual metric values;
5. Release resources and unload MD library.

All the interfaces are described in [metrics_discovery_api.h](https://github.com/intel/metrics-discovery/blob/master/inc/common/instrumentation/api/metrics_discovery_api.h) file.

### Device Creation
MD device creation on Linux may look like this:
```cpp
#include <metrics_discovery_api.h>
namespace md = MetricsDiscovery;

// ...

const char* name = "libigdmd.so";
void* handle = dlopen(name, RTLD_NOW);
assert(handle != nullptr);

md::OpenAdapterGroup_fn OpenAdapterGroup =
  (md::OpenAdapterGroup_fn)dlsym(handle, "OpenAdapterGroup");
assert(OpenAdapterGroup != nullptr);

md::IAdapterGroupLatest* adapter_group = nullptr;
status = OpenAdapterGroup(&adapter_group);
PTI_ASSERT(status == md::CC_OK);

md::IAdapterLatest* adapter = adapter_group->GetAdapter(0 /* device id*/);
PTI_ASSERT(adapter != nullptr);

uint32_t sub_devices_count = adapter->GetParams()->SubDevicesCount;
if (sub_devices_count == 0) {
  status = adapter->OpenMetricsDevice(&device);
} else {
  status = adapter->OpenMetricsSubDevice(0 /* sub-device id*/, &device);
}
PTI_ASSERT(status == md::CC_OK || status == md::CC_ALREADY_INITIALIZED);

// Enumerate and/or collect metrics

status = adapter_->CloseMetricsDevice(device_);
PTI_ASSERT(status == md::CC_OK);
status = adapter_group_->Close();
PTI_ASSERT(status == md::CC_OK);

dlclose(handle);
```

### Device Information
MD device object may contain some hardware related information about the GPU in global symbols (like device name, threads/execution untis/slices/subslices count, frequencies, platform identifier, etc.). To get them one may use the following code:
```cpp
std::cout << "Device Name: " << device->GetParams()->DeviceName << std::endl;
for (uint32_t i = 0; i < device->GetParams()->GlobalSymbolsCount; ++i) {
  md::TGlobalSymbol_1_0* symbol = device->GetGlobalSymbol(i);
  std::cout << symbol->SymbolName << ": ";
  switch (symbol->SymbolTypedValue.ValueType) {
    case md::VALUE_TYPE_UINT32:
      std::cout << symbol->SymbolTypedValue.ValueUInt32;
      break;
    case md::VALUE_TYPE_UINT64:
      std::cout << symbol->SymbolTypedValue.ValueUInt64;
      break;
    case md::VALUE_TYPE_FLOAT:
      std::cout << symbol->SymbolTypedValue.ValueFloat;
      break;
    case md::VALUE_TYPE_BOOL:
      std::cout << (symbol->SymbolTypedValue.ValueBool ? "true" : "false");
      break;
    case md::VALUE_TYPE_CSTRING:
      std::cout << symbol->SymbolTypedValue.ValueCString;
      break;
    default:
      break;
  }
  std::cout << std::endl;
}
```

### Enumeration
To enumerate metrics available on the system one has to iterate over all metric groups, next over all metric sets inside each group and finally over all metrics inside each set.

Every group, set, and separate metric contain a list of properties that helps to determine their purpose (like name or description) as well as to grab some additional information that should be used while collecting (e.g. size of collected data chunks or number of metrics in a set).

In addition to metrics, each set includes so called *information* items, that will be collected along with the metrics and show e.g. time when sample was collected, GPU core frequency, report identifier and others.
```cpp
for (uint32_t gid = 0; gid < device->GetParams()->ConcurrentGroupsCount; ++gid) {
  md::IConcurrentGroupLatest* group = device->GetConcurrentGroup(gid);
  assert(group != nullptr);
  std::cout << "Metric Group " << gid << ": " <<
      group->GetParams()->SymbolName << std::endl;

  for (uint32_t sid = 0; sid < group->GetParams()->MetricSetsCount; ++sid) {
    md::IMetricSetLatest* set = group->GetMetricSet(sid);
    assert(set != nullptr);
    std::cout << "\tMetric Set " << sid << ": " <<
      set->GetParams()->SymbolName << " (" << set->GetParams()->ShortName <<
      ")" << std::endl;
    
    for (uint32_t mid = 0; mid < set->GetParams()->MetricsCount; ++mid) {
      md::IMetricLatest* metric = set->GetMetric(mid);
      assert(metric != nullptr);
      std::cout << "\t\tMetric " << mid << ": " <<
        metric->GetParams()->SymbolName << " (" <<
        metric->GetParams()->ShortName << ")" << std::endl;
    }

    for (uint32_t iid = 0; iid < set->GetParams()->InformationCount; ++iid) {
      md::IInformation_1_0* info = set->GetInformation(iid);
      assert(info != nullptr);
      std::cout << "\t\tInfo " << iid + set->GetParams()->MetricsCount <<
        ": " << info->GetParams()->SymbolName << " (" <<
        info->GetParams()->ShortName << ")" << std::endl;
    }
  }
}
```

### Continuous Collection
The process of metrics collection with Intel(R) Metrics Discovery Application Programming Interface assumes that there is an infinite loop in a separate thread, where one asks for collected samples periodically, read the data for a chunk of samples and store them into some memory or file (one sample contains all the metics and information items from a metric set).

First, one should set sampling interval for collection (in nanoseconds) and determine the size of the buffer with raw results (in bytes).

Sampling interval shouldn't be too small - it's recommended to make it at least 100 microseconds. In general, smaller interval leads to more data and higher overhead.

MD library will periodically give one a chunk with several samples, so the buffer should be large enough to store the whole chunk. One of the strategies here may be to leave this size zero and get the MD library to decide which size is appropriate.
```cpp
uint32_t sampling_interval = 100000; // nanoseconds
uint32_t buffer_size = 0; // will be defined by MDAPI
md::TCompletionCode status = md::CC_OK;
```
Next, one should set a type of collection for target metric set. MD library allows to work with multiple APIs, like OpenGL, DirectX, Metal and others, so it's needed to determine explicitly which type of API one wants to employ. In addition to exact API, it's possible to enable the
most general collection using `API_TYPE_IOSTREAM` flag.
```cpp
md::IMetricSetLatest* set; // target metric set, see Enumeration section
status = set->SetApiFiltering(md::API_TYPE_IOSTREAM);
assert(status == md::CC_OK);
```
To start collection, call `OpenIoStream` for the group, target metric set belongs to. Multiple groups can be enabled at a time.

As a result, both `sampling_interval` and `buffer_size` may be updated by the values that will be actually used while collecting. Based on the new `buffer_size` one should allocate a buffer.

Note, that the call may fail due to lack of access rights for the current user (try "root" on Linux), or too small sampling interval value. Refer to
[Metrics Discovery (MD) API](https://github.com/intel/metrics-discovery) project to get more information.
```cpp
md::IConcurrentGroupLatest* group; // target metric group, see Enumeration section
status = group->OpenIoStream(set, 0, &sampling_interval, &buffer_size);
assert (status == md::CC_OK && buffer_size > 0);

std::vector<char> buffer(buffer_size, 0);
```
Finally, to grab collected data, one should make a loop within a separate thread and periodically check for the samples. MD library has a function `WaitForReports` that allows to pause thread execution until any samples will be collected.

To read collected reports (one report represents a sample), one should call `ReadIoStream` function with preallocated buffer. Actual number of collected reports will be returned.
```cpp
uint32_t max_report_count = buffer_size / set->GetParams()->RawReportSize;
uint32_t wait_time = 500; // milliseconds
while (true) {
    status = group->WaitForReports(wait_time);
    assert(status == md::CC_OK || status == md::CC_WAIT_TIMEOUT ||
           status == md::CC_INTERRUPTED);

    uint32_t report_count = max_report_count;
    status = group->ReadIoStream(&report_count, buffer.data()
                                 md::IO_READ_FLAG_DROP_OLD_REPORTS);
    assert(status == md::CC_OK || status == md::CC_READ_PENDING);

    if (report_count > 0) {
      // Store buffer into the memory or file
    }
}
```
When metrics collection is not needed any more, one should disable it by calling `CloseIoStream` for the target group:
```cpp
status = group->CloseIoStream();
assert(status == md::CC_OK);
```

### Calculation
Metric reports collected at the previous stages are in raw format and should be post-calculated to become user-readable. To perform calculations one should call `CalculateMetrics` function
for target metric set, which gives actual number of calculated reports. Note, that this value may differ from the number of raw reports.

Each calculated report contains an array of metric values and an array of information values, one right after another in memory. An order of values directly corresponds to the order of metrics one can learn while enumeration.

```cpp
std::vector<uint8_t> storage; // Collected samples in raw format

uint32_t raw_report_count = storage.size() / set->GetParams()->RawReportSize;
assert(storage.size() == raw_report_count * set->GetParams()->RawReportSize);

uint32_t calculated_report_count = 0;
uint32_t calculated_report_size =
  set->GetParams()->MetricsCount + set->GetParams()->InformationCount;
std::vector<md::TTypedValue_1_0> calculated_reports(
    calculated_report_size * raw_report_count);

status = set->CalculateMetrics(
    storage.data(), storage.size(), calculated_reports.data(),
    calculated_reports.size() * sizeof(md::TTypedValue_1_0),
    &calculated_report_count, nullptr, 0);
assert(status == md::CC_OK);

calculated_reports.resize(calculated_report_count * calculated_report_size);
```

### Time Correlation
It's often needed to map collected hardware metrics to a kernel in terms of time intervals.

Each metric set contains a special *information* item called `QueryBeginTime` that represents a timestamp (in nanoseconds) for a sample. At the same time one can collect kernel execution intervals using ***Device Activity Tracing*** capabilities. So to map exact sample to the kernel invocation, one need just to check if sample timestamp is in between of kernel start and end timestamps.

The problem is that metrics timestamps one can get with Intel(R) Metrics Discovery Application Programming Interface and kernel timestamps one can get e.g. with OpenCL(TM) or with oneAPI Level Zero (Level Zero) are different and can't be compared directly - so one has to convert them to a single time format first.

In Intel(R) Metrics Discovery Application Programming Interface library there is a function `GetGpuCpuTimestamps` that allows to bind GPU metrics timestamp to some CPU timestamp (which is based on `CLOCK_MONOTONIC` on Linux and `QueryPerformanceCounter` on Windows).

So the common strategy of metrics to kernels mapping is the following:
1. Convert `QueryBeginTime` into CPU timestamp with the help of `GetGpuCpuTimestamps` of Intel(R) Metrics Discovery Application Programming Interface library;
2. Convert kernel timestamp into host timestamp:
    - for OpenCL(TM) - with the help of `clGetDeviceAndHostTimer` function (Time Correlation section [here](../device_activity_tracing/OpenCL.md));
    - for oneAPI Level Zero (Level Zero) - with the help of `zeDeviceGetGlobalTimestamps` function (Time Correlation section [here](../device_activity_tracing/LevelZero.md));
    - on Linux one may need to convert `CLOCK_MONOTONIC_RAW` into `CLOCK_MONOTONIC` to use the same time units;
3. Compare directly metric CPU timestamp with kernel host start and host end timestamps to perform metrics to kernel correlation.

### Query-Based Collection for OpenCL(TM)
An alternative approach could be to collect a single aggregated metric report per each kernel invocation. In some sense such a way may be easier than time-based collection, since one don't need to worry about time correlation (report is already for the kernel) and to deal with separate thread (runtime takes most of the responsibilities on data collection), but from the other hand one will get the only aggregated report per kernel (that may be not enough to analyse over time kernel behaviour). Also such approach is limited to support only specific runtimes (e.g. OpenCL(TM)).

To enable query-based mertrics collection for OpenCL(TM) one should perform the following steps:
1. Create MD device and choose target metric set (as described above);
2. Set API filtering mode to OpenCL(TM) and activate metric set:
```cpp
md::TCompletionCode status = set_->SetApiFiltering(
    md::API_TYPE_OCL | md::API_TYPE_OGL4_X);
assert(status == md::CC_OK);
status = set_->Activate();
assert(status == md::CC_OK);
```
3. To be able to retrieve metrics for a kernel, one needs to create a specific command queue with the help of extension. The argument `configuration` here could be obtained from the target metric set. Note, that `CL_QUEUE_PROFILING_ENABLE` property is required for such a queue:
```cpp
cl_command_queue CL_API_CALL
clCreatePerfCountersCommandQueueINTEL(
    cl_context context,
    cl_device_id device,
    cl_command_queue_properties properties,
    cl_uint configuration,
    cl_int *errcodeRet);
// ...
cl_uint configuration = set_->GetParams()->ApiSpecificId.OCL;
```
4. Metric report for specific kernel could be retrieved as event profiling info:
```cpp
#define CL_PROFILING_COMMAND_PERFCOUNTERS_INTEL 0x407F

size_t report_size = set_->GetParams()->QueryReportSize;
PTI_ASSERT(report_size > 0);

std::vector<uint8_t> report(report_size, 0);
size_t output_size = 0;
cl_int status = clGetEventProfilingInfo(
    event, CL_PROFILING_COMMAND_PERFCOUNTERS_INTEL,
    report_size, report.data(), &output_size);
assert(status == CL_SUCCESS);
```
5. Report data grabbed from `clGetEventProfilingInfo` should be calculated into metrics (described above). There will be a single metric report.
6. To finalize data collection one should deactive target metric set and remove MD device:
```cpp
md::TCompletionCode status = set_->Deactivate();
assert(status == md::CC_OK);
```
Query-based metrics collection for Level Zero is described [here](./LevelZero.md).

## Build and Run
Since Intel(R) Metrics Discovery Application Programming Interface library is loaded dynamically at runtime, there is no need for any special build/run options. Just make sure Intel(R) Metrics Discovery Application Programming Interface library can be found correctly:
```sh
LD_LIBRARY_PATH=$LD_LIBRARY_PATH:<path_to_libigdmd.so> ./<application>
```

## Usage Details
- refer to the Intel(R) Metrics Discovery Application Programming Interface project [page](https://github.com/intel/metrics-discovery) to learn more on metrics collection for Intel(R) Processor Graphics
- refer to
[Compute Architecture Manuals](https://software.intel.com/en-us/articles/intel-graphics-developers-guides) to learn more on Intel(R) Processor Graphics Architecture

## Samples
- [GPU Metrics for OpenCL(TM)](../../samples/cl_gpu_metrics)
- [GPU Query for OpenCL(TM)](../../samples/cl_gpu_query)

## Tools
- [GPU Info](../../tools/gpuinfo)