// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <stdio.h>
#include "port/port.h"
#include "leveldb/status.h"

namespace leveldb {

//复制状态字符串，状态字符串只读
const char* Status::CopyState(const char* state) {
  uint32_t size;
  //state的前4个字节表示消息长度即size
  memcpy(&size, state, sizeof(size));
  char* result = new char[size + 5];
  memcpy(result, state, size + 5);
  return result;
}

Status::Status(Code code, const Slice& msg, const Slice& msg2) {
  assert(code != kOk);
  const uint32_t len1 = msg.size();
  const uint32_t len2 = msg2.size();
  //判断第二个字符串长度是否为0，如果不为0，则总长加2，这里2是用于存储':'和' '.
  const uint32_t size = len1 + (len2 ? (2 + len2) : 0);
  //state_ 总长度还包括前5个，其中0..3是消息长度，4是code值
  char* result = new char[size + 5];
  //将消息长度存入result前四个字节
  memcpy(result, &size, sizeof(size));
  //第5个字节存储状态
  result[4] = static_cast<char>(code);
  //第6个字节开始存储消息内容
  memcpy(result + 5, msg.data(), len1);
  //如果msg2长度不为0，则消息内容为msg1 + ': ' + msg2. 
  if (len2) {
    result[5 + len1] = ':';
    result[6 + len1] = ' ';
    memcpy(result + 7 + len1, msg2.data(), len2);
  }
  state_ = result;
}

//返回状态字符串
std::string Status::ToString() const {
  if (state_ == NULL) {
    return "OK";
  } else {
    char tmp[30];
    const char* type;
    switch (code()) {
      case kOk:
        type = "OK";
        break;
      case kNotFound:
        type = "NotFound: ";
        break;
      case kCorruption:
        type = "Corruption: ";
        break;
      case kNotSupported:
        type = "Not implemented: ";
        break;
      case kInvalidArgument:
        type = "Invalid argument: ";
        break;
      case kIOError:
        type = "IO error: ";
        break;
      default:
	//sprintf可能导致缓冲区溢出问题而不被推荐使用。
	//snprintf通过提供缓冲区的可用大小传入参数来保证缓冲区的不溢出，如果超出缓冲区大小则进行截断。
        snprintf(tmp, sizeof(tmp), "Unknown code(%d): ",
                 static_cast<int>(code()));
        type = tmp;
        break;
    }
    std::string result(type);
    uint32_t length;
    memcpy(&length, state_, sizeof(length));
    //下标从5开始表示消息内容
    result.append(state_ + 5, length);
    return result;
  }
}

}  // namespace leveldb
