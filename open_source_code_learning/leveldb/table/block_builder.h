// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_TABLE_BLOCK_BUILDER_H_
#define STORAGE_LEVELDB_TABLE_BLOCK_BUILDER_H_

#include <vector>

#include <stdint.h>
#include "leveldb/slice.h"

namespace leveldb {

struct Options;

class BlockBuilder {
 public:
  //构造函数
  explicit BlockBuilder(const Options* options);

  // Reset the contents as if the BlockBuilder was just constructed.
  // 构造时，重置block的各个属性，准备下一次写
  void Reset();

  // REQUIRES: Finish() has not been called since the last call to Reset().
  // REQUIRES: key is larger than any previously added key
  // 往当前块添加一条记录
  void Add(const Slice& key, const Slice& value);

  // Finish building the block and return a slice that refers to the
  // block contents.  The returned slice will remain valid for the
  // lifetime of this builder or until Reset() is called.
  // 当前块写结束，返回这个块的所有内容
  // 返回的字符串在builder生命周期内或者直到Reset被调用前一直有效
  Slice Finish();

  // Returns an estimate of the current (uncompressed) size of the block
  // we are building.
  // 估计这个块的数据量，用于判断当前块是否大于option当中定义的数据块大小
  size_t CurrentSizeEstimate() const;

  // Return true iff no entries have been added since the last Reset()
  bool empty() const {
    return buffer_.empty();
  }

 private:
  const Options*        options_;  //option类
  std::string           buffer_;  //这个块的所有数据   // Destination buffer
  std::vector<uint32_t> restarts_;  //存储每一个Restart[i]   // Restart points
  int                   counter_;  //两个Restart之间记录的条数 // Number of entries emitted since restart
  bool                  finished_;  //是否写完一个块    // Has Finish() been called?
  std::string           last_key_;  //每次写记录的上一条记录

  // No copying allowed
  BlockBuilder(const BlockBuilder&);
  void operator=(const BlockBuilder&);
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_TABLE_BLOCK_BUILDER_H_
