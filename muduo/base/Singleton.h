// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_SINGLETON_H
#define MUDUO_BASE_SINGLETON_H

#include <boost/noncopyable.hpp>
#include <assert.h>
#include <stdlib.h> // atexit
#include <pthread.h>

namespace muduo
{

namespace detail
{
// This doesn't detect inherited member functions!
// http://stackoverflow.com/questions/1966362/sfinae-to-check-for-inherited-member-functions
template<typename T>
struct has_no_destroy
{
//todo:what does it mean?
#ifdef __GXX_EXPERIMENTAL_CXX0X__
  template <typename C> static char test(decltype(&C::no_destroy));
#else
  template <typename C> static char test(typeof(&C::no_destroy));
#endif
  template <typename C> static int32_t test(...);
  const static bool value = sizeof(test<T>(0)) == 1;
};
}

template<typename T>
class Singleton : boost::noncopyable  //线程安全的单例类实现
{
 public:
  static T& instance()
  {
    //pthread_once能够保证init函数只被调用了一次，也就是对象只被创建了一次
    //该函数是线程安全的
    pthread_once(&ponce_, &Singleton::init);
    assert(value_ != NULL);
    return *value_;
  }

 private:
  Singleton();
  ~Singleton();

  static void init()
  {
    value_ = new T();
    if (!detail::has_no_destroy<T>::value)
    {
      ::atexit(destroy);  //注册退出时销毁函数
    }
  }

  static void destroy()
  {
    //sizeof会在编译时就计算出T的类型大小，如果是不完全类型，sizeof(T)为0, 
    //T_must_be_complete_type就相当于个数为-1个元素的数组，定义变量的时候会报错
    typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1];
	//如果正常运行，则dummy可正常定义，但是定义后并没使用，为防止警告所以加上(void) dummy;
    T_must_be_complete_type dummy; (void) dummy;

    delete value_;
    value_ = NULL;
  }

 private:
  static pthread_once_t ponce_;
  static T*             value_;
};

template<typename T>
pthread_once_t Singleton<T>::ponce_ = PTHREAD_ONCE_INIT;

template<typename T>
T* Singleton<T>::value_ = NULL;

}
#endif

