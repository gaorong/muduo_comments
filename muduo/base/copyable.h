#ifndef MUDUO_BASE_COPYABLE_H
#define MUDUO_BASE_COPYABLE_H

namespace muduo
{

//一个空基类，即标识类，用来标识凡是继承该类的对象都是值类型，可以直接拷贝
//即为 值语义
/// A tag class emphasises the objects are copyable.
/// The empty base class optimization applies.
/// Any derived class of copyable should be a value type.
class copyable
{
};

};

#endif  // MUDUO_BASE_COPYABLE_H
