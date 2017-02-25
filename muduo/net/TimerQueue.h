// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_TIMERQUEUE_H
#define MUDUO_NET_TIMERQUEUE_H

#include <set>
#include <vector>

#include <boost/noncopyable.hpp>

#include <muduo/base/Mutex.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Callbacks.h>
#include <muduo/net/Channel.h>

namespace muduo
{
namespace net
{

class EventLoop;
class Timer;
class TimerId;

///
/// A best efforts timer queue.
/// No guarantee that the callback will be on time.
///
class TimerQueue : boost::noncopyable
{
 public:
  TimerQueue(EventLoop* loop);
  ~TimerQueue();

  ///
  /// Schedules the callback to be run at given time,
  /// repeats if @c interval > 0.0.
  ///
  /// Must be thread safe. Usually be called from other threads.
  //一定是线程安全的，可以跨线程调用，通常情况下被其他程序调用 
  TimerId addTimer(const TimerCallback& cb,
                   Timestamp when,
                   double interval);
#ifdef __GXX_EXPERIMENTAL_CXX0X__
  TimerId addTimer(TimerCallback&& cb,
                   Timestamp when,
                   double interval);
#endif

  void cancel(TimerId timerId);

 private:

  // FIXME: use unique_ptr<Timer> instead of raw pointers.
  // unique_ptr是C++ 11标准的一个独享所有权的智能指针
  // 无法得到指向同一对象的两个unique_ptr指针
  // 但可以进行移动构造与移动赋值操作，即所有权可以移动到另一个对象（而非拷贝构造）
  //在下一个版本中会实现C++11版本
  typedef std::pair<Timestamp, Timer*> Entry;
  typedef std::set<Entry> TimerList;
  //下面的这个结构其实和上面的保存的是一样的数据，只不过set排序的方式不一样
  typedef std::pair<Timer*, int64_t> ActiveTimer;
  typedef std::set<ActiveTimer> ActiveTimerSet;

  //addTimerInLoop，CancelInLoop由EvenLoop中对应的函数调用
  //以下成员函数只可能在其所属的线程中调用，因而不必加锁
  void addTimerInLoop(Timer* timer);
  void cancelInLoop(TimerId timerId);
  // called when timerfd alarms
  void handleRead();
  // move out all expired timers
  std::vector<Entry> getExpired(Timestamp now);
  //对这些超时的定时器进行重置，因为可能是重复的定时器
  void reset(const std::vector<Entry>& expired, Timestamp now);

  bool insert(Timer* timer);

  EventLoop* loop_;
  const int timerfd_;  
  Channel timerfdChannel_;
  // Timer list sorted by expiration
  TimerList timers_;	//按到期时间排序

  // for cancel()
  ActiveTimerSet activeTimers_;  //按对象地址进行排序 ，与timers_保存的是相同的数据
  bool callingExpiredTimers_; /* atomic */
  ActiveTimerSet cancelingTimers_;  //保存的是被取消的定时器
};
 
}
}
#endif  // MUDUO_NET_TIMERQUEUE_H
