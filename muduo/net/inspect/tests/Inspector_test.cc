#include <muduo/net/inspect/Inspector.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/EventLoopThread.h>

using namespace muduo;
using namespace muduo::net;

int main()
{
  EventLoop loop;
  EventLoopThread t;  //监控线程
  //用t做为参数，所以Inspector所属的IO线程就是t
  Inspector ins(t.startLoop(), InetAddress(12345), "test");
  loop.loop();
}

