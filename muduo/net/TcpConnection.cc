// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/TcpConnection.h>

#include <muduo/base/Logging.h>
#include <muduo/base/WeakCallback.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Socket.h>
#include <muduo/net/SocketsOps.h>

#include <boost/bind.hpp>

#include <errno.h>

using namespace muduo;
using namespace muduo::net;


//默认的连接到来时的回调函数
void muduo::net::defaultConnectionCallback(const TcpConnectionPtr& conn)
{
  LOG_TRACE << conn->localAddress().toIpPort() << " -> "
            << conn->peerAddress().toIpPort() << " is "
            << (conn->connected() ? "UP" : "DOWN");
  // do not call conn->forceClose(), because some users want to register message callback only.
}

//默认的消息时的回调函数
void muduo::net::defaultMessageCallback(const TcpConnectionPtr&,
                                        Buffer* buf,
                                        Timestamp)
{
  buf->retrieveAll();   //直接取走消息
}

TcpConnection::TcpConnection(EventLoop* loop,
                             const string& nameArg,
                             int sockfd,
                             const InetAddress& localAddr,
                             const InetAddress& peerAddr)
  : loop_(CHECK_NOTNULL(loop)),  //检查loop为非空
    name_(nameArg),
    state_(kConnecting),
    reading_(true),
    socket_(new Socket(sockfd)),
    channel_(new Channel(loop, sockfd)),
    localAddr_(localAddr),
    peerAddr_(peerAddr),
    highWaterMark_(64*1024*1024)
{
  // 通道可读事件到来的时候，回调TcpConnection::handleRead，_1是事件发生时间
  channel_->setReadCallback(
      boost::bind(&TcpConnection::handleRead, this, _1));
  channel_->setWriteCallback(   //设置可写事件到来
      boost::bind(&TcpConnection::handleWrite, this));
  channel_->setCloseCallback(
      boost::bind(&TcpConnection::handleClose, this));
  channel_->setErrorCallback(
      boost::bind(&TcpConnection::handleError, this));
  LOG_DEBUG << "TcpConnection::ctor[" <<  name_ << "] at " << this
            << " fd=" << sockfd;
  socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection()
{
  LOG_DEBUG << "TcpConnection::dtor[" <<  name_ << "] at " << this
            << " fd=" << channel_->fd()
            << " state=" << stateToString();
  assert(state_ == kDisconnected);
}

bool TcpConnection::getTcpInfo(struct tcp_info* tcpi) const
{
  return socket_->getTcpInfo(tcpi);
}

string TcpConnection::getTcpInfoString() const
{
  char buf[1024];
  buf[0] = '\0';
  socket_->getTcpInfoString(buf, sizeof buf);
  return buf;
}

//该函数是线程安全的，可以跨线程调用
void TcpConnection::send(const void* data, int len)
{
  send(StringPiece(static_cast<const char*>(data), len));
}

void TcpConnection::send(const StringPiece& message)
{
  if (state_ == kConnected)
  {
    if (loop_->isInLoopThread())   //当前线程就是IO线程，直接调用
    {
      sendInLoop(message);
    }
    else
    {
      loop_->runInLoop(
          boost::bind(&TcpConnection::sendInLoop,
                      this,     // FIXME
                      message.as_string()));
                    //std::forward<string>(message)));
    }
  }
}

// FIXME efficiency!!!
void TcpConnection::send(Buffer* buf)
{
  if (state_ == kConnected)
  {
    if (loop_->isInLoopThread())
    {
      sendInLoop(buf->peek(), buf->readableBytes());
      buf->retrieveAll();
    }
    else
    {
      loop_->runInLoop(
          boost::bind(&TcpConnection::sendInLoop,
                      this,     // FIXME
                      buf->retrieveAllAsString()));
                    //std::forward<string>(message)));
    }
  }
}

void TcpConnection::sendInLoop(const StringPiece& message)
{
  sendInLoop(message.data(), message.size());
}

void TcpConnection::sendInLoop(const void* data, size_t len)
{
  loop_->assertInLoopThread();
  ssize_t nwrote = 0;
  size_t remaining = len;    //剩余的待发送字节数
  bool faultError = false;   //是否错误
  if (state_ == kDisconnected)
  {
    LOG_WARN << "disconnected, give up writing";
    return;
  }
  // if no thing in output queue, try writing directly
  // 通道没有关注可写事件并且发送缓冲区没有数据，直接write
  if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
  {
    nwrote = sockets::write(channel_->fd(), data, len);
    if (nwrote >= 0)
    {
      remaining = len - nwrote;
	  // 写完了，回调writeCompleteCallback_
      if (remaining == 0 && writeCompleteCallback_)
      {
        loop_->queueInLoop(boost::bind(writeCompleteCallback_, shared_from_this()));
      }
    }
    else // nwrote < 0
    {
      nwrote = 0;
      if (errno != EWOULDBLOCK)
      {
        LOG_SYSERR << "TcpConnection::sendInLoop";
        if (errno == EPIPE || errno == ECONNRESET) // FIXME: any others?
        {
          faultError = true;
        }
      }
    }
  }

  assert(remaining <= len);
  // 没有错误，并且还有未写完的数据（说明内核发送缓冲区满，要将未写完的数据添加到output buffer中）
  if (!faultError && remaining > 0)
  {
    size_t oldLen = outputBuffer_.readableBytes();  //outbuf中本身有的数据量
	// 如果超过highWaterMark_（高水位标），回调highWaterMarkCallback_
    if (oldLen + remaining >= highWaterMark_
        && oldLen < highWaterMark_
        && highWaterMarkCallback_)
    {
      //sendInLoop也是在io线程中执行，不直接调用回调函数，而是调用queueInLoop
      //todo : WHY??
      loop_->queueInLoop(boost::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
    }
    outputBuffer_.append(static_cast<const char*>(data)+nwrote, remaining);
    if (!channel_->isWriting())    //如果没有关注POLLOUT事件，则进行关注pollout事件
    {
      channel_->enableWriting();  
    }
  }
}

void TcpConnection::shutdown()
{
  // FIXME: use compare and swap
  if (state_ == kConnected)
  {
    setState(kDisconnecting);  //设置状态
    // FIXME: shared_from_this()?
    loop_->runInLoop(boost::bind(&TcpConnection::shutdownInLoop, this));
  }
}

void TcpConnection::shutdownInLoop()
{
  loop_->assertInLoopThread();
  if (!channel_->isWriting()//如未关注POLLOUT事件则进行关闭
  {
    // we are not writing
    socket_->shutdownWrite();  //关闭写的这一半
  }

  //如果还在关注POLLOUT事件，则只是修改了状态为kDisconnecting，并未真正关闭连接
  //这就需要outputBuffer中的内容发送完成之后还需要再次调用shutdownInLoop
  //连接shutdown后，client端会read返回0，进而close掉这个连接
  //此时poll事件循环会返回 POLLHUP | POLLIN 这两者
  //如果时正常的客户端关闭连接，则服务器事件循环会收到POLLIN，然后read返回0，
  //但如果服务器主动调用shutdown关闭连接，则客户端read后会返回0, 然后调用close关闭连接，此时服务器事件循环循环会收到POLLIN和POLLHUP
}

// void TcpConnection::shutdownAndForceCloseAfter(double seconds)
// {
//   // FIXME: use compare and swap
//   if (state_ == kConnected)
//   {
//     setState(kDisconnecting);
//     loop_->runInLoop(boost::bind(&TcpConnection::shutdownAndForceCloseInLoop, this, seconds));
//   }
// }

// void TcpConnection::shutdownAndForceCloseInLoop(double seconds)
// {
//   loop_->assertInLoopThread();
//   if (!channel_->isWriting())
//   {
//     // we are not writing
//     socket_->shutdownWrite();
//   }
//   loop_->runAfter(
//       seconds,
//       makeWeakCallback(shared_from_this(),
//                        &TcpConnection::forceCloseInLoop));
// }

void TcpConnection::forceClose()
{
  // FIXME: use compare and swap
  if (state_ == kConnected || state_ == kDisconnecting)
  {
    setState(kDisconnecting);
    loop_->queueInLoop(boost::bind(&TcpConnection::forceCloseInLoop, shared_from_this()));
  }
}

void TcpConnection::forceCloseWithDelay(double seconds)
{
  if (state_ == kConnected || state_ == kDisconnecting)
  {
    setState(kDisconnecting);
    loop_->runAfter(
        seconds,
        makeWeakCallback(shared_from_this(),
                         &TcpConnection::forceClose));  // not forceCloseInLoop to avoid race condition
  }
}

void TcpConnection::forceCloseInLoop()
{
  loop_->assertInLoopThread();
  if (state_ == kConnected || state_ == kDisconnecting)
  {
    // as if we received 0 byte in handleRead();
    handleClose();
  }
}

const char* TcpConnection::stateToString() const
{
  switch (state_)
  {
    case kDisconnected:
      return "kDisconnected";
    case kConnecting:
      return "kConnecting";
    case kConnected:
      return "kConnected";
    case kDisconnecting:
      return "kDisconnecting";
    default:
      return "unknown state";
  }
}

void TcpConnection::setTcpNoDelay(bool on)
{
  socket_->setTcpNoDelay(on);
}

void TcpConnection::startRead()
{
  loop_->runInLoop(boost::bind(&TcpConnection::startReadInLoop, this));
}

void TcpConnection::startReadInLoop()
{
  loop_->assertInLoopThread();
  if (!reading_ || !channel_->isReading())
  {
    channel_->enableReading();
    reading_ = true;
  }
}

void TcpConnection::stopRead()
{
  loop_->runInLoop(boost::bind(&TcpConnection::stopReadInLoop, this));
}

void TcpConnection::stopReadInLoop()
{
  loop_->assertInLoopThread();
  if (reading_ || channel_->isReading())
  {
    channel_->disableReading();
    reading_ = false;
  }
}

void TcpConnection::connectEstablished()
{
  loop_->assertInLoopThread();
  assert(state_ == kConnecting);  //断言处理建立状态
  setState(kConnected);

  //shared_from_this将this指针转换为一个shared_ptr对象，因为tie需要传递一个shared_ptr
  channel_->tie(shared_from_this());
  channel_->enableReading();   // TcpConnection所对应的通道加入到Poller关注

  //用户的回调函数
  connectionCallback_(shared_from_this());
}

void TcpConnection::connectDestroyed()
{
  loop_->assertInLoopThread();
  if (state_ == kConnected)  
  {
    setState(kDisconnected);
    channel_->disableAll();
    //如果没有回掉过用户的回调函数就回掉，否则就不在回掉了
    connectionCallback_(shared_from_this());
  }
  channel_->remove();    //从Looper中删除
}

void TcpConnection::handleRead(Timestamp receiveTime)
{
  loop_->assertInLoopThread();
  int savedErrno = 0;

  //读取到inputBuffer中
  ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
  if (n > 0)
  {
   // 调用注册的回调函数
    messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
  }
  else if (n == 0)  //处理断开连接
  {
    handleClose();
  } 
  else   //否则处理错误
  {
    errno = savedErrno;
    LOG_SYSERR << "TcpConnection::handleRead";
    handleError();
  }
}

void TcpConnection::handleWrite()
{
  loop_->assertInLoopThread();
  
  //如果正处于关注POLLOUT事件,说明之前的数据没有发送完成，则将缓冲区的数据发送
  if (channel_->isWriting())   
  {
    ssize_t n = sockets::write(channel_->fd(),
                               outputBuffer_.peek(),
                               outputBuffer_.readableBytes());
    if (n > 0)
    {
      outputBuffer_.retrieve(n);  //将readindex_指针回收
      if (outputBuffer_.readableBytes() == 0)  //说明已经发送完毕了，缓冲区已清空
      {
        //停止关注POLLOUT事件，以免出现busy-loop
        channel_->disableWriting();
        if (writeCompleteCallback_)  //回掉writeCompleteCallback
        {
			// 应用层发送缓冲区被清空，就回调用writeCompleteCallback_
          loop_->queueInLoop(boost::bind(writeCompleteCallback_, shared_from_this()));
        }
        if (state_ == kDisconnecting)
        {
          shutdownInLoop();  // 发送缓冲区已清空并且连接状态是kDisconnecting, 要关闭连接
        }
      }
    }
    else
    {
      LOG_SYSERR << "TcpConnection::handleWrite";
      // if (state_ == kDisconnecting)
      // {
      //   shutdownInLoop();
      // }
    }
  }
  else
  {
    LOG_TRACE << "Connection fd = " << channel_->fd()
              << " is down, no more writing";
  }
}

void TcpConnection::handleClose()
{
  loop_->assertInLoopThread();
  LOG_TRACE << "fd = " << channel_->fd() << " state = " << stateToString();
  assert(state_ == kConnected || state_ == kDisconnecting);
  // we don't close fd, leave it to dtor, so we can find leaks easily.
  setState(kDisconnected);
  channel_->disableAll();

  //返回自身的shared_ptr对象，TcpConnection的引用计数又加1
  //在断开的过程中，到此为止有TcpConnection的引用计数为3
  //第一处在TcpServer的connecton列表里
  //第二处在Channel::handeldEvent中提升了一个
  //第三处是下面的guardThis，
  TcpConnectionPtr guardThis(shared_from_this());
  connectionCallback_(guardThis);  //回调了用户的连接到来的回调函数函数


  // must be the last line
  closeCallback_(guardThis); // 调用TcpServer::removeConnection


  //closeCallback调用完成之后，TcpConnection的引用计数为3
  //与上面的变化是: TcpServer的connection列表中的被删除，
  //但是在用boost::function的时候又增加一个，所以一增一减还是3
  
  //但是出了这个函数之后TcpConnection的引用计数就变为2，guardThis被析构了
}

void TcpConnection::handleError()
{
  int err = sockets::getSocketError(channel_->fd());
  LOG_ERROR << "TcpConnection::handleError [" << name_
            << "] - SO_ERROR = " << err << " " << strerror_tl(err);
}

