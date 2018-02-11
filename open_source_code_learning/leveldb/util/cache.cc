// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "leveldb/cache.h"
#include "port/port.h"
#include "util/hash.h"
#include "util/mutexlock.h"

namespace leveldb {

Cache::~Cache() {
}

namespace {

//这里的cache缓存是指内存缓存，缓存一个数据时，leveldb的工作流程：
//1.创建一个容量大小capacity 的Cache（Cache类是一个抽象类，调用NewLRUCache函数返回一个SharedLRUCache对象）
//2.(ShardedLRUCache类)：将容量大小capacity的缓存分成了很多个小缓存LRUCache；将缓存数据data进行hash操作，得到的hash值定位得到属于哪个小缓存LRUCache。
//3.(LRUCache类)：小缓存LRUCache里实现了一个双向链表lru_和一个二级指针数组table_用来缓存数据。双向链表lru_用来保证当缓存容量饱满时，
//清除比较旧的缓存数据；二级指针数组table_用来保证缓存数据的快速查找；lru_.pre存储比较新的数据，lru_.next存储比较旧的数据；
//缓存数据的总大小大于缓存LRUCache的容量大小，则循环从双向链表lru_的next取缓存数据，将其从双向链表lru_和二级指针数组table_中移除，
//直到缓存数据的总大小小于缓存LRUCache的容量大小。
//4.(HandleTable类)：根据缓存数据的hash值，定位缓存数据属于哪个一级指针，然后遍历这一级指针上存放的二级指针链表，查找缓存数据。
//5.(LRUHandle结构)：缓存数据封装成LRUHandle数据对象，进行存储；双向链表也是LRUHandle数据对象，使用了next和prev字段；
//二级指针数组中的二级指针链表也是LRUHandle数据对象，使用了next_hash字段。
//

// LRU cache implementation
//
// Cache entries have an "in_cache" boolean indicating whether the cache has a
// reference on the entry.  The only ways that this can become false without the
// entry being passed to its "deleter" are via Erase(), via Insert() when
// an element with a duplicate key is inserted, or on destruction of the cache.
//
// The cache keeps two linked lists of items in the cache.  All items in the
// cache are in one list or the other, and never both.  Items still referenced
// by clients but erased from the cache are in neither list.  The lists are:
// - in-use:  contains the items currently referenced by clients, in no
//   particular order.  (This list is used for invariant checking.  If we
//   removed the check, elements that would otherwise be on this list could be
//   left as disconnected singleton lists.)
// - LRU:  contains the items not currently referenced by clients, in LRU order
// Elements are moved between these lists by the Ref() and Unref() methods,
// when they detect an element in the cache acquiring or losing its only
// external reference.

// An entry is a variable length heap-allocated structure.  Entries
// are kept in a circular doubly linked list ordered by access time.
struct LRUHandle {
  void* value;  //存储键值对的值
  void (*deleter)(const Slice&, void* value);  //结构体的清除函数
  LRUHandle* next_hash;  //二级指针数组的二级指针链表
  LRUHandle* next;  //双向链表，指向比较旧的数据
  LRUHandle* prev;  //双向链表，指向比较新的数据
  size_t charge;  //这个节点占用的内存      // TODO(opt): Only allow uint32_t?
  size_t key_length;  //这个节点键值得长度
  bool in_cache;  //判断cache是否包括一个入口引用      // Whether entry is in the cache.
  uint32_t refs;  //这个节点引用次数，当次数为0时，即可删除      // References, including cache reference, if present.
  uint32_t hash;  //这个键值得哈希值      // Hash of key(); used for fast sharding and comparisons
  char key_data[1];  //key的首地址   // Beginning of key

  Slice key() const {
    // next_ is only equal to this if the LRU handle is the list head of an
    // empty list. List heads never have meaningful keys.
    assert(next != this);  //当LRU handle为空空链表头节点时，next等于this

    return Slice(key_data, key_length);
  }
};

// We provide our own simple hash table since it removes a whole bunch
// of porting hacks and is also faster than some of the built-in hash
// table implementations in some of the compiler/runtime combinations
// we have tested.  E.g., readrandom speeds up by ~5% over the g++
// 4.4.3's builtin hashtable.
class HandleTable {
 public:
  HandleTable() : length_(0), elems_(0), list_(NULL) { Resize(); }
  ~HandleTable() { delete[] list_; }

  //在哈希表中查询一个LRUHandle
  LRUHandle* Lookup(const Slice& key, uint32_t hash) {
    return *FindPointer(key, hash);
  }

