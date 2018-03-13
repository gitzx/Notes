// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_DB_VERSION_EDIT_H_
#define STORAGE_LEVELDB_DB_VERSION_EDIT_H_

#include <set>
#include <utility>
#include <vector>
#include "db/dbformat.h"

//version_edit这个类主要是两个版本之间的差量.
//就是当前版本+version_edit即可成为新的版本，version0+version_edit=version1

namespace leveldb {

class VersionSet;

struct FileMetaData {
  int refs; //引用次数
  int allowed_seeks;  //允许的最大查找次数         // Seeks allowed until compaction
  uint64_t number; //SSTable的文件编号
  uint64_t file_size; //SSTable文件大小         // File size in bytes
  InternalKey smallest; //SSTable文件的最小key值  // Smallest internal key served by table
  InternalKey largest; //SSTable文件的最大key值  // Largest internal key served by table

  FileMetaData() : refs(0), allowed_seeks(1 << 30), file_size(0) { }
};

class VersionEdit {
 public:
  VersionEdit() { Clear(); }
  ~VersionEdit() { }

  void Clear();

  void SetComparatorName(const Slice& name) {
    has_comparator_ = true;
    comparator_ = name.ToString();
  }
  void SetLogNumber(uint64_t num) {
    has_log_number_ = true;
    log_number_ = num;
  }
  void SetPrevLogNumber(uint64_t num) {
    has_prev_log_number_ = true;
    prev_log_number_ = num;
  }
  void SetNextFile(uint64_t num) {
    has_next_file_number_ = true;
    next_file_number_ = num;
  }
  void SetLastSequence(SequenceNumber seq) {
    has_last_sequence_ = true;
    last_sequence_ = seq;
  }
  void SetCompactPointer(int level, const InternalKey& key) {
    compact_pointers_.push_back(std::make_pair(level, key));
  }

  // Add the specified file at the specified number.
  // REQUIRES: This version has not been saved (see VersionSet::SaveTo)
  // REQUIRES: "smallest" and "largest" are smallest and largest keys in file
  void AddFile(int level, uint64_t file,
               uint64_t file_size,
               const InternalKey& smallest,
               const InternalKey& largest) {
    FileMetaData f;
    f.number = file;
    f.file_size = file_size;
    f.smallest = smallest;
    f.largest = largest;
    new_files_.push_back(std::make_pair(level, f));
  }

  // Delete the specified "file" from the specified "level".
  void DeleteFile(int level, uint64_t file) {
    deleted_files_.insert(std::make_pair(level, file));
  }

  void EncodeTo(std::string* dst) const;
  Status DecodeFrom(const Slice& src);

  std::string DebugString() const;

 private:
  friend class VersionSet;

  //定义删除文件集合，<层次， 文件编号>
  typedef std::set< std::pair<int, uint64_t> > DeletedFileSet;

  std::string comparator_; //比较器
  uint64_t log_number_; //log文件编号FileNumber
  uint64_t prev_log_number_; //辅助log的FileNumber
  uint64_t next_file_number_; //下一个可用的FileNumber
  SequenceNumber last_sequence_; //用过的最后一个SequnceNumber
  //标志是否存在，验证使用
  bool has_comparator_; //是否有比较器
  bool has_log_number_;
  bool has_prev_log_number_;
  bool has_next_file_number_;
  bool has_last_sequence_;

  //要更新的level ==》compact_pointer
  std::vector< std::pair<int, InternalKey> > compact_pointers_;
  //要删除的sstable文件(compact的input)
  DeletedFileSet deleted_files_;
  //新的文件(compact的output)
  std::vector< std::pair<int, FileMetaData> > new_files_;
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_VERSION_EDIT_H_
