// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/TcpServer.h>

#include <muduo/base/Logging.h>
#include <muduo/net/Acceptor.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/EventLoopThreadPool.h>
#include <muduo/net/SocketsOps.h>

#include <boost/bind.hpp>

#include <stdio.h>  // snprintf

using namespace muduo;
using namespace muduo::net;


TcpServer::TcpServer(EventLoop* loop,
                     const InetAddress& listenAddr,
                     const string& nameArg,
                     Option option)
  : loop_(CHECK_NOTNULL(loop)),
    ipPort_(listenAddr.toIpPort()),
    name_(nameArg),
    acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),
    threadPool_(new EventLoopThreadPool(loop, name_)), //初始化线程池
    connectionCallback_(defaultConnectionCallback),
    messageCallback_(defaultMessageCallback),
    nextConnId_(1)
{
  // Acceptor::handleRead函数中会回调用TcpServer::newConnection
  // _1对应的是socket文件描述符，_2对应的是对等方的地址(InetAddress)
  acceptor_->setNewConnectionCallback(
      boost::bind(&TcpServer::newConnection, this, _1, _2));
}

TcpServer::~TcpServer()
{
  loop_->assertInLoopThread();
  LOG_TRACE << "TcpServer::~TcpServer [" << name_ << "] destructing";

  for (ConnectionMap::iterator it(connections_.begin());
      it != connections_.end(); ++it)
  {
    TcpConnectionPtr conn = it->second;
    it->second.reset();
    conn->getLoop()->runInLoop(
      boost::bind(&TcpConnection::connectDestroyed, conn));
    conn.reset();
  }
}

void TcpServer::setThreadNum(int numThreads)
{
  assert(0 <= numThreads);
  threadPool_->setThreadNum(numThreads);
}

// 该函数多次调用是无害的
// 该函数可以跨线程调用
void TcpServer::start()
{
  if (started_.getAndSet(1) == 0)
  {
    threadPool_->start(threadInitCallback_);

    assert(!acceptor_->listenning());    //断言accepter未处于监听状态
    loop_->runInLoop(    //让主线程开始监听，get_pointer返回原生指针
        boost::bind(&Acceptor::listen, get_pointer(acceptor_)));
  }
}

void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr)
{
  loop_->assertInLoopThread();
  //按照轮叫的方式选择一个EvenLoop
  EventLoop* ioLoop = threadPool_->getNextLoop();
  char buf[64];
  snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
  ++nextConnId_;
  string connName = name_ + buf;   //连接名称 : servername+ipPort#conntId

  LOG_INFO << "TcpServer::newConnection [" << name_
           << "] - new connection [" << connName
           << "] from " << peerAddr.toIpPort();


  InetAddress localAddr(sockets::getLocalAddr(sockfd));
  // FIXME poll with zero timeout to double confirm the new connection
  // FIXME use make_shared if necessary
  //创建一个连接
  TcpConnectionPtr conn(new TcpConnection(ioLoop,
                                          connName,
                                          sockfd,
                                          localAddr,
                                          peerAddr));
  connections_[connName] = conn;

  //下面三行都是注册用户的回调函数
  conn->setConnectionCallback(connectionCallback_);
  conn->setMessageCallback(messageCallback_);
  conn->setWriteCompleteCallback(writeCompleteCallback_);

  //下面是注册TcpServer的回调函数
  conn->setCloseCallback(
      boost::bind(&TcpServer::removeConnection, this, _1)); // FIXME: unsafe

  //让所属的EvenLoop调用connectEstablished
  ioLoop->runInLoop(boost::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn)
{
  // FIXME: unsafe
  loop_->runInLoop(boost::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
  loop_->assertInLoopThread();
  LOG_INFO << "TcpServer::removeConnectionInLoop [" << name_
           << "] - connection " << conn->name();

  //从连接列表中删除，TcpConnection的引用计数又变为2
  size_t n = connections_.erase(conn->name());  
  (void)n;
  assert(n == 1);
  EventLoop* ioLoop = conn->getLoop();

  //调用下面语句使得TcpConnection引用计数变为3
  ioLoop->queueInLoop(
      boost::bind(&TcpConnection::connectDestroyed, conn));
}

