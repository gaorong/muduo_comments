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


//C++ÖĞÃüÃû¿Õ¼ä¶¨ÒåµÄ±äÁ¿ÓëÆÕÍ¨µÄ±äÁ¿Ã»Ê²Ã´Çø±ğ£¬
//Ö»²»¹ıÊÇ¼ÓÁËÒ»²ã·ÃÎÊ¿Õ¼ä±êÊ¶¶øÒÑ
namespace CurrentThread
{
	//ÔÚCurrentthread.hÖĞÒ²ÓĞÏàÍ¬µÄÃüÃû¿Õ¼ä
	//__thread£¬gccÄÚÖÃµÄÏß³Ì¾Ö²¿´æ´¢ÉèÊ©£¬__threadÖ»ÄÜĞŞÊÎPODÀàĞÍ
  __thread int t_cachedTid = 0; //Ïß³ÌÕæÊµpid¼´tid£¬ »ñÈ¡»º´æÊÇÎªÁË·ÀÖ¹Ã¿´ÎÏµÍ³Ğ§ÓÃ½µµÍĞ§ÂÊ
  __thread char t_tidString[32];   //tidµÄ×Ö·û´®±êÊ¶ĞÎÊ½
  __thread int t_tidStringLength = 6;
  __thread const char* t_threadName = "unknown";

  //±àÒëÊ±¶ÏÑÔ
  const bool sameType = boost::is_same<int, pid_t>::value;
  BOOST_STATIC_ASSERT(sameType);
}

namespace detail
{

pid_t gettid()
{
  //½øĞĞÏµÍ³µ÷ÓÃ»ñÈ¡tid
  return static_cast<pid_t>(::syscall(SYS_gettid));
}

void afterFork()
{

  //µ±¶àÏß³ÌµÄÖ÷Ïß³Ìµ÷ÓÃÁËforkºó£¬»á½«×Ó½ø³ÌµÄÖ÷Ïß³ÌÃû×ÖÉèÖÃÎªmain 
  //ÓÃÓÚÇø·Ö×Ó½ø³ÌÉÔºó´´½¨µÄ×Ô¼ºµÄ×ÓÏß³Ì
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
    //µ÷ÓÃforkÊ±£¬ÄÚ²¿´´½¨×Ó½ø³ÌÇ°ÔÚ¸¸½ø³ÌÖĞ»áµ÷ÓÃprepare£¬
	//ÄÚ²¿´´½¨×Ó½ø³Ì³É¹¦ºó£¬¸¸½ø³Ì»áµ÷ÓÃparent £¬×Ó½ø³Ì»áµ÷ÓÃchild
    pthread_atfork(NULL, NULL, &afterFork);
  }
};

//È«¾Ö±äÁ¿£¬ÏµÍ³Æô¶¯ÔÚmainº¯ÊıÖ®Ç°¾Íµ÷ÓÃ¸Ãº¯Êı£
ThreadNameInitializer init;

//±£´æÏß³ÌÊı¾İµÄ½á¹¹Ìå
struct ThreadData
{
  typedef muduo::Thread::ThreadFunc ThreadFunc;
  //Ö÷ÒªÊÇÈı¸öÊı¾İ³ÉÔ±
  ThreadFunc func_;
  string name_;
  //ÕâÊÇÒ»¸öÈõÒıÓÃ¶ÔÏó£¬·ÀÖ¹share_ptr³öÏÖÄÚ´æĞ¹Â¶
  //share_ptr¿ÉÒÔÖ±½Ó¸³Öµ¸øweak_ptr¶ÔÏó
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

	//Í¨¹ıboost::weak_ptr::lockº¯Êı»ñµÃshared_ptr¶ÔÏó
    boost::shared_ptr<pid_t> ptid = wkTid_.lock();
    if (ptid)   
    {
      *ptid = tid;    //¶ÔËùÖ¸µÄ¶ÔÏó½øĞĞ¸³Öµ
       ptid.reset();  //ÒıÓÃ¼ÆÊı¼õ1£¬Í£Ö¹¶ÔÖ¸ÕëµÄ¹²Ïí
    }

    muduo::CurrentThread::t_threadName = name_.empty() ? "muduoThread" : name_.c_str();
	//ÎªÏß³ÌÉèÖÃÃû×Ö
	::prctl(PR_SET_NAME, muduo::CurrentThread::t_threadName);

	try
    {
      func_();
      muduo::CurrentThread::t_threadName = "finished"; //±êÊ¶Îª½áÊø
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

//²»ÊôÓÚÈÎºÎÀàµÄÆÕÍ¨È«¾Öº¯Êı£¬×öÎªpthread_createµÄ²ÎÊı
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
	//Ö÷Ïß³ÌµÄpidµÈÓÚtid
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
 //Ïß³ÌÀà´´½¨µÄÊ±ºòµ÷ÓÃÕâ¸öº¯ÊıÊµÏÖ³õÊ¼»¯
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
   //×Ô¶¯×ÊÔ´»ØÊÕ
    pthread_detach(pthreadId_);
  }
}

void Thread::setDefaultName()
{
  //´´½¨Ïß³ÌµÄ¸öÊı¼Ó1
  int num = numCreated_.incrementAndGet();
  if (name_.empty())
  {
    char buf[32];
	//Èç¹ûÎ´ÉèÖÃÃû×Ö£¬Ä¬ÈÏÎ´Thread+Ïß³ÌºÅ
    snprintf(buf, sizeof buf, "Thread%d", num);
    name_ = buf;
  }
}

void Thread::start()
{
  assert(!started_);
  started_ = true;
  // FIXME: move(func_)

  //´´½¨Ò»¸öThreadData½á¹¹Ìå
  detail::ThreadData* data = new detail::ThreadData(func_, name_, tid_);
  if (pthread_create(&pthreadId_, NULL, &detail::startThread, data))
  {
    //´´½¨Ê§°Ü·µ»Ø·Ç0£¬Ö´ĞĞÕâ¸ö·ÖÖ§
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

