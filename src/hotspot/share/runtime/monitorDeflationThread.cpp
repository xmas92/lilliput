/*
 * Copyright (c) 2020, 2024, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "classfile/javaClasses.hpp"
#include "classfile/vmClasses.hpp"
#include "classfile/vmSymbols.hpp"
#include "logging/log.hpp"
#include "logging/logMessage.hpp"
#include "logging/logStream.hpp"
#include "memory/universe.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/java.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/lightweightSynchronizer.hpp"
#include "runtime/monitorDeflationThread.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/objectMonitor.hpp"
#include "runtime/orderAccess.hpp"
#include "runtime/synchronizer.hpp"
#include "utilities/checkedCast.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/numberSeq.hpp"
#include <cmath>

void MonitorDeflationThread::initialize() {
  EXCEPTION_MARK;

  const char* name = "Monitor Deflation Thread";
  Handle thread_oop = JavaThread::create_system_thread_object(name, CHECK);

  MonitorDeflationThread* thread = new MonitorDeflationThread(&monitor_deflation_thread_entry);
  JavaThread::vm_exit_on_osthread_failure(thread);

  JavaThread::start_internal_daemon(THREAD, thread, thread_oop, NearMaxPriority);
}

void MonitorDeflationThread::monitor_deflation_thread_entry_placeholder(JavaThread* jt, intx wait_time, TRAPS) {
  if (wait_time == max_intx) {
    warning("Async deflation is disabled");
    // TODO: It is never clear what the expected size of the table should be
    //       when deflation is disabled. Any ObjectMonitors are effectively
    //       leaked.
    LightweightSynchronizer::set_table_max(jt);
    return;
  }

  intx time_to_wait = wait_time;
  size_t deflated = 0;
  size_t inflated = 0;
  ssize_t count = 0;
  bool was_deflated = false;
  TruncatedSeq count_seq;
  TruncatedSeq deflated_seq;
  TruncatedSeq inflated_seq;
  TruncatedSeq deflation_rates;
  TruncatedSeq deflation_speeds;
  TruncatedSeq inflation_rates;
  TruncatedSeq deflation_times;
  TruncatedSeq deflation_cycles;
  jlong time_since_last = 0;
  jlong deflation_time = 0;

  while (true) {
    bool resize = false;
    bool deflate = false;
    {
      ThreadBlockInVM tbivm(jt);
      LogMessage(monitorinflation) lm;

      MonitorLocker ml(MonitorDeflation_lock, Mutex::_no_safepoint_check_flag);
      while (true) {
        if (ObjectSynchronizer::is_async_deflation_needed()) {
          deflate = true;
        }
        const bool resize_requested = LightweightSynchronizer::resize_requested(jt);
        OrderAccess::loadload();
        if (LightweightSynchronizer::needs_resize(jt)) {
          resize = true;
        } else if (resize_requested) {
          // TODO: What do we do here?
          lm.info("Warning, received grow hint without resizing needed.");
        }

        if (deflate || resize) {
          // Do the work
          break;
        }

        // Wait until notified that there is some work to do.
        ml.wait(time_to_wait);

        if (was_deflated) {
          // Reset
          was_deflated = false;

          // Heuristic testing
          NonInterleavingLogStream ls(LogLevel::Info, lm);
          const double deflation_time_in_seconds = (double)deflation_time / NANOUNITS;
          const double deflation_cycle_in_seconds = (double)time_since_last / NANOUNITS;

          const double deflation_rate = deflated / deflation_cycle_in_seconds;
          const double deflation_speed = deflated / deflation_time_in_seconds;
          const double inflation_rate = inflated / deflation_cycle_in_seconds;

          count_seq.add(ObjectSynchronizer::in_use_list_count());
          inflated_seq.add(inflated);
          deflated_seq.add(deflated);
          deflation_rates.add(deflation_rate);
          deflation_speeds.add(deflation_speed);
          inflation_rates.add(inflation_rate);
          deflation_times.add(deflation_time_in_seconds);
          deflation_cycles.add(deflation_cycle_in_seconds);

          auto printer = [&](auto& seq, const char* name) {
            ls.print_cr("%s: added: %f predicted: %f", name, seq.last(), seq.predict_next());
            seq.AbsSeq::dump_on(&ls);
          };

#define A_PRINTER(a) printer(a, #a)
          A_PRINTER(count_seq);
          A_PRINTER(inflated_seq);
          A_PRINTER(deflated_seq);
          A_PRINTER(deflation_rates);
          A_PRINTER(deflation_speeds);
          A_PRINTER(inflation_rates);
          A_PRINTER(deflation_times);
          A_PRINTER(deflation_cycles);
#undef A_PRINTER

        }
      }
    }

    if (resize) {
      const intx time_since_last_deflation = checked_cast<intx>(ObjectSynchronizer::time_since_last_async_deflation_ms());
      const bool resize_successful = LightweightSynchronizer::resize_table(jt);
      const bool wait_time_passed = time_since_last_deflation >= wait_time;

      if (!resize_successful) {
        // Resize failed, try again in 250 ms
        time_to_wait = 250;
      } else if (wait_time_passed) {
        // Reset back to original wait_time
        time_to_wait = wait_time;
      } else {
        // Respect original wait_time. Wait out the remainder.
        time_to_wait = wait_time - time_since_last_deflation;
      }
    } else {
      // Reset back to original wait_time
      time_to_wait = wait_time;
    }

    if (deflate) {
      time_since_last = ObjectSynchronizer::time_since_last_async_deflation_ns();
      size_t new_count = ObjectSynchronizer::in_use_list_count();
      inflated = new_count - count;
      constexpr double one_in_1000 = 3.290527;
      const double worst_inflation_rate = inflation_rates.avg() + one_in_1000 * inflation_rates.sd();
      const double worst_inflation = worst_inflation_rate * deflation_cycles.avg();
      const double in_use_list_target = ObjectSynchronizer::in_use_list_ceiling() * (0.01 * MonitorUsedDeflationThreshold);
      const double target = MAX2(0.0, in_use_list_target - worst_inflation);

      log_info(omworld)("Inf: %f, Aim: %f, Tar: %f", worst_inflation, in_use_list_target, target);

      deflated = ObjectSynchronizer::deflate_idle_monitors(checked_cast<size_t>(round(target)));
      deflation_time =  ObjectSynchronizer::time_since_last_async_deflation_ns();;
      count = new_count - deflated;
      was_deflated = true;
    }

  }
}

void MonitorDeflationThread::monitor_deflation_thread_entry(JavaThread* jt, TRAPS) {

  // We wait for the lowest of these three intervals:
  //  - GuaranteedSafepointInterval
  //      While deflation is not related to safepoint anymore, this keeps compatibility with
  //      the old behavior when deflation also happened at safepoints. Users who set this
  //      option to get more/less frequent deflations would be served with this option.
  //  - AsyncDeflationInterval
  //      Normal threshold-based deflation heuristic checks the conditions at this interval.
  //      See is_async_deflation_needed().
  //  - GuaranteedAsyncDeflationInterval
  //      Backup deflation heuristic checks the conditions at this interval.
  //      See is_async_deflation_needed().
  //
  intx wait_time = max_intx;
  if (GuaranteedSafepointInterval > 0) {
    wait_time = MIN2(wait_time, GuaranteedSafepointInterval);
  }
  if (AsyncDeflationInterval > 0) {
    wait_time = MIN2(wait_time, AsyncDeflationInterval);
  }
  if (GuaranteedAsyncDeflationInterval > 0) {
    wait_time = MIN2(wait_time, GuaranteedAsyncDeflationInterval);
  }

  if (LockingMode == LM_LIGHTWEIGHT) {
    monitor_deflation_thread_entry_placeholder(jt, wait_time, THREAD);
    return;
  }

  // If all options are disabled, then wait time is not defined, and the deflation
  // is effectively disabled. In that case, exit the thread immediately after printing
  // a warning message.
  if (wait_time == max_intx) {
    warning("Async deflation is disabled");
    LightweightSynchronizer::set_table_max(jt);
    return;
  }

  while (true) {
    {
      // TODO[OMWorld]: This is all being rewritten.
      // Need state transition ThreadBlockInVM so that this thread
      // will be handled by safepoint correctly when this thread is
      // notified at a safepoint.

      ThreadBlockInVM tbivm(jt);

      MonitorLocker ml(MonitorDeflation_lock, Mutex::_no_safepoint_check_flag);
      while (!ObjectSynchronizer::is_async_deflation_needed()) {
        // Wait until notified that there is some work to do.
        ml.wait(wait_time);
      }
    }

    (void)ObjectSynchronizer::deflate_idle_monitors();

    if (log_is_enabled(Debug, monitorinflation)) {
      // The VMThread calls do_final_audit_and_print_stats() which calls
      // audit_and_print_stats() at the Info level at VM exit time.
      LogStreamHandle(Debug, monitorinflation) ls;
      ObjectSynchronizer::audit_and_print_stats(&ls, false /* on_exit */);
    }
  }
}
