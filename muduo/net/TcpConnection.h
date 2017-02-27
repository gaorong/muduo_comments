// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_TCPCONNECTION_H
#define MUDUO_NET_TCPCONNECTION_H

#include <muduo/base/StringPiece.h>
#include <muduo/base/Types.h>
#include <muduo/net/Callbacks.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/InetAddress.h>

#include <boost/any.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

// struct tcp_info is in <netinet/tcp.h>
struct tcp_info;

namespace muduo
{
namespace net
{

class Channel;
class EventLoop;
class Socket;

///
/// TCP connection, for both client and server usage.
///
/// This is an interface class, so don't expose too much details.

//TcpConnection管理一个连接
//管理这个连接的输入输出缓冲区，
class TcpConnection : boost::noncopyable,
                      public boost::enable_shared_from_this<TcpConnection>
{

//该类继承了enable_shared_from_this是为了使用shared_from_this函数
//shared_from_this函数可以保证调用的过程中引用计数加一，而不是新创建一个对象
//参见案例gaorongTest/Esft.cpp


//TcpConnection的生命周期释放过程:
//当一个tcp连接关闭的时候，首先触发Poller的可读事件，调用Channel::handledEvent来处理
//然后调用TcpConnection::handledread处理，handledRead经过read后发现返回字节为0
//继续调用TcpCOnnecton::handledClose，在这里面会调用tcpServer注册的回调函数removeConnection
//TcpServer::removeConnection会将这个TcpConnection从它的连接列表中删除
//但是此时不能直接delete TcpConnection这个对象，因为TcpConnnection对象中
//的channel还在函数调用栈中调用handledEvent，此时如果直接delete TcpConnection
//会将channel也delete掉，程序会出现core dump错误，所以需要等channel::handeledEvent
//执行完毕之后再delete TcpConnection对象，这就用到shared_ptr。



 public:
  /// Constructs a TcpConnection with a connected sockfd
  ///
  /// User should not create this object.
  TcpConnection(EventLoop* loop,
                const string& name,
                int sockfd,
                const InetAddress& localAddr,
                const InetAddress& peerAddr);
  ~TcpConnection();

  EventLoop* getLoop() const { return loop_; }
  const string& name() const { return name_; }
  const InetAddress& localAddress() const { return localAddr_; }
  const InetAddress& peerAddress() const { return peerAddr_; }
  bool connected() const { return state_ == kConnected; }
  bool disconnected() const { return state_ == kDisconnected; }
  // return true if success.
  bool getTcpInfo(struct tcp_info*) const;
  string getTcpInfoString() const;

  // void send(string&& message); // C++11
  void send(const void* message, int len);
  void send(const StringPiece& message);
  // void send(Buffer&& message); // C++11
  void send(Buffer* message);  // this one will swap data
  void shutdown(); // NOT thread safe, no simultaneous calling
  // void shutdownAndForceCloseAfter(double seconds); // NOT thread safe, no simultaneous calling

  void forceClose();
  void forceCloseWithDelay(double seconds);
  void setTcpNoDelay(bool on);
  // reading or not
  void startRead();
  void stopRead();
  bool isReading() const { return reading_; }; // NOT thread safe, may race with start/stopReadInLoop

  void setContext(const boost::any& context)
  { context_ = context; }

  const boost::any& getContext() const
  { return context_; }

  boost::any* getMutableContext()
  { return &context_; }

  void setConnectionCallback(const ConnectionCallback& cb)
  { connectionCallback_ = cb; }

  void setMessageCallback(const MessageCallback& cb)
  { messageCallback_ = cb; }

  void setWriteCompleteCallback(const WriteCompleteCallback& cb)
  { writeCompleteCallback_ = cb; }

  void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark)
  { highWaterMarkCallback_ = cb; highWaterMark_ = highWaterMark; }

  /// Advanced interface
  Buffer* inputBuffer()
  { return &inputBuffer_; }

  Buffer* outputBuffer()
  { return &outputBuffer_; }

  
  /// Internal use only.  只在内部使用，由TcpServer注册，而非用户
  void setCloseCallback(const CloseCallback& cb)
  { closeCallback_ = cb; }

  // called when TcpServer accepts a new connection
  void connectEstablished();   // should be called only once
  // called when TcpServer has removed me from its map
  void connectDestroyed();  // should be called only once

 private:
  enum StateE { kDisconnected, kConnecting, kConnected, kDisconnecting };
  void handleRead(Timestamp receiveTime);
  void handleWrite();
  void handleClose();
  void handleError();
  // void sendInLoop(string&& message);
  void sendInLoop(const StringPiece& message);
  void sendInLoop(const void* message, size_t len);
  void shutdownInLoop();
  // void shutdownAndForceCloseInLoop(double seconds);
  void forceCloseInLoop();
  void setState(StateE s) { state_ = s; }
  const char* stateToString() const;
  void startReadInLoop();
  void stopReadInLoop();

  EventLoop* loop_;
  const string name_;  //连接名称
  //连接状态，枚举类型
  StateE state_;  // FIXME: use atomic variable  
  bool reading_;
  // we don't expose those classes to client.
  boost::scoped_ptr<Socket> socket_;
  boost::scoped_ptr<Channel> channel_;
  const InetAddress localAddr_;   //本地地址
  const InetAddress peerAddr_;	  //对等地址
  
  ConnectionCallback connectionCallback_;
  MessageCallback messageCallback_;
  WriteCompleteCallback writeCompleteCallback_;     // 数据发送完毕回调函数，即所有的用户数据都已拷贝到内核缓冲区时回调该函数
													// outputBuffer_被清空也会回调该函数，可以理解为低水位标回调函数
  HighWaterMarkCallback highWaterMarkCallback_;    // 高水位标回调函数,也就是outputBuffer撑到一定程度了
  CloseCallback closeCallback_;

  size_t highWaterMark_;	//高水位标
  Buffer inputBuffer_;		//接收缓冲区
  Buffer outputBuffer_; // FIXME: use list<Buffer> as output buffer.

  //boost::any是一种可变类型的指针，比void*类型安全，它支持任意类型的类型安全存储以及安全返回
  //可以在标准库容器中存放不同类型的方法，例如vector<boost::any>
  boost::any context_;	//绑定一个未知类型的上下文对象
  // FIXME: creationTime_, lastReceiveTime_
  //        bytesReceived_, bytesSent_
};

typedef boost::shared_ptr<TcpConnection> TcpConnectionPtr;

}
}

#endif  // MUDUO_NET_TCPCONNECTION_H