  //在哈希表中添加一个LRUHandle
  LRUHandle* Insert(LRUHandle* h) {
    LRUHandle** ptr = FindPointer(h->key(), h->hash);  //查找要插入的节点的地址
    LRUHandle* old = *ptr;
    h->next_hash = (old == NULL ? NULL : old->next_hash);
    *ptr = h;
    if (old == NULL) {
      ++elems_;
      if (elems_ > length_) {
        // Since each cache entry is fairly large, we aim for a small
        // average linked list length (<= 1).
        Resize();
      }
    }
    return old;
  }

  //在哈希表中删除一个LRUHandle
  LRUHandle* Remove(const Slice& key, uint32_t hash) {
    LRUHandle** ptr = FindPointer(key, hash);  //查找要删除节点的位置
    LRUHandle* result = *ptr;
    if (result != NULL) {
      *ptr = result->next_hash;
      --elems_;
    }
    return result;
  }

 private:
  // The table consists of an array of buckets where each bucket is
  // a linked list of cache entries that hash into the bucket.
  uint32_t length_;  //哈希数组的长度
  uint32_t elems_;  //哈希表存储元素的数量
  LRUHandle** list_;  //哈希数组的指针，因为存储的是指针，因此类型为指针的指针

  // Return a pointer to slot that points to a cache entry that
  // matches key/hash.  If there is no such cache entry, return a
  // pointer to the trailing slot in the corresponding linked list.
  // 在哈希表中，通过key和hash值查询LRUHandle
  LRUHandle** FindPointer(const Slice& key, uint32_t hash) {
    //根据hash值，节点在哈希表中的索引位置
    //hash & (length_ - 1)的运算结果是0到length-1
    LRUHandle** ptr = &list_[hash & (length_ - 1)];
    //迭代哈希链，扎到键值相等的节点
    //二级指针链表 *ptr不为空，遍历二级指针链表找到hash相同且key也相同的节点
    while (*ptr != NULL &&
           ((*ptr)->hash != hash || key != (*ptr)->key())) {
      ptr = &(*ptr)->next_hash;
    }
    return ptr;
  }

  //改变哈希表的大小
  void Resize() {
    uint32_t new_length = 4;  //初始化第一次分配哈希表数组长度为4
    while (new_length < elems_) {
      //当后面元素数量大于哈希表长度时，
      //再次分配时哈希表的大小为数组长度的两倍
      new_length *= 2;  
    }
    //下面new方法，只给一级指针分配了内存块
    LRUHandle** new_list = new LRUHandle*[new_length];
    //避免一级指针分配的内存块，存有野指针，需要使用memset对内存块清零处理
    //new_list和&new_list[i]是一级指针，
    //*new_list和new_list[i]是二级指针，
    //**new_list是二级指针存储的值。
    //将一级指针的内存块中存储0，也就是将二级指针*new_list置空
    memset(new_list, 0, sizeof(new_list[0]) * new_length);
    uint32_t count = 0;
    for (uint32_t i = 0; i < length_; i++) { //遍历一级指针
 	//每个h通过表达式hash&(length-1)得到属于一级指针的位置
	//一级指针上存放的二级指针链表，通过h的next_hash链接起来
      LRUHandle* h = list_[i]; 
	//遍历的逻辑是重新定位h属于的一级指针。并在新的一级指针上组成新的二级链表。
      while (h != NULL) {  
        LRUHandle* next = h->next_hash;  
        uint32_t hash = h->hash; 
	//定位新的一级指针， *ptr就是new_list[hash & (new_length - 1)]
        LRUHandle** ptr = &new_list[hash & (new_length - 1)];
	//如果是第一次运行，则*ptr为NULL，其他则是渠道上个循环h的地址
        h->next_hash = *ptr;  
        *ptr = h;  
	//二级链表的下一个节点
        h = next;  
        count++;  // 计数
      }
    }
    assert(elems_ == count);
    delete[] list_;  //析构原有哈希数组
    list_ = new_list;  //list_指向新的哈希数组
    length_ = new_length;  //更新长度
  }
};

// A single shard of sharded cache.
class LRUCache {
 public:
  LRUCache();
  ~LRUCache();

  // Separate from constructor so caller can easily make an array of LRUCache
  void SetCapacity(size_t capacity) { capacity_ = capacity; }

  // Like Cache methods, but with an extra "hash" parameter.
  Cache::Handle* Insert(const Slice& key, uint32_t hash,
                        void* value, size_t charge,
                        void (*deleter)(const Slice& key, void* value));
  Cache::Handle* Lookup(const Slice& key, uint32_t hash);
  void Release(Cache::Handle* handle);
  void Erase(const Slice& key, uint32_t hash);
  void Prune();
  size_t TotalCharge() const {
    MutexLock l(&mutex_);
    return usage_;
  }

