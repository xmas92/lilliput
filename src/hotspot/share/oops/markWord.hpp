/*
 * Copyright (c) 1997, 2023, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_OOPS_MARKWORD_HPP
#define SHARE_OOPS_MARKWORD_HPP

#include "gc/shared/gc_globals.hpp"
#include "metaprogramming/primitiveConversions.hpp"
#include "oops/compressedKlass.hpp"
#include "oops/oopsHierarchy.hpp"
#include "runtime/globals.hpp"
#include "utilities/globalDefinitions.hpp"

#include <type_traits>

// The markWord describes the header of an object.
//
// Bit-format of an object header (most significant first, big endian layout below):
//
//  32 bits:
//  --------
//             hash:25 ------------>| age:4  unused_gap:1  lock:2 (normal object)
//
//  64 bits:
//  --------
//  unused:25 hash:31 -->| unused_gap:1  age:4  unused_gap:1  lock:2 (normal object)
//
//  64 bits (with compact headers):
//  -------------------------------
//  nklass:32 hash:25 -->| unused_gap:1  age:4  self-fwded:1  lock:2 (normal object)
//
//  - hash contains the identity hash value: largest value is
//    31 bits, see os::random().  Also, 64-bit vm's require
//    a hash value no bigger than 32 bits because they will not
//    properly generate a mask larger than that: see library_call.cpp
//
//  - the two lock bits are used to describe three states: locked/unlocked and monitor.
//
//    [ptr             | 00]  locked             ptr points to real header on stack (stack-locking in use)
//    [header          | 00]  locked             locked regular object header (fast-locking in use)
//    [header          | 01]  unlocked           regular object header
//    [ptr             | 10]  monitor            inflated lock (header is swapped out)
//    [ptr             | 11]  marked             used to mark an object
//    [0 ............ 0| 00]  inflating          inflation in progress (stack-locking in use)
//
//    We assume that stack/thread pointers have the lowest two bits cleared.
//
//  - INFLATING() is a distinguished markword value of all zeros that is
//    used when inflating an existing stack-lock into an ObjectMonitor.
//    See below for is_being_inflated() and INFLATING().

class BasicLock;
class ObjectMonitor;
class JavaThread;
class outputStream;

class markWord {
 private:
  uintptr_t _value;

 public:
  explicit markWord(uintptr_t value) : _value(value) {}

  markWord() = default;         // Doesn't initialize _value.

  // It is critical for performance that this class be trivially
  // destructable, copyable, and assignable.
  ~markWord() = default;
  markWord(const markWord&) = default;
  markWord& operator=(const markWord&) = default;

  static markWord from_pointer(void* ptr) {
    return markWord((uintptr_t)ptr);
  }
  void* to_pointer() const {
    return (void*)_value;
  }

  bool operator==(const markWord& other) const {
    return _value == other._value;
  }
  bool operator!=(const markWord& other) const {
    return !operator==(other);
  }

  // Conversion
  uintptr_t value() const { return _value; }

  // Constants
  static const int age_bits                       = 4;
  static const int lock_bits                      = 2;
  static const int self_forwarded_bits            = 1;
  static const int max_hash_bits                  = BitsPerWord - age_bits - lock_bits - self_forwarded_bits;
  static const int hash_bits                      = max_hash_bits > 31 ? 31 : max_hash_bits;
  static const int hash_bits_compact              = max_hash_bits > 25 ? 25 : max_hash_bits;
  // Used only without compact headers.
  static const int unused_gap_bits                = LP64_ONLY(1) NOT_LP64(0);
#ifdef _LP64
  // Used only with compact headers.
  static const int klass_bits                     = 32;
#endif

  static const int lock_shift                     = 0;
  static const int self_forwarded_shift           = lock_shift + lock_bits;
  static const int age_shift                      = self_forwarded_shift + self_forwarded_bits;
  static const int hash_shift                     = age_shift + age_bits + unused_gap_bits;
  static const int hash_shift_compact             = age_shift + age_bits;
#ifdef _LP64
  // Used only with compact headers.
  static const int klass_shift                    = hash_shift_compact + hash_bits_compact;
#endif

  static const uintptr_t lock_mask                = right_n_bits(lock_bits);
  static const uintptr_t lock_mask_in_place       = lock_mask << lock_shift;
  static const uintptr_t self_forwarded_mask      = right_n_bits(self_forwarded_bits);
  static const uintptr_t self_forwarded_mask_in_place = self_forwarded_mask << self_forwarded_shift;
  static const uintptr_t age_mask                 = right_n_bits(age_bits);
  static const uintptr_t age_mask_in_place        = age_mask << age_shift;
  static const uintptr_t hash_mask                = right_n_bits(hash_bits);
  static const uintptr_t hash_mask_in_place       = hash_mask << hash_shift;
  static const uintptr_t hash_mask_compact        = right_n_bits(hash_bits_compact);
  static const uintptr_t hash_mask_compact_in_place = hash_mask_compact << hash_shift_compact;
#ifdef _LP64
  static const uintptr_t klass_mask               = right_n_bits(klass_bits);
  static const uintptr_t klass_mask_in_place      = klass_mask << klass_shift;
#endif

  static const uintptr_t locked_value             = 0;
  static const uintptr_t unlocked_value           = 1;
  static const uintptr_t monitor_value            = 2;
  static const uintptr_t marked_value             = 3;

  static const uintptr_t no_hash                  = 0 ;  // no hash value assigned
  static const uintptr_t no_hash_in_place         = (uintptr_t)no_hash << hash_shift;
  static const uintptr_t no_lock_in_place         = unlocked_value;

  static const uint max_age                       = age_mask;

  // Creates a markWord with all bits set to zero.
  static markWord zero() { return markWord(uintptr_t(0)); }

  // lock accessors (note that these assume lock_shift == 0)
  bool is_locked()   const {
    return (mask_bits(value(), lock_mask_in_place) != unlocked_value);
  }
  bool is_unlocked() const {
    return (mask_bits(value(), lock_mask_in_place) == unlocked_value);
  }
  bool is_marked()   const {
    return (mask_bits(value(), lock_mask_in_place) == marked_value);
  }
  bool is_neutral()  const {
    return (mask_bits(value(), lock_mask_in_place) == unlocked_value);
  }

  // Special temporary state of the markWord while being inflated.
  // Code that looks at mark outside a lock need to take this into account.
  bool is_being_inflated() const { return (value() == 0); }

  // Distinguished markword value - used when inflating over
  // an existing stack-lock.  0 indicates the markword is "BUSY".
  // Lockword mutators that use a LD...CAS idiom should always
  // check for and avoid overwriting a 0 value installed by some
  // other thread.  (They should spin or block instead.  The 0 value
  // is transient and *should* be short-lived).
  // Fast-locking does not use INFLATING.
  static markWord INFLATING() { return zero(); }    // inflate-in-progress

  // Should this header be preserved during GC?
  bool must_be_preserved(const oopDesc* obj) const {
    return (!is_unlocked() || !has_no_hash());
  }

  // WARNING: The following routines are used EXCLUSIVELY by
  // synchronization functions. They are not really gc safe.
  // They must get updated if markWord layout get changed.
  markWord set_unlocked() const {
    return markWord(value() | unlocked_value);
  }
  bool has_locker() const {
    assert(LockingMode == LM_LEGACY, "should only be called with legacy stack locking");
    return (value() & lock_mask_in_place) == locked_value;
  }
  BasicLock* locker() const {
    assert(has_locker(), "check");
    return (BasicLock*) value();
  }

  bool is_fast_locked() const {
    assert(LockingMode == LM_LIGHTWEIGHT || LockingMode == LM_PLACEHOLDER, "should only be called with new lightweight locking");
    return (value() & lock_mask_in_place) == locked_value;
  }
  markWord set_fast_locked() const {
    // Clear the lock_mask_in_place bits to set locked_value:
    return markWord(value() & ~lock_mask_in_place);
  }

  bool has_monitor() const {
    return ((value() & lock_mask_in_place) == monitor_value);
  }
  ObjectMonitor* monitor() const {
    assert(has_monitor(), "check");
    assert(LockingMode != LM_PLACEHOLDER, "Placeholder locking does not use markWord for monitors");
    // Use xor instead of &~ to provide one extra tag-bit check.
    return (ObjectMonitor*) (value() ^ monitor_value);
  }
  bool has_displaced_mark_helper() const {
    intptr_t lockbits = value() & lock_mask_in_place;
    return LockingMode == LM_PLACEHOLDER ? false :                           // no displaced mark
           LockingMode == LM_LIGHTWEIGHT ? lockbits == monitor_value         // monitor?
                                         : (lockbits & unlocked_value) == 0; // monitor | stack-locked?
  }
  markWord displaced_mark_helper() const;
  void set_displaced_mark_helper(markWord m) const;
  markWord copy_set_hash(intptr_t hash) const {
    if (UseCompactObjectHeaders) {
      uintptr_t tmp = value() & (~hash_mask_compact_in_place);
      tmp |= ((hash & hash_mask_compact) << hash_shift_compact);
      return markWord(tmp);
    } else {
      uintptr_t tmp = value() & (~hash_mask_in_place);
      tmp |= ((hash & hash_mask) << hash_shift);
      return markWord(tmp);
    }
  }
  // it is only used to be stored into BasicLock as the
  // indicator that the lock is using heavyweight monitor
  static markWord unused_mark() {
    return markWord(marked_value);
  }
  // the following two functions create the markWord to be
  // stored into object header, it encodes monitor info
  static markWord encode(BasicLock* lock) {
    return from_pointer(lock);
  }
  static markWord encode(ObjectMonitor* monitor) {
    assert(LockingMode != LM_PLACEHOLDER, "Placeholder locking does not use markWord for monitors");
    uintptr_t tmp = (uintptr_t) monitor;
    return markWord(tmp | monitor_value);
  }

  markWord set_has_monitor() const {
    return markWord((value() & ~lock_mask_in_place) | monitor_value);
  }

  // used to encode pointers during GC
  markWord clear_lock_bits() const { return markWord(value() & ~lock_mask_in_place); }

  // age operations
  markWord set_marked()   { return markWord((value() & ~lock_mask_in_place) | marked_value); }
  markWord set_unmarked() { return markWord((value() & ~lock_mask_in_place) | unlocked_value); }

  uint     age()           const { return (uint) mask_bits(value() >> age_shift, age_mask); }
  markWord set_age(uint v) const {
    assert((v & ~age_mask) == 0, "shouldn't overflow age field");
    return markWord((value() & ~age_mask_in_place) | ((v & age_mask) << age_shift));
  }
  markWord incr_age()      const { return age() == max_age ? markWord(_value) : set_age(age() + 1); }

  // hash operations
  intptr_t hash() const {
    if (UseCompactObjectHeaders) {
      return mask_bits(value() >> hash_shift_compact, hash_mask_compact);
    } else {
      return mask_bits(value() >> hash_shift, hash_mask);
    }
  }

  bool has_no_hash() const {
    return hash() == no_hash;
  }

#ifdef _LP64
  inline markWord actual_mark() const;
  inline Klass* klass() const;
  inline Klass* klass_or_null() const;
  inline narrowKlass narrow_klass() const;
  inline markWord set_narrow_klass(narrowKlass nklass) const;
  inline markWord set_klass(Klass* klass) const;
#endif

  // Prototype mark for initialization
  static markWord prototype() {
    return markWord( no_hash_in_place | no_lock_in_place );
  }

  // Debugging
  void print_on(outputStream* st, bool print_monitor_info = true) const;

  // Prepare address of oop for placement into mark
  inline static markWord encode_pointer_as_mark(void* p) { return from_pointer(p).set_marked(); }

  // Recover address of oop from encoded form used in mark
  inline void* decode_pointer() { return (void*)clear_lock_bits().value(); }

#ifdef _LP64
  inline bool self_forwarded() const {
    bool self_fwd = mask_bits(value(), self_forwarded_mask_in_place) != 0;
    assert(!self_fwd || UseAltGCForwarding, "Only set self-fwd bit when using alt GC forwarding");
    return self_fwd;
  }

  inline markWord set_self_forwarded() const {
    assert(UseAltGCForwarding, "Only call this with alt GC forwarding");
    return markWord(value() | self_forwarded_mask_in_place | marked_value);
  }
#endif
};

// Support atomic operations.
template<>
struct PrimitiveConversions::Translate<markWord> : public std::true_type {
  typedef markWord Value;
  typedef uintptr_t Decayed;

  static Decayed decay(const Value& x) { return x.value(); }
  static Value recover(Decayed x) { return Value(x); }
};

#endif // SHARE_OOPS_MARKWORD_HPP
