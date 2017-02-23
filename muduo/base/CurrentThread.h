// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_CURRENTTHREAD_H
#define MUDUO_BASE_CURRENTTHREAD_H

#include <stdint.h>

namespace muduo
{

//CurrentThread用于保存线程私有属性
//这些都是静态函数，可以直接调用例如 muduo::CurrentThread::tid()
namespace CurrentThread
{
  //extern 关键字标识出这些变量需要在其他文件中定义 
  //这个命名空间本来是在Thread.cc中实现的，但由于Thrad.cc庞杂，就提取出来了，但是需要加上extern

  // internal
  extern __thread int t_cachedTid;
  extern __thread char t_tidString[32];
  extern __thread int t_tidStringLength;
  extern __thread const char* t_threadName;
  void cacheTid();

  inline int tid()
  {
	  //__builtin_expect是gcc为优化cpu执行指令，
	  //这句代码相当于判断t_cachedTid是否等于0
    if (__builtin_expect(t_cachedTid == 0, 0))
    {
      cacheTid();
    }
    return t_cachedTid;
  }

  inline const char* tidString() // for logging
  {
    return t_tidString;
  }

  inline int tidStringLength() // for logging
  {
    return t_tidStringLength;
  }

  inline const char* name()
  {
    return t_threadName;
  }

  bool isMainThread();

  void sleepUsec(int64_t usec);
}
}

#endif
