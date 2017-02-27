// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_EVENTLOOPTHREADPOOL_H
#define MUDUO_NET_EVENTLOOPTHREADPOOL_H

#include <muduo/base/Types.h>

#include <vector>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/ptr_container/ptr_vector.hpp>

namespace muduo
{

namespace net
{

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool : boost::noncopyable
{
 public:
  typedef boost::function<void(EventLoop*)> ThreadInitCallback;

  EventLoopThreadPool(EventLoop* baseLoop, const string& nameArg);
  ~EventLoopThreadPool();
  void setThreadNum(int numThreads) { numThreads_ = numThreads; }
  void start(const ThreadInitCallback& cb = ThreadInitCallback());

  // valid after calling start()
  /// round-robin
  EventLoop* getNextLoop();

  /// with the same hash code, it will always return the same EventLoop
  EventLoop* getLoopForHash(size_t hashCode);

  std::vector<EventLoop*> getAllLoops();

  bool started() const
  { return started_; }

  const string& name() const
  { return name_; }

 private:

  EventLoop* baseLoop_;  // 与Acceptor所属EventLoop相同，即mainReactor(用来关注Accepter事件，而其他subReactor用来关注已连接套接字的事件)
  string name_;			 //名字
  bool started_;     //是否启动
  int numThreads_;  //线程数
  int next_;     // 新连接到来，所选择的EventLoop对象下标
  boost::ptr_vector<EventLoopThread> threads_;	//io线程列表，用ptr_vector销毁的时候会自动销毁它所维护的EventLoopThread

  //EvenLoop列表，一个io线程对应一个Evenloop对象，因为都是栈上的对象(参看EvenLoopThread类)，所以只需要vector维护就可以
  std::vector<EventLoop*> loops_;		 
};

}
}

#endif  // MUDUO_NET_EVENTLOOPTHREADPOOL_H