 private:
  void LRU_Remove(LRUHandle* e);
  void LRU_Append(LRUHandle*list, LRUHandle* e);
  void Ref(LRUHandle* e);
  void Unref(LRUHandle* e);
  bool FinishErase(LRUHandle* e);

  // Initialized before use.
  // 缓存数据的总容量，这个双向链表的存储容量，由每个节点的charge累加和
  size_t capacity_;

  // mutex_ protects the following state.
  // 这个链表的互斥量
  mutable port::Mutex mutex_;
  //缓存数据的总大小
  size_t usage_;

  // Dummy head of LRU list.
  // lru.prev is newest entry, lru.next is oldest entry.
  // Entries have refs==1 and in_cache==true.
  //双向循环链表，有大小限制，当缓存不够时，保证先清除旧的数据 
  LRUHandle lru_;

  // Dummy head of in-use list.
  // Entries are in use by clients, and have refs >= 2 and in_cache==true.
  // 已使用链表的傀儡节点
  LRUHandle in_use_;
  //哈希表，一个链表还附带一个哈希表
  //二级指针数组，链表没有大小限制，动态扩展大小，保证数据快速查找
  //hash定位一级指针，得到存放在一级指针上的二级指针链表，遍历查找数据
  HandleTable table_;
};

LRUCache::LRUCache()
    : usage_(0) {
  // Make empty circular linked lists.
  // 初始为空的循环链表，前后指针都指向自己
  lru_.next = &lru_;
  lru_.prev = &lru_;
  in_use_.next = &in_use_;
  in_use_.prev = &in_use_;
}

LRUCache::~LRUCache() {
  assert(in_use_.next == &in_use_);  // Error if caller has an unreleased handle
  for (LRUHandle* e = lru_.next; e != &lru_; ) {
    LRUHandle* next = e->next;
    assert(e->in_cache);
    e->in_cache = false;
    assert(e->refs == 1);  // Invariant of lru_ list.
    Unref(e);
    e = next;
  }
}

void LRUCache::Ref(LRUHandle* e) {
  if (e->refs == 1 && e->in_cache) {  // If on lru_ list, move to in_use_ list.
     //先删除再加入
     //当缓存不够时会先清除lru_的next处的数据，因此需要保证先清除比较旧的数据
    LRU_Remove(e);
    LRU_Append(&in_use_, e);
  }
  e->refs++;
}

void LRUCache::Unref(LRUHandle* e) {
  assert(e->refs > 0);
  e->refs--;
  if (e->refs == 0) { // Deallocate.
    assert(!e->in_cache);
    (*e->deleter)(e->key(), e->value);
    free(e);
  } else if (e->in_cache && e->refs == 1) {  // No longer in use; move to lru_ list.
    LRU_Remove(e);
    LRU_Append(&lru_, e);
  }
}

void LRUCache::LRU_Remove(LRUHandle* e) {
  e->next->prev = e->prev;
  e->prev->next = e->next;
}

void LRUCache::LRU_Append(LRUHandle* list, LRUHandle* e) {
  // Make "e" newest entry by inserting just before *list
  // 在list之前插入，使e为最新的节点
  e->next = list;
  e->prev = list->prev;
  e->prev->next = e;
  e->next->prev = e;
}

Cache::Handle* LRUCache::Lookup(const Slice& key, uint32_t hash) {
  MutexLock l(&mutex_);
  LRUHandle* e = table_.Lookup(key, hash);
  if (e != NULL) {
    Ref(e);
  }
  return reinterpret_cast<Cache::Handle*>(e);
}

void LRUCache::Release(Cache::Handle* handle) {
  MutexLock l(&mutex_);
  Unref(reinterpret_cast<LRUHandle*>(handle));
}

Cache::Handle* LRUCache::Insert(
    const Slice& key, uint32_t hash, void* value, size_t charge,
    void (*deleter)(const Slice& key, void* value)) {
  MutexLock l(&mutex_);  //多线程使用，添加删除均需要锁住

  //给插入的节点分配空间
  //减去记录key的首地址大小(一个字节)，加上key实际大小
  LRUHandle* e = reinterpret_cast<LRUHandle*>(
      malloc(sizeof(LRUHandle)-1 + key.size()));
  e->value = value;
  e->deleter = deleter;
  e->charge = charge;
  e->key_length = key.size();
  e->hash = hash;
  e->in_cache = false;
  e->refs = 1;  //返回值引用一次 // for the returned handle.
  memcpy(e->key_data, key.data(), key.size());

  if (capacity_ > 0) {
    e->refs++; //链表引用一次  // for the cache's reference.
    e->in_cache = true;
    LRU_Append(&in_use_, e);  //添加进循环链表
    usage_ += charge;  //链表空间使用量更新
    FinishErase(table_.Insert(e));
  } else {  // don't cache. (capacity_==0 is supported and turns off caching.)
    // next is read by key() in an assert, so it must be initialized
    e->next = NULL;
  }
  //缓存不够用时，从循环链表的lru_next开始删除节点，同时要把哈希表的节点删除
  //缓存不够，清除比较旧的数据
  while (usage_ > capacity_ && lru_.next != &lru_) {
    LRUHandle* old = lru_.next;
    assert(old->refs == 1);
    bool erased = FinishErase(table_.Remove(old->key(), old->hash));
    if (!erased) {  // to avoid unused variable when compiled NDEBUG
      assert(erased);
    }
  }

  //返回新插入节点的指针
  return reinterpret_cast<Cache::Handle*>(e);
}

// If e != NULL, finish removing *e from the cache; it has already been removed
// from the hash table.  Return whether e != NULL.  Requires mutex_ held.
bool LRUCache::FinishErase(LRUHandle* e) {
  if (e != NULL) {
    //如果e!=NULL，从缓存中删除节点
    assert(e->in_cache);
    LRU_Remove(e);
    e->in_cache = false;
    usage_ -= e->charge;
    Unref(e);
  }
  return e != NULL;
}

void LRUCache::Erase(const Slice& key, uint32_t hash) {
  MutexLock l(&mutex_);  //多线程使用
  FinishErase(table_.Remove(key, hash));
}

void LRUCache::Prune() {
  MutexLock l(&mutex_);
  while (lru_.next != &lru_) {
    LRUHandle* e = lru_.next;
    assert(e->refs == 1);
    bool erased = FinishErase(table_.Remove(e->key(), e->hash));
    if (!erased) {  // to avoid unused variable when compiled NDEBUG
      assert(erased);
    }
  }
}

//ShardedLRUCache封装类，有16个LRUCache类，即16个环形链表和哈希表
//根据键的哈希值，调用相应的LRUCache类的方法
static const int kNumShardBits = 4;
static const int kNumShards = 1 << kNumShardBits;

class ShardedLRUCache : public Cache {
 private:
  LRUCache shard_[kNumShards];
  port::Mutex id_mutex_;
  uint64_t last_id_;

