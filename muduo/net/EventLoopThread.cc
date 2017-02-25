// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/EventLoopThread.h>

#include <muduo/net/EventLoop.h>

#include <boost/bind.hpp>

using namespace muduo;
using namespace muduo::net;


EventLoopThread::EventLoopThread(const ThreadInitCallback& cb,
                                 const string& name)
  : loop_(NULL),
    exiting_(false),
    thread_(boost::bind(&EventLoopThread::threadFunc, this), name),
    mutex_(),
    cond_(mutex_),
    callback_(cb)
{
}

EventLoopThread::~EventLoopThread()
{
  exiting_ = true;
  
  if (loop_ != NULL) // not 100% race-free, eg. threadFunc could be running callback_.
  {
    // still a tiny chance to call destructed object, if threadFunc exits just now.
    // but when EventLoopThread destructs, usually programming is exiting anyway.
    loop_->quit();  // 退出IO线程，让IO线程的loop循环退出，从而退出了IO线程
    thread_.join();
  }
}

EventLoop* EventLoopThread::startLoop()
{
  assert(!thread_.started());  //断言线程还没有启动
  thread_.start();		//启动线程，调用回调函数threadFunc
  //经过上面调用后就又启动一个新的线程，旧线程继续在这执行，新线程执行threadFunc

  {
    MutexLockGuard lock(mutex_);
    while (loop_ == NULL)   //通过这种方式判断线程中EvenLoop是否创建完毕
    {
      cond_.wait();
    }
  }

  return loop_;
}

void EventLoopThread::threadFunc()
{
  EventLoop loop;  //创建一个对象

  if (callback_)  //调用ThreadInitCallback
  {
    callback_(&loop);
  }

  {
    MutexLockGuard lock(mutex_);	
    // loop_指针指向了一个栈上的对象，threadFunc函数退出之后，这个指针就失效了
    // threadFunc函数退出，就意味着线程退出了，EventLoopThread对象也就没有存在的价值了。
    // 因而不会有什么大的问题

    //loop_指针执行栈上的对象，threadFunc函数退出之后，这个指针就失效了
    //因为EventLoopThread在程序开始时候创建，在程序退出时才析构，EventLoopThread一直伴随这程序的生命周期
    //所以loop_一直在此期间有效，当程序退出时，loop_也就不会再用了
	loop_ = &loop;
    cond_.notify();
  }

  loop.loop();   //新线程中执行loop循环
  //assert(exiting_);
  loop_ = NULL;
}

