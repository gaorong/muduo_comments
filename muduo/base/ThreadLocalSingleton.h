// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREADLOCALSINGLETON_H
#define MUDUO_BASE_THREADLOCALSINGLETON_H

#include <boost/noncopyable.hpp>
#include <assert.h>
#include <pthread.h>

namespace muduo
{

//用到了两种本地线程存储机制
template<typename T>
class ThreadLocalSingleton : boost::noncopyable
{
 public:

  static T& instance()
  {
    if (!t_value_)
    {
	  
	  //通过调用Deleter对象，设置单例的时候只是为线程设置了一个单例
      t_value_ = new T();
      deleter_.set(t_value_);    //可以直接使用deleter,在程序运行开始已经为静态类初始化了
    }
    return *t_value_;
  }

  static T* pointer()
  {
    return t_value_;
  }

 private:
  ThreadLocalSingleton();
  ~ThreadLocalSingleton();

  static void destructor(void* obj)
  {
    assert(obj == t_value_);
    typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1];
    T_must_be_complete_type dummy; (void) dummy;
    delete t_value_;
    t_value_ = 0;
  }

	//这个嵌套类只是为了实现自动销毁对象，不需要我们显式销毁
	//所以根本不需要封装get方法，上面已经有了pointer方法
	//当Deleter销毁的时候，会调用析构函数，继而会调用destructor实现对象的自动销毁
  class Deleter
  {
   public:
    Deleter()
    {
	  //指定一个销毁时的回掉函数destructor，这样就不需要我们每次显式的调用删除对象了     
      pthread_key_create(&pkey_, &ThreadLocalSingleton::destructor);
    }

    ~Deleter()
    {
      pthread_key_delete(pkey_);
    }

    void set(T* newObj)
    {
      assert(pthread_getspecific(pkey_) == NULL);
      pthread_setspecific(pkey_, newObj);
    }

    pthread_key_t pkey_;
  };

  static __thread T* t_value_;   //也是线程私有的指针，指针是POD类型，可以直接__thread定义
  static Deleter deleter_;    //静态的Delter类，
};


//静态类的定义
template<typename T>
__thread T* ThreadLocalSingleton<T>::t_value_ = 0;

template<typename T>
typename ThreadLocalSingleton<T>::Deleter ThreadLocalSingleton<T>::deleter_;

}
#endif
