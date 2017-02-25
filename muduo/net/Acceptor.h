// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_ACCEPTOR_H
#define MUDUO_NET_ACCEPTOR_H

#include <boost/function.hpp>
#include <boost/noncopyable.hpp>

#include <muduo/net/Channel.h>
#include <muduo/net/Socket.h>

namespace muduo
{
namespace net
{

class EventLoop;
class InetAddress;

///
/// Acceptor of incoming TCP connections.
///

//参见测试程序gaorongTest/Reactor_test07
//Accepter其实就是一个简单的fd和channel，只不过多了一些初始化的工作:bind,listen
//accepter封装了fd和channel，增加了一些功能
class Acceptor : boost::noncopyable
{
 public:
  typedef boost::function<void (int sockfd,
                                const InetAddress&)> NewConnectionCallback;

  Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport);
  ~Acceptor();

  void setNewConnectionCallback(const NewConnectionCallback& cb)
  { newConnectionCallback_ = cb; }

  bool listenning() const { return listenning_; }
  void listen();

 private:
  void handleRead();

  EventLoop* loop_;			//channle所属的事件循环
  Socket acceptSocket_;		//对应的socket
  Channel acceptChannel_;	//socket的channel
  NewConnectionCallback newConnectionCallback_;  //accrpt后调用的用户回调函数
  bool listenning_;			//是否处于监听状态
  int idleFd_;				//空闲文件描述符，为了防止文件句柄到达上限
};

}
}

#endif  // MUDUO_NET_ACCEPTOR_H