  //返回这个键值的哈希值，调用hash函数
  static inline uint32_t HashSlice(const Slice& s) {
    return Hash(s.data(), s.size(), 0);
  }

  //得到shard_数组的下标
  static uint32_t Shard(uint32_t hash) {
     //hash是4个字节即32位，向右移动28位，则剩下高4位有效位
     //最小的是0000，最大的是1111等于15，即得到的数字是在[0,15]范围内
    return hash >> (32 - kNumShardBits);
  }

 public:
  explicit ShardedLRUCache(size_t capacity)
      : last_id_(0) {
       //将容量平均分成16份，如果有剩余，将剩余补全	      
    const size_t per_shard = (capacity + (kNumShards - 1)) / kNumShards;
    for (int s = 0; s < kNumShards; s++) {
      shard_[s].SetCapacity(per_shard);
    }
  }
  virtual ~ShardedLRUCache() { }
  // charge 数据大小
  virtual Handle* Insert(const Slice& key, void* value, size_t charge,
                         void (*deleter)(const Slice& key, void* value)) {
    const uint32_t hash = HashSlice(key);
    return shard_[Shard(hash)].Insert(key, hash, value, charge, deleter);
  }
  virtual Handle* Lookup(const Slice& key) {
    const uint32_t hash = HashSlice(key);
    return shard_[Shard(hash)].Lookup(key, hash);
  }
  virtual void Release(Handle* handle) {
    LRUHandle* h = reinterpret_cast<LRUHandle*>(handle);
    shard_[Shard(h->hash)].Release(handle);
  }
  virtual void Erase(const Slice& key) {
    const uint32_t hash = HashSlice(key);
    shard_[Shard(hash)].Erase(key, hash);
  }
  virtual void* Value(Handle* handle) {
    return reinterpret_cast<LRUHandle*>(handle)->value;
  }
  virtual uint64_t NewId() {
    MutexLock l(&id_mutex_);
    return ++(last_id_);
  }
  virtual void Prune() {
    for (int s = 0; s < kNumShards; s++) {
      shard_[s].Prune();
    }
  }
  virtual size_t TotalCharge() const {
    size_t total = 0;
    for (int s = 0; s < kNumShards; s++) {
      total += shard_[s].TotalCharge();
    }
    return total;
  }
};

}  // end anonymous namespace

Cache* NewLRUCache(size_t capacity) {
  return new ShardedLRUCache(capacity);
}

}  // namespace leveldb
