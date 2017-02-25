// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/poller/PollPoller.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Types.h>
#include <muduo/net/Channel.h>

#include <assert.h>
#include <errno.h>
#include <poll.h>

using namespace muduo;
using namespace muduo::net;

PollPoller::PollPoller(EventLoop* loop)
  : Poller(loop)
{
}

PollPoller::~PollPoller()
{
}

Timestamp PollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
  // XXX pollfds_ shouldn't change
  //其中timeoutMs用来设置poll的超时时间，如果超时还未收到消息则答应nothing happend
  int numEvents = ::poll(&*pollfds_.begin(), pollfds_.size(), timeoutMs);
  int savedErrno = errno;
  Timestamp now(Timestamp::now());
  if (numEvents > 0)
  {
    LOG_TRACE << numEvents << " events happended";
    fillActiveChannels(numEvents, activeChannels);
  }
  else if (numEvents == 0)
  {
    LOG_TRACE << " nothing happended";
  }
  else
  {
    if (savedErrno != EINTR)
    {
      errno = savedErrno;
      LOG_SYSERR << "PollPoller::poll()";
    }
  }
  return now;
}

//numEvents表示返回的事件个数
void PollPoller::fillActiveChannels(int numEvents,
                                    ChannelList* activeChannels) const
{
  for (PollFdList::const_iterator pfd = pollfds_.begin();
      pfd != pollfds_.end() && numEvents > 0; ++pfd)
  {
    if (pfd->revents > 0)
    {
      --numEvents;  //每处理一个就--
      //根据fd找到对应的channel并进行一些断言
      ChannelMap::const_iterator ch = channels_.find(pfd->fd);
      assert(ch != channels_.end());
      Channel* channel = ch->second;
      assert(channel->fd() == pfd->fd);

	  //对channle的事件继续设置
      channel->set_revents(pfd->revents);
      // pfd->revents = 0;
      //添加到活动通道集合中
      activeChannels->push_back(channel);
    }
  }
}

//注册和更新关注的事件
void PollPoller::updateChannel(Channel* channel)
{
  Poller::assertInLoopThread();
  LOG_TRACE << "fd = " << channel->fd() << " events = " << channel->events();
  if (channel->index() < 0)
  {
    
    // a new one, add to pollfds_
	//如果index小于零,说明是个新的通道，Channle的ctor会初始化为-1
    //新的channel断言找不到
    assert(channels_.find(channel->fd()) == channels_.end());

 	//设置poll需要的结构体并添加到数组中
    struct pollfd pfd;
    pfd.fd = channel->fd();
    pfd.events = static_cast<short>(channel->events());
    pfd.revents = 0;
    pollfds_.push_back(pfd);
	
    int idx = static_cast<int>(pollfds_.size())-1;
    channel->set_index(idx);     //设置index
    channels_[pfd.fd] = channel;   //添加到map中
  }
  else
  {
    // update existing one  更新一个以存在的channel

	//首先断言
    assert(channels_.find( channel->fd()) != channels_.end() );
    assert(channels_[channel->fd()] == channel);
    int idx = channel->index();
    assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));

    struct pollfd& pfd = pollfds_[idx];
	//后面那个分支为什么是-fd-1? 详情请看下面
    assert(pfd.fd == channel->fd() || pfd.fd == -channel->fd()-1);
    pfd.events = static_cast<short>(channel->events());
    pfd.revents = 0;

	//将一个channel暂时更改为不关心该事件，但不从Poller中移除该channel
    if (channel->isNoneEvent())
    {
      // ignore this pollfd
      //这里可以直接设置为-1，设置成下面这样是为了removeChannel优化
	  //防止fd为0，所以减去1
	  //当fd为负值时，调用poll会返回POLLNVAL，
	  pfd.fd = -channel->fd()-1;
    }
  }
}

void PollPoller::removeChannel(Channel* channel)
{
  Poller::assertInLoopThread();
  LOG_TRACE << "fd = " << channel->fd();
  assert(channels_.find(channel->fd()) != channels_.end());
  assert(channels_[channel->fd()] == channel);

  //在调用removeChannel前，必须不在关注事件，调用updateChannel将事件置为NonEvenr
  //以免还在关注事件的时候把他移除掉
  assert(channel->isNoneEvent());


  int idx = channel->index();
  assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
  const struct pollfd& pfd = pollfds_[idx]; (void)pfd;
  assert(pfd.fd == -channel->fd()-1 && pfd.events == channel->events());
  size_t n = channels_.erase(channel->fd());
  assert(n == 1); (void)n;   //用key移除时返回值为1
  
  if (implicit_cast<size_t>(idx) == pollfds_.size()-1)
  {
    pollfds_.pop_back();   //如果idx在数组中是最后一个，直接pop
  }
  else
  {
    //这里移除的算法复杂度为-1，将待删除的元素与最后一个元素交换再pop_back
    int channelAtEnd = pollfds_.back().fd;
    iter_swap(pollfds_.begin()+idx, pollfds_.end()-1);

	//如果channelAtEnd为负值，下面为了求出真实的fd,例如-(-4)-1=3,真实的fd为3
    if (channelAtEnd < 0)
    {
      channelAtEnd = -channelAtEnd-1;
    }
    channels_[channelAtEnd]->set_index(idx);   //根据fd重置channel的idx
    pollfds_.pop_back();
  }
}

