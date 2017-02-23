// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/base/Thread.h>
#include <muduo/base/CurrentThread.h>
#include <muduo/base/Exception.h>
#include <muduo/base/Logging.h>

#include <boost/static_assert.hpp>
#include <boost/type_traits/is_same.hpp>
#include <boost/weak_ptr.hpp>

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <linux/unistd.h>



namespace muduo
{


//C++中命名空间定义的变量与普通的变量没什么区别，
//只不过是加了一层访问空间标识而已
namespace CurrentThread
{
	//在Currentthread.h中也有相同的命名空间
	//__thread，gcc内置的线程局部存储设施，__thread只能修饰POD类型
  __thread int t_cachedTid = 0; //线程真实pid即tid， 获取缓存是为了防止每次系统效用降低效率
  __thread char t_tidString[32];   //tid的字符串标识形式
  __thread int t_tidStringLength = 6;
  __thread const char* t_threadName = "unknown";

  //编译时断言
  const bool sameType = boost::is_same<int, pid_t>::value;
  BOOST_STATIC_ASSERT(sameType);
}

namespace detail
{

pid_t gettid()
{
  //进行系统调用获取tid
  return static_cast<pid_t>(::syscall(SYS_gettid));
}

void afterFork()
{

  //当多线程的主线程调用了fork后，会将子进程的主线程名字设置为main 
  //用于区分子进程稍后创建的自己的子线程
  muduo::CurrentThread::t_cachedTid = 0;
  muduo::CurrentThread::t_threadName = "main";
  CurrentThread::tid();
  // no need to call pthread_atfork(NULL, NULL, &afterFork);
}

class ThreadNameInitializer
{
 public:
  ThreadNameInitializer()
  {
    muduo::CurrentThread::t_threadName = "main";
    CurrentThread::tid();
	
	//int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void));
    //调用fork时，内部创建子进程前在父进程中会调用prepare，
	//内部创建子进程成功后，父进程会调用parent ，子进程会调用child
    pthread_atfork(NULL, NULL, &afterFork);
  }
};

//全局变量，系统启动在main函数之前就调用该函数
ThreadNameInitializer init;

//保存线程数据的结构体
struct ThreadData
{
  typedef muduo::Thread::ThreadFunc ThreadFunc;
  //主要是三个数据成员
  ThreadFunc func_;
  string name_;
  //这是一个弱引用对象，防止share_ptr出现内存泄露
  //share_ptr可以直接赋值给weak_ptr对象
  boost::weak_ptr<pid_t> wkTid_;

  ThreadData(const ThreadFunc& func,
             const string& name,
             const boost::shared_ptr<pid_t>& tid)
    : func_(func),
      name_(name),
      wkTid_(tid)
  { }

  void runInThread()
  {
    pid_t tid = muduo::CurrentThread::tid();

	//通过boost::weak_ptr::lock函数获得shared_ptr对象
    boost::shared_ptr<pid_t> ptid = wkTid_.lock();
    if (ptid)
    {
      *ptid = tid;
      ptid.reset();
    }

    muduo::CurrentThread::t_threadName = name_.empty() ? "muduoThread" : name_.c_str();
	//为线程设置名字
	::prctl(PR_SET_NAME, muduo::CurrentThread::t_threadName);

	try
    {
      func_();
      muduo::CurrentThread::t_threadName = "finished"; //标识为结束
    }
    catch (const Exception& ex)
    {
      muduo::CurrentThread::t_threadName = "crashed";
      fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
      fprintf(stderr, "reason: %s\n", ex.what());
      fprintf(stderr, "stack trace: %s\n", ex.stackTrace());
      abort();
    }
    catch (const std::exception& ex)
    {
      muduo::CurrentThread::t_threadName = "crashed";
      fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
      fprintf(stderr, "reason: %s\n", ex.what());
      abort();
    }
    catch (...)
    {
      muduo::CurrentThread::t_threadName = "crashed";
      fprintf(stderr, "unknown exception caught in Thread %s\n", name_.c_str());
      throw; // rethrow
    }
  }
};

//不属于任何类的普通全局函数，做为pthread_create的参数
void* startThread(void* obj)
{
  ThreadData* data = static_cast<ThreadData*>(obj);
  data->runInThread();
  delete data;
  return NULL;
}

}
}

using namespace muduo;

void CurrentThread::cacheTid()
{
  if (t_cachedTid == 0)
  {
    t_cachedTid = detail::gettid();
    t_tidStringLength = snprintf(t_tidString, sizeof t_tidString, "%5d ", t_cachedTid);
  }
}

bool CurrentThread::isMainThread()
{
	//主线程的pid等于tid
  return tid() == ::getpid();
}

void CurrentThread::sleepUsec(int64_t usec)
{
  struct timespec ts = { 0, 0 };
  ts.tv_sec = static_cast<time_t>(usec / Timestamp::kMicroSecondsPerSecond);
  ts.tv_nsec = static_cast<long>(usec % Timestamp::kMicroSecondsPerSecond * 1000);
  ::nanosleep(&ts, NULL);
}


AtomicInt32 Thread::numCreated_;

Thread::Thread(const ThreadFunc& func, const string& n)
  : started_(false),
    joined_(false),
    pthreadId_(0),
    tid_(new pid_t(0)),
    func_(func),
    name_(n)
{
 //线程类创建的时候调用这个函数实现初始化
  setDefaultName();
}

#ifdef __GXX_EXPERIMENTAL_CXX0X__
Thread::Thread(ThreadFunc&& func, const string& n)
  : started_(false),
    joined_(false),
    pthreadId_(0),
    tid_(new pid_t(0)),
    func_(std::move(func)),
    name_(n)
{
  setDefaultName();
}

#endif

Thread::~Thread()
{
  if (started_ && !joined_)
  {
   //自动资源回收
    pthread_detach(pthreadId_);
  }
}

void Thread::setDefaultName()
{
  //创建线程的个数加1
  int num = numCreated_.incrementAndGet();
  if (name_.empty())
  {
    char buf[32];
	//如果未设置名字，默认未Thread+线程号
    snprintf(buf, sizeof buf, "Thread%d", num);
    name_ = buf;
  }
}

void Thread::start()
{
  assert(!started_);
  started_ = true;
  // FIXME: move(func_)

  //创建一个ThreadData结构体
  detail::ThreadData* data = new detail::ThreadData(func_, name_, tid_);
  if (pthread_create(&pthreadId_, NULL, &detail::startThread, data))
  {
    //创建失败返回非0，执行这个分支
    started_ = false;
    delete data; // or no delete?
    LOG_SYSFATAL << "Failed in pthread_create";
  }
}

int Thread::join()
{
  assert(started_);
  assert(!joined_);
  joined_ = true;
  return pthread_join(pthreadId_, NULL);
}

