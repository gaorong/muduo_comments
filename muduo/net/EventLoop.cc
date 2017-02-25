// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/EventLoop.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
#include <muduo/net/Channel.h>
#include <muduo/net/Poller.h>
#include <muduo/net/SocketsOps.h>
#include <muduo/net/TimerQueue.h>

#include <boost/bind.hpp>

#include <signal.h>
#include <sys/eventfd.h>

using namespace muduo;
using namespace muduo::net;

namespace
{
// 当前线程EventLoop对象指针
// 线程局部存储
__thread EventLoop* t_loopInThisThread = 0;  

const int kPollTimeMs = 10000;   //这是默认的超时时间

int createEventfd()
{
  int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evtfd < 0)
  {
    LOG_SYSERR << "Failed in eventfd";
    abort();
  }
  return evtfd;
}

#pragma GCC diagnostic ignored "-Wold-style-cast"
class IgnoreSigPipe
{
 public:
  IgnoreSigPipe()
  {
    ::signal(SIGPIPE, SIG_IGN);
    // LOG_TRACE << "Ignore SIGPIPE";
  }
};
#pragma GCC diagnostic error "-Wold-style-cast"

IgnoreSigPipe initObj;
}

EventLoop* EventLoop::getEventLoopOfCurrentThread()
{
  return t_loopInThisThread;
}

EventLoop::EventLoop()
  : looping_(false),		//构造时候未循环
    quit_(false),
    eventHandling_(false),
    callingPendingFunctors_(false),
    iteration_(0),
    threadId_(CurrentThread::tid()),  //初始化为当前线程
    poller_(Poller::newDefaultPoller(this)),
    timerQueue_(new TimerQueue(this)),
    wakeupFd_(createEventfd()),
    wakeupChannel_(new Channel(this, wakeupFd_)),
    currentActiveChannel_(NULL)
{
  LOG_DEBUG << "EventLoop created " << this << " in thread " << threadId_;

  // 如果当前线程已经创建了EventLoop对象，终止(LOG_FATAL)
  if (t_loopInThisThread)
  {
    LOG_FATAL << "Another EventLoop " << t_loopInThisThread
              << " exists in this thread " << threadId_;
  }
  else
  {
    t_loopInThisThread = this;
  }

  //设置wakeupChannel的回掉函数
  wakeupChannel_->setReadCallback(
      boost::bind(&EventLoop::handleRead, this));
  // we are always reading the wakeupfd
  wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
  LOG_DEBUG << "EventLoop " << this << " of thread " << threadId_
            << " destructs in thread " << CurrentThread::tid();
  wakeupChannel_->disableAll();
  wakeupChannel_->remove();
  ::close(wakeupFd_);
  t_loopInThisThread = NULL;	//置为NULL，表示当前线程再无evenLoop	
}


// 事件循环，该函数不能跨线程调用
// 只能在创建该对象的线程中调用
void EventLoop::loop()
{
  //断言未循环并在当前线程中
  assert(!looping_);	
  assertInLoopThread();
  looping_ = true;
  quit_ = false;  // FIXME: what if someone calls quit() before loop() ?
  LOG_TRACE << "EventLoop " << this << " start looping";

  while (!quit_)
  {
    activeChannels_.clear();   //将活动通道清除
    //通过poll返回活动通道，存放在activeChannel
    pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
    ++iteration_;
	//打印
    if (Logger::logLevel() <= Logger::TRACE)
    {
      printActiveChannels();
    }
    // TODO sort channel by priority
    eventHandling_ = true;
    for (ChannelList::iterator it = activeChannels_.begin();
        it != activeChannels_.end(); ++it)
    {
      currentActiveChannel_ = *it;  //更新当前正在处理通道
      currentActiveChannel_->handleEvent(pollReturnTime_);
    }
    currentActiveChannel_ = NULL;   //将当前正在处理通道置为NULL
    eventHandling_ = false;    //不在处理通道
    //设计灵活，当前线程也可以处理计算任务(当IO任务不是很多时可以用作计算)
    doPendingFunctors();   
  }

  LOG_TRACE << "EventLoop " << this << " stop looping";
  looping_ = false;
}

//该函数可以跨线程调用,也就是可以在其它线程中让EvenLoop退出
void EventLoop::quit()
{
  quit_ = true;   //线程共享的，所以可以直接更改
  // There is a chance that loop() just executes while(!quit_) and exits,
  // then EventLoop destructs, then we are accessing an invalid object.
  // Can be fixed using mutex_ in both places.
  //如果不是当前线程在调用该函数，则当前线程可能正阻塞在poll函数那里
  //所以需要手动调用wakeup产生一个IO事件唤醒线程，然后线程醒来后处理事件后就会在
  //下次循环中判断到quit_为true，从而退出事件循环
  if (!isInLoopThread())  
  {
    wakeup();   //如果不是当前线程则调用wakeup进行唤醒 
  }
}


//这是一种编程思想，当其他线程需要通过这个线程的资源来执行任务的时候，
//并不是直接再其他线程中访问资源调用函数
//这样就会造成资源的竞争，需要加锁保证，而现在我们让当前线程
//为其他线程提供一个接口，其他线程将要执行的任务用这个接口交给当前线程
//这样当前线程统一处理自己的资源，而不用加锁，唯一需要加锁的地方就是
//通过接口添加任务的任务队列这个地方，大大减小了锁粒度

//在IO线程中执行某个回调函数，该函数可以跨线程执行
//可查看测试程序gaorongTests/Reactor_text05
void EventLoop::runInLoop(const Functor& cb)
{
  if (isInLoopThread())  //如果在当前IO线程中调用，则同步调用cb,即直接调用
  {
    cb();
  }
  else
  {
    //如果在其他线程中调用该函数，则异步调用，用queueInLoop添加到任务队列终中
    queueInLoop(cb);
  }
}


