// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_MUTEX_H
#define MUDUO_BASE_MUTEX_H

#include <muduo/base/CurrentThread.h>
#include <boost/noncopyable.hpp>
#include <assert.h>
#include <pthread.h>

#ifdef CHECK_PTHREAD_RETURN_VALUE
//todo<gaorong>: what does it mean?
#ifdef NDEBUG
__BEGIN_DECLS
extern void __assert_perror_fail (int errnum,
                                  const char *file,
                                  unsigned int line,
                                  const char *function)
    __THROW __attribute__ ((__noreturn__));
__END_DECLS
#endif

//判断返回值的一个宏
//__typeof__可以自动判断类型，可以用在任何地方，甚至用其返回值定义变量
//__assert_perror_fail实现见上面
#define MCHECK(ret) ({ __typeof__ (ret) errnum = (ret);         \
                       if (__builtin_expect(errnum != 0, 0))    \
                         __assert_perror_fail (errnum, __FILE__, __LINE__, __func__);})

#else  // CHECK_PTHREAD_RETURN_VALUE

#define MCHECK(ret) ({ __typeof__ (ret) errnum = (ret);         \
                       assert(errnum == 0); (void) errnum;})

#endif // CHECK_PTHREAD_RETURN_VALUE

namespace muduo
{

// Use as data member of a class, eg.
//
// class Foo
// {
//  public:
//   int size() const;
//
//  private:
//   mutable MutexLock mutex_;
//   std::vector<int> data_; // GUARDED BY mutex_
// };
class MutexLock : boost::noncopyable
{
 public:
  MutexLock()
    : holder_(0)
  {
    MCHECK(pthread_mutex_init(&mutex_, NULL));
  }

  ~MutexLock()
  {
   //断言holder为0才进行释放
    assert(holder_ == 0);
    MCHECK(pthread_mutex_destroy(&mutex_));
  }

  // must be called when locked, i.e. for assertion
  bool isLockedByThisThread() const
  {
    return holder_ == CurrentThread::tid();
  }

  void assertLocked() const
  {
    assert(isLockedByThisThread());
  }

  // internal usage

  void lock()
  {
    MCHECK(pthread_mutex_lock(&mutex_));
    assignHolder();
  }

  void unlock()
  {
    unassignHolder();
    MCHECK(pthread_mutex_unlock(&mutex_));
  }

  pthread_mutex_t* getPthreadMutex() /* non-const */
  {
    return &mutex_;
  }

 private:
 	//友元
  friend class Condition;

	//这个局部对象用在Condition类的wait时候
  class UnassignGuard : boost::noncopyable
  {
   public:
    UnassignGuard(MutexLock& owner)
      : owner_(owner)
    {
      owner_.unassignHolder();
    }

    ~UnassignGuard()
    {
      owner_.assignHolder();
    }

   private:
    MutexLock& owner_;
  };

  void unassignHolder()
  {
    holder_ = 0;
  }

  void assignHolder()
  {
    holder_ = CurrentThread::tid();
  }

  pthread_mutex_t mutex_;
  pid_t holder_;  //持有锁的线程tid
};

// Use as a stack variable, eg.
// int Foo::size() const
// {
//   MutexLockGuard lock(mutex_);
//   return data_.size();
// }
//下面这个类是分装MutexLock，防止在函数退出的时候忘记解锁造成死锁
//它是静态分配，函数返回时自动析构解锁
class MutexLockGuard : boost::noncopyable
{
 public:
  explicit MutexLockGuard(MutexLock& mutex)
    : mutex_(mutex)
  {
    mutex_.lock();
  }

  ~MutexLockGuard()
  {
    mutex_.unlock();
  }

 private:

 //这里是引用
  MutexLock& mutex_;  //与MutexLock是关联关系，并不管理mutex的生命周期所以不是组合关系
};

}

// Prevent misuse like:
// MutexLockGuard(mutex_);
// A tempory object doesn't hold the lock for long!
//定义了一个宏，防止直接构造匿名对象
#define MutexLockGuard(x) error "Missing guard object name"

#endif  // MUDUO_BASE_MUTEX_H
