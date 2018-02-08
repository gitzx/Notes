// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// Slice is a simple structure containing a pointer into some external
// storage and a size.  The user of a Slice must ensure that the slice
// is not used after the corresponding external storage has been
// deallocated.
//
// Multiple threads can invoke const methods on a Slice without
// external synchronization, but if any of the threads may call a
// non-const method, all threads accessing the same Slice must use
// external synchronization.

#ifndef STORAGE_LEVELDB_INCLUDE_SLICE_H_
#define STORAGE_LEVELDB_INCLUDE_SLICE_H_

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include "leveldb/export.h"

namespace leveldb {
/* ==>>>
 * 类似redis的sds，slice没有采用C++自带的字符串string
 * 将数据和长度包装成Slice使用，直接操控指针避免不必要的数据拷贝。 
 */
class LEVELDB_EXPORT Slice {
 public:
  //创建一个空的字符串
  // Create an empty slice.
  Slice() : data_(""), size_(0) { }

  //用一个字符串指针和字符串长度初始化一个Slice
  // Create a slice that refers to d[0,n-1].
  Slice(const char* d, size_t n) : data_(d), size_(n) { }

  //用C++字符串初始化Slice，兼容C++字符串
  // Create a slice that refers to the contents of "s"
  Slice(const std::string& s) : data_(s.data()), size_(s.size()) { }

  //用一个字符串指针初始化Slice
  // Create a slice that refers to s[0,strlen(s)-1]
  Slice(const char* s) : data_(s), size_(strlen(s)) { }

  //获取Slice字符串，不能改变值
  // Return a pointer to the beginning of the referenced data
  const char* data() const { return data_; }

  // 获取字符串的长度
  // Return the length (in bytes) of the referenced data
  size_t size() const { return size_; }

  //判断Slice是否为空，如果为空，返回true
  // Return true iff the length of the referenced data is zero
  bool empty() const { return size_ == 0; }

  //重载[]操作符，通过slice[n]获取第n个字符
  // Return the ith byte in the referenced data.
  // REQUIRES: n < size()
  char operator[](size_t n) const {
    assert(n < size());
    return data_[n];
  }

  //将Slice清空
  // Change this slice to refer to an empty array
  void clear() { data_ = ""; size_ = 0; }

  //去除Slice前缀n个字符
  // Drop the first "n" bytes from this slice.
  void remove_prefix(size_t n) {
    assert(n <= size());
    data_ += n;
    size_ -= n;
  }

  //返回Slice的string形式的副本
  // Return a string that contains the copy of the referenced data.
  std::string ToString() const { return std::string(data_, size_); }

  // Three-way comparison.  Returns value:
  //   <  0 iff "*this" <  "b",
  //   == 0 iff "*this" == "b",
  //   >  0 iff "*this" >  "b"
  int compare(const Slice& b) const;

  //判断这个Slice是否以字符串x为前缀
  // Return true iff "x" is a prefix of "*this"
  bool starts_with(const Slice& x) const {
    return ((size_ >= x.size_) &&
            (memcmp(data_, x.data_, x.size_) == 0));
  }

 /* ==>>>
  * 成员属性：指向字符串的指针和这个字符串的长度
  */
 private:
  const char* data_;
  size_t size_;

  // Intentionally copyable
};

//重载==操作符，用于判断Slice==Slice
inline bool operator==(const Slice& x, const Slice& y) {
  return ((x.size() == y.size()) &&
          (memcmp(x.data(), y.data(), x.size()) == 0));
}

//重载不等于操作符
inline bool operator!=(const Slice& x, const Slice& y) {
  return !(x == y);
}

//字符串比较函数
inline int Slice::compare(const Slice& b) const {
  const size_t min_len = (size_ < b.size_) ? size_ : b.size_;
  int r = memcmp(data_, b.data_, min_len);
  if (r == 0) {
    if (size_ < b.size_) r = -1;
    else if (size_ > b.size_) r = +1;
  }
  return r;
}

}  // namespace leveldb


#endif  // STORAGE_LEVELDB_INCLUDE_SLICE_H_

/*
 **声明定义一个空字符串
 *Slice slice；
 **有一个字符串指针初始化Slice
 *const char* p="orange"
 *Slice slice(p);
 **获取Slice的字符串
 *slice.data();
 **获取Slice字符串的长度
 *slice.size()
*/