//加入队列中，等待被执行，该函数可以跨线程调用，即其他线程可以给当前线程添加任务
void EventLoop::queueInLoop(const Functor& cb)
{
  {	
  MutexLockGuard lock(mutex_);
  pendingFunctors_.push_back(cb);
  }

  //如果不是当前线程(可能阻塞在poll)，需要唤醒 
  //或者是当前线程但是在正在处理队列中的任务(使得处理完当前队列中的元素后立即在进行下一轮处理，因为在这里又添加了任务)需要唤醒
  //只有当前IO线程的事件回调中调用queueInLoop才不需要唤醒(因为执行完handleEvent会自然执行doPendingFunctor)
  if (!isInLoopThread() || callingPendingFunctors_)
  {
    wakeup();
  }
}

size_t EventLoop::queueSize() const
{
  MutexLockGuard lock(mutex_);
  return pendingFunctors_.size();
}

//以下三个函数都是调用addTimer进行添加定时器
//可查看测试程序gaorongTests/Reactor_test04
TimerId EventLoop::runAt(const Timestamp& time, const TimerCallback& cb)
{
  return timerQueue_->addTimer(cb, time, 0.0);  //0.0代表不是一个重复的定时器
}

TimerId EventLoop::runAfter(double delay, const TimerCallback& cb)
{
  Timestamp time(addTime(Timestamp::now(), delay));
  return runAt(time, cb);
}

TimerId EventLoop::runEvery(double interval, const TimerCallback& cb)
{
  Timestamp time(addTime(Timestamp::now(), interval));
  return timerQueue_->addTimer(cb, time, interval);
}

#ifdef __GXX_EXPERIMENTAL_CXX0X__
// FIXME: remove duplication
void EventLoop::runInLoop(Functor&& cb)
{
  if (isInLoopThread())
  {
    cb();
  }
  else
  {
    queueInLoop(std::move(cb));
  }
}

void EventLoop::queueInLoop(Functor&& cb)
{
  {
  MutexLockGuard lock(mutex_);
  pendingFunctors_.push_back(std::move(cb));  // emplace_back
  }

  if (!isInLoopThread() || callingPendingFunctors_)
  {
    wakeup();
  }
}

TimerId EventLoop::runAt(const Timestamp& time, TimerCallback&& cb)
{
  return timerQueue_->addTimer(std::move(cb), time, 0.0);
}

TimerId EventLoop::runAfter(double delay, TimerCallback&& cb)
{
  Timestamp time(addTime(Timestamp::now(), delay));
  return runAt(time, std::move(cb));
}

TimerId EventLoop::runEvery(double interval, TimerCallback&& cb)
{
  Timestamp time(addTime(Timestamp::now(), interval));
  return timerQueue_->addTimer(std::move(cb), time, interval);
}
#endif

void EventLoop::cancel(TimerId timerId)
{
  return timerQueue_->cancel(timerId);  //直接调用timerQueue的
}

void EventLoop::updateChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);  //断言channle所属的EvenLoop是自身
  assertInLoopThread();     //断言EvenLoop是当前线程
  poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  if (eventHandling_)
  {
    assert(currentActiveChannel_ == channel ||
        std::find(activeChannels_.begin(), activeChannels_.end(), channel) == activeChannels_.end());
  }
  poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  return poller_->hasChannel(channel);
}

void EventLoop::abortNotInLoopThread()
{
  LOG_FATAL << "EventLoop::abortNotInLoopThread - EventLoop " << this
            << " was created in threadId_ = " << threadId_
            << ", current thread id = " <<  CurrentThread::tid();
}

void EventLoop::wakeup()
{
  //evenfd的缓冲区只有八个字节，所以只需要写一个uint64_t的数值就可以
  uint64_t one = 1;
  ssize_t n = sockets::write(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
  {
    LOG_ERROR << "EventLoop::wakeup() writes " << n << " bytes instead of 8";
  }
}

//wakeupChannel的回掉函数
void EventLoop::handleRead()
{
  uint64_t one = 1;
  //内部调用的还是::read函数
  ssize_t n = sockets::read(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
  {
    LOG_ERROR << "EventLoop::handleRead() reads " << n << " bytes instead of 8";
  }
}

// 1. 不是简单地在临界区内依次调用Functor，
//而是把回调列表swap到functors中，这样一方面减小了临界区的长度
//（意味着不会阻塞其它线程的queueInLoop()添加任务到pendingFunctors_），另一方面，也避免了死
//锁（因为Functor可能再次调用queueInLoop()添加任务到pendingFunctors_）

//2. 由于doPendingFunctors()调用的Functor可能再次调用queueInLoop(cb)添加任务到pendingFunctors_，这时，
//queueInLoop()就必须wakeup()，否则新增的cb可能就不能及时调用了

//3. muduo没有反复执行doPendingFunctors()直到pendingFunctors为空，
//这是有意的，否则IO线程可能陷入死循环，无法处理IO事件。
void EventLoop::doPendingFunctors()
{
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;  //打开正在处理任务的flag

  {
  MutexLockGuard lock(mutex_);
  functors.swap(pendingFunctors_);  //这样效率高
  }

  for (size_t i = 0; i < functors.size(); ++i)
  {
    functors[i]();
  }
  callingPendingFunctors_ = false;
}

void EventLoop::printActiveChannels() const
{
  for (ChannelList::const_iterator it = activeChannels_.begin();
      it != activeChannels_.end(); ++it)
  {
    const Channel* ch = *it;
    LOG_TRACE << "{" << ch->reventsToString() << "} ";
  }
}

