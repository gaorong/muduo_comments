// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_EVENTLOOPTHREAD_H
#define MUDUO_NET_EVENTLOOPTHREAD_H

#include <muduo/base/Condition.h>
#include <muduo/base/Mutex.h>
#include <muduo/base/Thread.h>

#include <boost/noncopyable.hpp>

namespace muduo
{
namespace net
{

class EventLoop;

//查看测试程序gaorongTests/Reactor_text06
class EventLoopThread : boost::noncopyable
{
 public:
  typedef boost::function<void(EventLoop*)> ThreadInitCallback;

  //构造函数中将ThreadInitCallback，指定默认值为空函数
  EventLoopThread(const ThreadInitCallback& cb = ThreadInitCallback(),
                  const string& name = string());
  ~EventLoopThread();
  EventLoop* startLoop();	//启动线程，会调用Thread中的start函数,该线程成为IO线程

 private:	
  void threadFunc();	//线程函数

  EventLoop* loop_;		//loop_指针指向一个EvenLoop对象
  bool exiting_;		//是否退出
  Thread thread_;		//基于对象的封装方法，不是用继承，而是用包含
  MutexLock mutex_;		
  Condition cond_;
  ThreadInitCallback callback_;  // 回调函数在EventLoop::loop事件循环之前被调用，默认为空
};

}
}

#endif  // MUDUO_NET_EVENTLOOPTHREAD_H

