// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_ATOMIC_H
#define MUDUO_BASE_ATOMIC_H

#include <boost/noncopyable.hpp>
#include <stdint.h>

/*
*
gcc原子性操作:
// 原子自增操作
type __sync_fetch_and_add (type *ptr, type value)

// 原子比较和交换（设置）操作  (如果比较相等就设置为newval)
type __sync_val_compare_and_swap (type *ptr, type oldval type newval)
bool __sync_bool_compare_and_swap (type *ptr, type oldval type newval)

// 原子赋值操作
type __sync_lock_test_and_set (type *ptr, type value)
使用这些原子性操作，编译的时候需要加-march=cpu-type   
可以直接写成-match=native， 让CPU自己检测属于什么类
*/

namespace muduo
{

namespace detail
{

//为不可拷贝的类，boost::noncopyable实现就是将拷贝构造函数和复制运算符做成私有的。
template<typename T>
class AtomicIntegerT : boost::noncopyable
{
 public:
 	//初始化0，可以直接自增
  AtomicIntegerT()
    : value_(0)
  {
  }

  // uncomment if you need copying and assignment
  //
  // AtomicIntegerT(const AtomicIntegerT& that)
  //   : value_(that.get())
  // {}
  //
  // AtomicIntegerT& operator=(const AtomicIntegerT& that)
  // {
  //   getAndSet(that.get());
  //   return *this;
  // }

  T get()
  {
    // in gcc >= 4.7: __atomic_load_n(&value_, __ATOMIC_SEQ_CST)
    return __sync_val_compare_and_swap(&value_, 0, 0);
  }

  T getAndAdd(T x)
  {
    // in gcc >= 4.7: __atomic_fetch_add(&value_, x, __ATOMIC_SEQ_CST)
    return __sync_fetch_and_add(&value_, x);
  }

  T addAndGet(T x)
  {
    return getAndAdd(x) + x;
  }

  T incrementAndGet()
  {
    return addAndGet(1);
  }

  T decrementAndGet()
  {
    return addAndGet(-1);
  }

  void add(T x)
  {
    getAndAdd(x);
  }

  void increment()
  {
    incrementAndGet();
  }

  void decrement()
  {
    decrementAndGet();
  }

  T getAndSet(T newValue)
  {
    // in gcc >= 4.7: __atomic_exchange_n(&value, newValue, __ATOMIC_SEQ_CST)
    return __sync_lock_test_and_set(&value_, newValue);
  }

 private:
 	//用volatile修饰，防止编译器进行优化，保证每次取值都是直接从内存中取值
  volatile T value_;
};
}

typedef detail::AtomicIntegerT<int32_t> AtomicInt32;
typedef detail::AtomicIntegerT<int64_t> AtomicInt64;
}

#endif  // MUDUO_BASE_ATOMIC_H
