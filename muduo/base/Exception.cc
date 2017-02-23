// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/base/Exception.h>

//#include <cxxabi.h>
#include <execinfo.h>
#include <stdlib.h>
#include <stdio.h>


using namespace muduo;

Exception::Exception(const char* msg)
  : message_(msg)
{
  //在构造函数中调用fillStackTrace
  fillStackTrace();
}

Exception::Exception(const string& msg)
  : message_(msg)
{
  fillStackTrace();
}

Exception::~Exception() throw ()
{
}

const char* Exception::what() const throw()
{
  return message_.c_str();
}

const char* Exception::stackTrace() const throw()
{
  return stack_.c_str();
}

void Exception::fillStackTrace()
{
  const int len = 200;
  void* buffer[len];
  //下面两个函数的用法详见man手册
  // 函数调用栈回溯，保存各个栈帧的地址，参数使一个指针数组
  int nptrs = ::backtrace(buffer, len);
  //将地址转换为函数符号
  char** strings = ::backtrace_symbols(buffer, nptrs);
  if (strings)
  {
    for (int i = 0; i < nptrs; ++i)
    {
      //C++为支持重载，对函数名进行了名字改编，这里输出的都是改编后的函数名
      // TODO demangle funcion name with abi::__cxa_demangle
      stack_.append(strings[i]);
      stack_.push_back('\n');
    }
	//man手册中说到，保存符号信息的数组必须由调用者释放 	
    free(strings);
  }
}


//以下代码为自己实现，非muduo库原有，对一个函数名字的demangled
//主要是调用__cxa_demangle函数实现	
string Exception::demangle(const char* symbol)
{
  size_t size;
  int status;
  char temp[128];
  char* demangled;
  //first, try to demangle a c++ name
  if (1 == sscanf(symbol, "%*[^(]%*[^_]%127[^)+]", temp)) {
    if (NULL != (demangled = abi::__cxa_demangle(temp, NULL, &size, &status))) {
      string result(demangled);
      free(demangled);
      return result;
    }
  }
  //if that didn't work, try to get a regular c symbol
  if (1 == sscanf(symbol, "%127s", temp)) {
    return temp;
  }
 
  //if all else fails, just return the symbol
  return symbol;
}



