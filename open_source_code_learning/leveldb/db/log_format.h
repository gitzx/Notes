// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// Log format information shared by reader and writer.
// See ../doc/log_format.md for more detail.

//log文件按块划分，默认每块为32768kb(32M)。唯一的例外是文件结尾包含部分block
//trailer：如果block最后剩余的部分小于record的头长度(checksum/length/type 共7bytes)，
//则剩余的部分作为block的trailer，填入0不使用，record写入下一个block中。
//每个block由连续的record组成。record的格式：| CRC(4 byte) | Length(2 byte) | type(1 byte) | data |

#ifndef STORAGE_LEVELDB_DB_LOG_FORMAT_H_
#define STORAGE_LEVELDB_DB_LOG_FORMAT_H_

namespace leveldb {
namespace log {

enum RecordType {
  // Zero is reserved for preallocated files
  kZeroType = 0,

  kFullType = 1, //记录完全在一个block中

  // For fragments
  kFirstType = 2, //当前block容纳不了所有的内容，记录的第一个片在本block中
  kMiddleType = 3, //记录的内容的起始位置不在本block，结束位置也不在本block
  kLastType = 4  //记录的起始位置不在本block，但结束位置在本block
};
static const int kMaxRecordType = kLastType;

static const int kBlockSize = 32768; //每个block块大小

// Header is checksum (4 bytes), length (2 bytes), type (1 byte).
// checksum记录的是type和data的crc校验，小端
// length是record内保存的data长度，小端
// type是指保存完整数据的全部，开始，中间或最后部分
static const int kHeaderSize = 4 + 2 + 1; //record头部信息大小

}  // namespace log
}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_LOG_FORMAT_H_
