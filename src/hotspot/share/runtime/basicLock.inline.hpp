/*
 * Copyright (c) 2024, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_RUNTIME_BASICLOCK_INLINE_HPP
#define SHARE_RUNTIME_BASICLOCK_INLINE_HPP

#include "runtime/basicLock.hpp"

inline markWord BasicLock::displaced_header() const {
  assert(LockingMode == LM_LEGACY, "must be");
  return markWord(get_metadata());
}

inline void BasicLock::set_displaced_header(markWord header) {
  assert(LockingMode == LM_LEGACY, "must be");
  Atomic::store(&_metadata, header.value());
}

inline ObjectMonitor* BasicLock::object_monitor_cache() const {
  assert(LockingMode == LM_LIGHTWEIGHT, "must be");
  return reinterpret_cast<ObjectMonitor*>(get_metadata());
}

inline void BasicLock::clear_object_monitor_cache() {
  assert(LockingMode == LM_LIGHTWEIGHT, "must be");
  set_metadata(0);
}

inline void BasicLock::set_object_monitor_cache(ObjectMonitor* mon) {
  assert(LockingMode == LM_LIGHTWEIGHT, "must be");
  set_metadata(reinterpret_cast<uintptr_t>(mon));
}

#endif // SHARE_RUNTIME_BASICLOCK_INLINE_HPP
