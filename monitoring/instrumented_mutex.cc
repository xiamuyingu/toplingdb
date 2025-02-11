//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include "monitoring/instrumented_mutex.h"

#include "monitoring/perf_context_imp.h"
#include "monitoring/thread_status_util.h"
#include "rocksdb/system_clock.h"
#include "test_util/sync_point.h"

namespace ROCKSDB_NAMESPACE {
namespace {
#ifndef NPERF_CONTEXT
static inline
Statistics* stats_for_report(SystemClock* clock, Statistics* stats) {
  if (clock != nullptr && stats != nullptr &&
      stats->get_stats_level() > kExceptTimeForMutex) {
    return stats;
  } else {
    return nullptr;
  }
}
#endif  // NPERF_CONTEXT
}  // namespace

#ifdef __GNUC__
__attribute__((flatten))
#endif
void InstrumentedMutex::Lock() {
  PERF_TIMER_MUTEX_WAIT_GUARD(
      db_mutex_lock_nanos, stats_for_report(clock_, stats_));
  LockInternal();
}

void InstrumentedMutex::LockInternal() {
#ifndef NDEBUG
  ThreadStatusUtil::TEST_StateDelay(ThreadStatus::STATE_MUTEX_WAIT);
#endif
  mutex_.Lock();
}

void InstrumentedCondVar::Wait() {
  PERF_TIMER_COND_WAIT_GUARD(
      db_condition_wait_nanos, stats_for_report(clock_, stats_));
  WaitInternal();
}

void InstrumentedCondVar::WaitInternal() {
#ifndef NDEBUG
  ThreadStatusUtil::TEST_StateDelay(ThreadStatus::STATE_MUTEX_WAIT);
#endif
  cond_.Wait();
}

bool InstrumentedCondVar::TimedWait(uint64_t abs_time_us) {
  PERF_TIMER_COND_WAIT_GUARD(
      db_condition_wait_nanos, stats_for_report(clock_, stats_));
  return TimedWaitInternal(abs_time_us);
}

bool InstrumentedCondVar::TimedWaitInternal(uint64_t abs_time_us) {
#ifndef NDEBUG
  ThreadStatusUtil::TEST_StateDelay(ThreadStatus::STATE_MUTEX_WAIT);
#endif

  TEST_SYNC_POINT_CALLBACK("InstrumentedCondVar::TimedWaitInternal",
                           &abs_time_us);

  return cond_.TimedWait(abs_time_us);
}

}  // namespace ROCKSDB_NAMESPACE
