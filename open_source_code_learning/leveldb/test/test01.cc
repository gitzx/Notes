/*
 * 下载leveldb源码，cd到leveld目录下执行 make （mac环境下测试）
 * 执行make后，leveldb目录下会生成out-static和out-shared
 * 将include/leveldb目录拷贝到/usr/local/include
 * 1. 使用静态链接库编译(leveldb有用到线程相关调用)：
 *  g++ test01.cc -o test01 ../out-static/libleveldb.a -lpthread
 *
 * 2. 使用动态链接库编译：
 *  将动态链接库文件拷贝到/usr/local/lib下
 *  sudo cp libleveldb.dylib.1.20 /usr/local/lib
 *  cd /usr/local/lib
 *  sudo ln -s libleveldb.dylib.1.20 libleveldb.dylib.1
 *  sudo ln -s libleveldb.dylib.1 libleveldb.dylib
 *
 *  g++ test01.cc -o test01 -lpthread -lleveldb
 */

#include <iostream>
#include <cassert>
#include <cstdlib>
#include <string>
#include <leveldb/db.h>
using namespace std;

int main(void)
{
	leveldb::DB *db;
	leveldb::Options options;
	options.create_if_missing = true;
	leveldb::Status status = leveldb::DB::Open(options, "./testdb", &db);
	assert(status.ok());
	std::string key1 = "hello";
	std::string value1 = "world";
	std::string value;
	leveldb::Status s = db->Put(leveldb::WriteOptions(), key1, value1);
	if(s.ok())
	{
		s=db->Get(leveldb::ReadOptions(), "hello", &value);
	}
	else
	{
		cout << s.ToString() << endl;
	}
	if(s.ok())
	{
		cout << value << endl;
	}
	else
	{
		cout << s.ToString() << endl;
	}
	delete db;
	return 0;
}

/*
 * world
 */
© 2018 GitHub, Inc.