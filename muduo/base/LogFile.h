#ifndef MUDUO_BASE_LOGFILE_H
#define MUDUO_BASE_LOGFILE_H

#include <muduo/base/Mutex.h>
#include <muduo/base/Types.h>

#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>

namespace muduo
{

namespace FileUtil
{
class AppendFile;
}

//是一个线程安全的日志类
class LogFile : boost::noncopyable
{
 public:
  LogFile(const string& basename,
          size_t rollSize,
          bool threadSafe = true,   //线程安全，默认为true
          int flushInterval = 3,    //刷新间隔默认3s
          int checkEveryN = 1024);  //每写1024次检查一次，是否需要日志滚动
  ~LogFile();

  void append(const char* logline, int len);
  void flush();
  bool rollFile();

 private:
  void append_unlocked(const char* logline, int len);

  static string getLogFileName(const string& basename, time_t* now);

  const string basename_;		//日志文件的basename
  const size_t rollSize_;		//日志文件写满rollsize就换一个新文件
  const int flushInterval_;     //日志写入的间隔时间
  const int checkEveryN_;
  
  //计数器，当count_等于checkEveryN的时候会检测是否需要换一个新的日志文件
  int count_;   

   //智能指针
  boost::scoped_ptr<MutexLock> mutex_; 
   //开始记录日志的时间，(会调整到0点时间),为了方便以时间为单位的日志滚动
  time_t startOfPeriod_;
  time_t lastRoll_;  //上一次滚动日志的时间
  time_t lastFlush_;   //上一次写入日志的时间

  
  boost::scoped_ptr<FileUtil::AppendFile> file_;

  const static int kRollPerSeconds_ = 60*60*24;  //一天，用来标识一天滚动一次日志
};

}
#endif  // MUDUO_BASE_LOGFILE_H
