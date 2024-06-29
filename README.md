## TODO

- [x] 完成`block.cc`下`removeRecord()`函数修改+注释

- [x] 完成`blockTest.cc`下`updateRecord()`，`removeRecord()`测试



`removeRecord()` 单测包括：

> 删除已有record+删除不存在record验证

具体指标：

- [x] `remove_result`验证
- [x] `Slots` 改变
- [x] `tombstone` 置位
- [x] `freesize`改变
- [x] `freespace`不变
- [x] `freespacesize`改变



`updateRecord()` 单测包括：

> 更新已有record + 更新不存在record + 更新过长record 验证

具体指标：

- [x] `update_result`验证
- [x] `Slots` 改变
- [x] `freesize`不变
- [x] `freespace`改变
- [x] `freespacesize`改变

> Hidden Confusion

`blockTest.cc`下，更新过长record部分，原来更新`iov[2] = (size_t) getFreeSize() + 135`，不知道`135`怎么来的

## proj2 疑问

- B+Tree的每个叶节点存储的对应的是什么？block, record?

---

## 单元测试

本项目使用Catch2作为单元测试框架，Catch2教程参考：[教程](https://blog.csdn.net/liuyuan185442111/article/details/120392894)

#### 1.使用：

```cpp
// Standard C/C++ main entry point
int main (int argc, char * argv[]) {
	return Catch::Session().run( argc, argv );
}
```

#### 2. TEST_CASE和断言

`SECTION`: `TEST_CASE`的子独立运行单元，但是不并行执行

`REQUIRE()`：当前为真通过，为假停止test_case

`CHECK()`：当前为真通过，为假警告但继续test_case

类似，有`REQUIRE_FALSE()`, `CHECK_FALSE()`

**浮点数比较**：
1. `epsilon`: 百分比误差
2. `margin`: 值误差
3. `scale`: 缩放因子（建立在原数之上）

---

(原始)

# dbimpl工程

《数据库系统原理与实现》课程实验

![系统构架](docs/img/arch.png)

## 编译环境

1. cmake

2. visual studio 2019+

## 编译步骤

1. 打开visual studio 2019 x64 native tools command命令终端；

2. 进入db/build目录；

3. cmake ..

4. make

## 修订记录

### [2021.05.30]

完成Block::insertRecord，正在处理Table::insert，需要添加移动记录的方案。打算在Block中添加一个RecordIterator，这个迭代器是数组型的，功能比Table::BlockIterator要强。

### [2021.04.24]

增加buffer管理层，统一管理所有缓冲。缓冲大小为BLOCK_SIZE，由一个BufDesp结构管理，BufDesp内部有一个引用计数，当引用计数大于0，该缓冲出借给用户，不能释放。向上层暴露几个接口：borrow，write，relref。borrow相当与read，write只是标记了DIRTY，relref释放借用的缓冲。

## TODO

1. struct iovec结构应该包含一个类型的指针，这样就可以避免来回的betoh，很烦。

2. 超块的管理有待完善；

3. wal日志；

4. buffer层需要一个协程来刷盘；

## 实验1 聚集存储

在底层实现聚集存储，定义Block、Record等元素，导出记录的增加、删除、更新、枚举接口。

## 实验2 Btree索引

与实验1配合，采用Btree增加对索引的支持，导出查询接口。

## 实验3 Buffer管理

## 实验4 日志

## 实验5 SQL解析

## 实验6 事务并发与恢复