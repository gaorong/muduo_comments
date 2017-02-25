// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/Timer.h>

using namespace muduo;
using namespace muduo::net;

AtomicInt64 Timer::s_numCreated_;  //默认值为0



void Timer::restart(Timestamp now)
{
  if (repeat_)  
  {
    //如果是重复计时器则重新计算下一个超时时刻:现在再加interval_
	//addTime为全局函数,定义在TimeStamp中
	expiration_ = addTime(now, interval_);
  }
  else
  {
    //如果不是重复计时器则下一个超时时刻为一个非法时间
    expiration_ = Timestamp::invalid();
  }
}
