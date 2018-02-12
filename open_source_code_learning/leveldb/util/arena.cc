// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "util/arena.h"
#include <assert.h>

namespace leveldb {

//arena是leveldb的内存管理工具：预先向系统申请一块内存，此后申请内存先在上一次分配的内存块中查找
//是否剩余空间能够容纳当前申请的内存，如果足够容纳，就直接使用剩余空间；
//否则根据当前申请内存的大小重新分配一块空间，如果大于1024，直接分配一块新的内存空间，大小与申请空间大小相同，
//若不大于1024，那么重新分配一块4096大小的空间，使用该空间来存放待申请的空间，之前的剩余空间就被浪费了。
//
//主要目的：
//1. 避免频繁的进行malloc/new和free/delete操作；
//2. 避免造成大量的内存碎片

static const int kBlockSize = 4096;  //常量变量名k开头

//初始化
Arena::Arena() : memory_usage_(0) {
  alloc_ptr_ = NULL;  // First allocation will allocate a block
  alloc_bytes_remaining_ = 0;
}

//删除申请的内存
Arena::~Arena() {
  for (size_t i = 0; i < blocks_.size(); i++) {
    delete[] blocks_[i];
  }
}

//
char* Arena::AllocateFallback(size_t bytes) {
  //如果大于1024，则直接分配新的存储空间
  if (bytes > kBlockSize / 4) {
    // Object is more than a quarter of our block size.  Allocate it separately
    // to avoid wasting too much space in leftover bytes.
    char* result = AllocateNewBlock(bytes);
    return result;
  }

  // We waste the remaining space in the current block.
  // 如果不大于1024，则分配一块标准大小的内存块，当前块的剩余空间就被浪费了
  alloc_ptr_ = AllocateNewBlock(kBlockSize);
  alloc_bytes_remaining_ = kBlockSize;

  //指定返回的位置
  char* result = alloc_ptr_;
  //指针移动到空闲内存开始的地方
  alloc_ptr_ += bytes;
  //记录剩余内存
  alloc_bytes_remaining_ -= bytes;
  return result;
}

//考虑内存对齐的内存分配，align必须为2的指数次方
char* Arena::AllocateAligned(size_t bytes) {
  const int align = (sizeof(void*) > 8) ? sizeof(void*) : 8;
  assert((align & (align-1)) == 0);   // Pointer size should be a power of 2
  //计算出alloc_ptr_ % align 
  size_t current_mod = reinterpret_cast<uintptr_t>(alloc_ptr_) & (align-1);
  //对齐还需要向前移动的大小
  size_t slop = (current_mod == 0 ? 0 : align - current_mod);
  size_t needed = bytes + slop;
  char* result;
  if (needed <= alloc_bytes_remaining_) {
    result = alloc_ptr_ + slop;
    alloc_ptr_ += needed;
    alloc_bytes_remaining_ -= needed;
  } else {
    // AllocateFallback always returned aligned memory
    result = AllocateFallback(bytes);
  }
  assert((reinterpret_cast<uintptr_t>(result) & (align-1)) == 0);
  return result;
}

//分配新的内存块
char* Arena::AllocateNewBlock(size_t block_bytes) {
  char* result = new char[block_bytes];
  blocks_.push_back(result);
  memory_usage_.NoBarrier_Store(
      reinterpret_cast<void*>(MemoryUsage() + block_bytes + sizeof(char*)));
  return result;
}

}  // namespace leveldb
