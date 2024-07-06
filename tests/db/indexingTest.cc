#include "../catch.hpp"
#include <db/block.h>
#include <db/endian.h>
#include <db/record.h>
#include <db/buffer.h>
#include <db/file.h>
#include <db/table.h>
#include <db/indexing.h>

using namespace db;

TEST_CASE("db/indexing.cc"){
    SECTION("Node::leaf"){
        //打开表
        Table table;
        table.open("table"); // ATTENTION: 后期需要改成别的表
        //构建一个节点
        unsigned int node_id = table.allocate();
        BufDesp *desp = kBuffer.borrow(table.name_.c_str(), node_id);
        Node node;
        node.attach(desp->buffer);
        // 设置为叶子节点
        node.set_leaf(1);
        REQUIRE(node.is_leaf() == 1);
        // 设置为非叶子节点
        node.set_leaf(0);
        REQUIRE(node.is_leaf() == 0);
        // 删除节点
        table.deallocate(node_id);
    }

    // SECTION("Node::get_left");

    // SECTION("Node::set_left");

    // SECTION("Node::same_key");

    SECTION("Bptree::set_table")
    {
        // 创建新表及其域
        // 填充关系
        RelationInfo relation;
        relation.path = "table_two.dat";

        // id char(20) varchar
        FieldInfo field;
        field.name = "key";
        field.index = 0;
        field.length = 8;
        field.type = findDataType("INT");
        relation.fields.push_back(field);

        field.name = "value";
        field.index = 1;
        field.length = 20;
        field.type = findDataType("INT");
        relation.fields.push_back(field);

        relation.count = 2;
        relation.key = 0;

        int ret = kSchema.create("table_two", relation);

        // 打开新table并绑定到Bptree上
        Table table_two;
        table_two.open("table_two");

        Bptree tree;
        tree.set_table(&table_two, 0, 1);
        // REQUIRE(tree.table_ == table_two);
        // TODO:判断key_type与value_type
        // REQUIRE(tree.key_type == );
        // REQUIRE(tree.value_type == );
    }

    // SECTION("Bptree::attach_node");

    // SECTION("Bptree::node_append");

    SECTION("Bptree::clear_tree")
    {
        //打开表
        Table table_two;
        table_two.open("table_two");
        Bptree tree;
        tree.set_table(&table_two, 0, 1);
        // 读超级块
        SuperBlock super;
        BufDesp *desp = kBuffer.borrow(table_two.name_.c_str(), 0);
        super.attach(desp->buffer);
        desp->relref();
        REQUIRE(super.getRoot() == 0);
        // 构建一个根节点
        unsigned int root_id = table_two.allocate();
        REQUIRE(table_two.dataCount() == 1);
        super.setRoot(root_id);
        REQUIRE(super.getRoot() == root_id);
        // 读根节点
        Node root_node;
        root_node.setTable(&table_two);
        tree.attach_node(root_node, root_id);
        // 插入三个节点
        int key = 10;
        unsigned int mid_child = table_two.allocate();
        REQUIRE(table_two.dataCount() == 2);
        std::vector<struct iovec> iov(2);
        key = htobe32(key);
        mid_child = htobe32(mid_child);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &mid_child;
        iov[1].iov_len = sizeof(int);
        root_node.insertRecord(iov);
        mid_child = be32toh(mid_child);
        // 记录2
        // 此时树的结构为      10         20
        //                      mid_child    right_child
        key = 20;
        unsigned int right_child = table_two.allocate();
        REQUIRE(table_two.dataCount() == 3);
        key = htobe32(key);
        right_child = htobe32(right_child);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &right_child;
        iov[1].iov_len = sizeof(int);
        root_node.insertRecord(iov);
        right_child = be32toh(right_child);
        // 记录3
        // 此时树的结构为
        //                     10               20
        //       left_child(5)      mid_child(3)     right_child(4)
        unsigned int left_child = table_two.allocate();
        root_node.setNext(left_child);

        // 分别将left_child,mid_child,right_child设置为叶子节点
        tree.attach_node(root_node, left_child);
        root_node.set_leaf(true);
        tree.attach_node(root_node, mid_child);
        root_node.set_leaf(true);
        tree.attach_node(root_node, right_child);
        root_node.set_leaf(true);
        // 测试清空树的功能
        tree.clear_tree();
        REQUIRE(table_two.dataCount() == 0);
        REQUIRE(super.getRoot() == 0);
    }

    SECTION("Bptree::get_root")
    {
        //打开表
        Table table_two;
        table_two.open("table_two");
        Bptree tree;
        tree.set_table(&table_two, 0, 1);
        // 读超级块
        SuperBlock super;
        BufDesp *desp = kBuffer.borrow(table_two.name_.c_str(), 0);
        super.attach(desp->buffer);
        desp->relref();
        // 测试get_root,根节点为空的情况
        std::pair<bool, unsigned int> root_test = tree.get_root();
        REQUIRE(root_test.first == false);
        REQUIRE(root_test.second == super.getRoot());
        // 构建一个根节点
        unsigned int root_id = table_two.allocate();
        REQUIRE(table_two.dataCount() == 1);
        super.setRoot(root_id);
        // 读根节点，根节点设置为叶子节点
        Node root_node;
        root_node.setTable(&table_two);
        tree.attach_node(root_node, root_id);
        root_node.set_leaf(true);
        REQUIRE(super.getRoot() == root_id);
        root_test = tree.get_root();
        REQUIRE(root_test.first == true);
        REQUIRE(root_test.second == super.getRoot());
        //清空手动建的树
        tree.clear_tree();
        REQUIRE(table_two.dataCount() == 0);
        REQUIRE(super.getRoot() == 0);
    }

    SECTION("Bptree::find_leaf")
    {
        //打开表
        Table table_two;
        table_two.open("table_two");
        Bptree tree;
        tree.set_table(&table_two, 0, 1);
        // 读超级块
        SuperBlock super;
        BufDesp *desp = kBuffer.borrow(table_two.name_.c_str(), 0);
        super.attach(desp->buffer);
        desp->relref();
        // 空树搜索
        REQUIRE(table_two.dataCount() == 0);
        REQUIRE(super.getRoot() == 0);
        // TODO:起一个合适的名字
        // TODO:设置合适的key_len
        char empty_test[4];
        struct iovec empty_test_key;
        empty_test_key.iov_base = &empty_test;
        empty_test_key.iov_len = 4;
        std::pair<bool, struct iovec> ret = tree.search(empty_test_key);
        REQUIRE(ret.first == false);
        // 构建一个根节点
        unsigned int root_id = table_two.allocate();
        REQUIRE(table_two.dataCount() == 1);
        super.setRoot(root_id);
        // 读根节点，根节点设置为叶子节点
        Node root_node;
        root_node.setTable(&table_two);
        tree.attach_node(root_node, root_id);
        root_node.set_leaf(true);
        // 给根节点手动插入记录
        // 记录1
        // 此时树的结构为      10
        //         mid_child
        int key = 10;
        unsigned int mid_child = table_two.allocate();
        REQUIRE(table_two.dataCount() == 2);
        std::vector<struct iovec> iov(2);
        key = htobe32(key);
        mid_child = htobe32(mid_child);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &mid_child;
        iov[1].iov_len = sizeof(int);
        root_node.insertRecord(iov);
        mid_child = be32toh(mid_child);
        // 记录2
        // 此时树的结构为      10         20
        //                      mid_child    right_child
        key = 20;
        unsigned int right_child = table_two.allocate();
        REQUIRE(table_two.dataCount() == 3);
        key = htobe32(key);
        right_child = htobe32(right_child);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &right_child;
        iov[1].iov_len = sizeof(int);
        root_node.insertRecord(iov);
        right_child = be32toh(right_child);
        // 根节点是叶子节点
        struct iovec iov_search;
        iov_search.iov_base = &key;
        iov_search.iov_len = sizeof(int);
        unsigned int node_id = tree.find_leaf(iov_search);
        REQUIRE(node_id == root_id);
        // 将根节点设置为非叶子节点
        root_node.set_leaf(0);
        // 记录3
        // 此时树的结构为
        //                     10               20
        //       left_child(5)      mid_child(3)     right_child(4)
        unsigned int left_child = table_two.allocate();
        root_node.setNext(left_child);
        // 搜索左边，先将left_child设置为叶子节点
        tree.attach_node(root_node, left_child);
        root_node.set_leaf(true);
        key = 5;
        key = htobe32(key);
        iov_search.iov_base = &key;
        iov_search.iov_len = sizeof(int);
        node_id = tree.find_leaf(iov_search);
        REQUIRE(node_id == left_child);
        int track = tree.track.top();
        REQUIRE(track == root_id);
        REQUIRE(tree.track.size() == 1);
        tree.track.pop();
        // 搜索中间，把中间节点设为叶子节点
        tree.attach_node(root_node, mid_child);
        root_node.set_leaf(true);
        key = 15;
        key = htobe32(key);
        iov_search.iov_base = &key;
        iov_search.iov_len = sizeof(int);
        node_id = tree.find_leaf(iov_search);
        REQUIRE(node_id == mid_child);
        track = tree.track.top();
        REQUIRE(track == root_id);
        REQUIRE(tree.track.size() == 1);
        tree.track.pop();
        // 搜索右边，把右节点设为叶子节点
        tree.attach_node(root_node, right_child);
        root_node.set_leaf(true);
        key = 25;
        key = htobe32(key);
        iov_search.iov_base = &key;
        iov_search.iov_len = sizeof(int);
        node_id = tree.find_leaf(iov_search);
        REQUIRE(node_id == right_child);
        track = tree.track.top();
        REQUIRE(track == root_id);
        REQUIRE(tree.track.size() == 1);
        tree.track.pop();
        //搜索分界点20，测试是否跳转正确
        key = 20;
        key = htobe32(key);
        iov_search.iov_base = &key;
        iov_search.iov_len = sizeof(int);
        node_id = tree.find_leaf(iov_search);
        REQUIRE(node_id == right_child);
        track = tree.track.top();
        REQUIRE(track == root_id);
        REQUIRE(tree.track.size() == 1);
        tree.track.pop();
        //搜索分界点10，测试是否跳转正确
        key = 10;
        key = htobe32(key);
        iov_search.iov_base = &key;
        iov_search.iov_len = sizeof(int);
        node_id = tree.find_leaf(iov_search);
        REQUIRE(node_id == mid_child);
        track = tree.track.top();
        REQUIRE(track == root_id);
        REQUIRE(tree.track.size() == 1);
        tree.track.pop();

        tree.clear_tree();
        REQUIRE(table_two.dataCount() == 0);
        REQUIRE(super.getRoot() == 0);
    }

    // SECTION("Bptree::reset_track");
    
    SECTION("Bptree::search")
    {
         //打开表
         Table table_two;
         table_two.open("table_two");
         Bptree tree;
         tree.set_table(&table_two, 0, 1);
         // 读超级块
         SuperBlock super;
         BufDesp *desp = kBuffer.borrow(table_two.name_.c_str(), 0);
         super.attach(desp->buffer);
         desp->relref();

         REQUIRE(table_two.dataCount() == 0);

        // 构建一个根节点
        unsigned int root_id = table_two.allocate();
        REQUIRE(table_two.dataCount() == 1);
        super.setRoot(root_id);
        // 读根节点
        Node root_node;
        root_node.setTable(&table_two);
        tree.attach_node(root_node, root_id);
        // 给根节点手动插入记录
        // 记录1
        // 此时树的结构为      10
        //         mid_child
        int key = 10;
        unsigned int mid_child = table_two.allocate();
        REQUIRE(table_two.dataCount() == 2);
        std::vector<struct iovec> iov(2);
        key = htobe32(key);
        mid_child = htobe32(mid_child);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &mid_child;
        iov[1].iov_len = sizeof(int);
        root_node.insertRecord(iov);
        mid_child = be32toh(mid_child);
        // 记录2
        // 此时树的结构为      10         20
        //                      mid_child    right_child
        key = 20;
        unsigned int right_child = table_two.allocate();
        REQUIRE(table_two.dataCount() == 3);
        key = htobe32(key);
        right_child = htobe32(right_child);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &right_child;
        iov[1].iov_len = sizeof(int);
        root_node.insertRecord(iov);
        right_child = be32toh(right_child);
        // 记录3
        // 此时树的结构为
        //                     10               20
        //       left_child(5)      mid_child(3)     right_child(4)
        unsigned int left_child = table_two.allocate();
        root_node.setNext(left_child);
        // 将left_child设置为叶子节点，并插入数据
        Node left_node;
        tree.attach_node(left_node, left_child);
        left_node.set_leaf(true);
        key = 3;
        int value = 100;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        left_node.insertRecord(iov);
        // 插入数据2
        key = 5;
        value = 200;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        left_node.insertRecord(iov);
        // 插入数据3
        key = 7;
        value = 300;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        left_node.insertRecord(iov);
        // 此时left_node: 6,7,8
        // 将mid_child设置为叶子节点，并插入数据
        Node mid_node;
        tree.attach_node(mid_node, mid_child);
        mid_node.set_leaf(true);
        key = 13;
        value = 400;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        mid_node.insertRecord(iov);
        // 插入数据2
        key = 15;
        value = 500;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        mid_node.insertRecord(iov);
        // 插入数据3
        key = 17;
        value = 600;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        mid_node.insertRecord(iov);
        // 此时mid_node: 13,15,17
        // 将right_child设置为叶子节点，并插入数据
        Node right_node;
        tree.attach_node(right_node, right_child);
        right_node.set_leaf(true);
        key = 23;
        value = 700;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        right_node.insertRecord(iov);
        // 插入数据2
        key = 25;
        value = 800;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        right_node.insertRecord(iov);
        // 插入数据3
        key = 27;
        value = 900;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        right_node.insertRecord(iov);
        // 此时right_node: 23,25,27
        // 此时树的结构为
        //                                     10                             20
        //             3(100)  5(200)  7(300)      13(400)  15(500)  17(600)      23(700)  25(800)  27(900)
        //                  (left_child)                  (mid_child)                   (right_child)
        // 开始进行搜索函数测试
        // 搜索left_child:5
        struct iovec search_key;
        key = 3;
        key = htobe32(key);
        search_key.iov_base = &key;
        search_key.iov_len = sizeof(int);
        std::pair<bool, struct iovec> search_result = tree.search(search_key);
        REQUIRE(search_result.first == true);
        value = 100;
        value = htobe32(value);
        REQUIRE(memcmp((void*)&search_result.second.iov_base, (void*)&value, search_result.second.iov_len));
        // 搜索mid_child:15
        key = 15;
        key = htobe32(key);
        search_key.iov_base = &key;
        search_key.iov_len = sizeof(int);
        search_result = tree.search(search_key);
        REQUIRE(search_result.first == true);
        value = 500;
        value = htobe32(value);
        REQUIRE(memcmp((void*)&search_result.second.iov_base, (void*)&value, search_result.second.iov_len));
        // 搜索right_child:27
        key = 27;
        key = htobe32(key);
        search_key.iov_base = &key;
        search_key.iov_len = sizeof(int);
        search_result = tree.search(search_key);
        REQUIRE(search_result.first == true);
        value = 900;
        value = htobe32(value);
        REQUIRE(memcmp((void*)&search_result.second.iov_base, (void*)&value, search_result.second.iov_len));
        // 搜索失败测试
        key = 1;
        key = htobe32(key);
        search_key.iov_base = &key;
        search_key.iov_len = sizeof(int);
        search_result = tree.search(search_key);
        REQUIRE(search_result.first == false);
        value = 100;
        value = htobe32(value);
        REQUIRE(memcmp((void*)&search_result.second.iov_base, (void*)&value, search_result.second.iov_len) == 0);
        
        key = 30;
        key = htobe32(key);
        search_key.iov_base = &key;
        search_key.iov_len = sizeof(int);
        search_result = tree.search(search_key);
        REQUIRE(search_result.first == false);
        value = 1000;
        value = htobe32(value);
        REQUIRE(memcmp((void*)&search_result.second.iov_base, (void*)&value, search_result.second.iov_len) == 0);
        
        //清空手动建的树
        tree.clear_tree();
        REQUIRE(table_two.dataCount() == 0);
        REQUIRE(super.getRoot() == 0);
    }

    SECTION("Bptree::insert")
    {
        //打开表
        Table table_two;
        table_two.open("table_two");
        Bptree tree;
        tree.set_table(&table_two, 0, 1);
        // 读超级块
        SuperBlock super;
        BufDesp *desp = kBuffer.borrow(table_two.name_.c_str(), 0);
        super.attach(desp->buffer);
        desp->relref();
        // 空树搜索
        REQUIRE(table_two.dataCount() == 0);
        REQUIRE(super.getRoot() == 0);
        
        // 完成初始化，准备开始测试insert
        super.setOrder(4);
        // 从空树开始插入
        // 插入后数据形状：  10
        int key = 10;
        int value = 10;
        std::vector<struct iovec> iov(2);
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        bool success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        // 通过search判断插入是否成功
        std::pair<bool, struct iovec> search_result = tree.search(iov[0]);
        REQUIRE(search_result.first == true);
        REQUIRE(memcmp((void*)&search_result.second.iov_base, (void*)&value, search_result.second.iov_len));
        REQUIRE(super.getRoot() != 0);     
        
        // 继续插入数据
        // 插入后数据形状：  10  20
        key = 20;
        value = 20;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        // 通过search判断插入是否成功
        search_result = tree.search(iov[0]);
        REQUIRE(search_result.first == true);
        REQUIRE(memcmp((void*)&search_result.second.iov_base, (void*)&value, search_result.second.iov_len));
        
        // 继续插入数据
        // 插入后数据形状：  10  20  30
        key = 30;
        value = 30;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        // 通过search判断插入是否成功
        search_result = tree.search(iov[0]);
        REQUIRE(search_result.first == true);
        REQUIRE(memcmp((void*)&search_result.second.iov_base, (void*)&value, search_result.second.iov_len));

        // 插入第四个数据后，节点分裂
        // 数据的形状：         30
        //               10 20      30 40
        key = 40;
        value = 40;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        // 通过search判断插入是否成功
        search_result = tree.search(iov[0]);
        REQUIRE(search_result.first == true);
        REQUIRE(memcmp((void*)&search_result.second.iov_base, (void*)&value, search_result.second.iov_len));
        
        // 插入重复数据10，测试无法插入的情况
        key = 10;
        value = 10;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == false);
        
        // TODO:弄清这个顶层Search功能是什么，为什么要这么测试，对代码进行修改
        // 插入新数据，用于测试顶层Search功能(老师的代码这么说)
        // 插入第五个数据
        // 数据的形状：         30
        //               10 20      30 40 50
        key = 50;
        value = 50;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        // 通过search判断插入是否成功
        search_result = tree.search(iov[0]);
        REQUIRE(search_result.first == true);
        REQUIRE(memcmp((void*)&search_result.second.iov_base, (void*)&value, search_result.second.iov_len));

        // 插入第六个数据，节点分裂
        // 数据的形状：          30        50
        //               10 20      30 40      50  60
        key = 60;
        value = 60;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        // 通过search判断插入是否成功
        search_result = tree.search(iov[0]);
        REQUIRE(search_result.first == true);
        REQUIRE(memcmp((void*)&search_result.second.iov_base, (void*)&value, search_result.second.iov_len));

        // 插入第七个数据
        // 数据的形状：          30        50
        //               10 20      30 40      50  60  70
        key = 70;
        value = 70;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        // 通过search判断插入是否成功
        search_result = tree.search(iov[0]);
        REQUIRE(search_result.first == true);
        REQUIRE(memcmp((void*)&search_result.second.iov_base, (void*)&value, search_result.second.iov_len));

        // 插入第八个数据，节点分裂
        // 数据的形状：          30          50          70
        //                10 20      30 40      50  60      70  80
        key = 80;
        value = 80;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        // 通过search判断插入是否成功
        search_result = tree.search(iov[0]);
        REQUIRE(search_result.first == true);
        REQUIRE(memcmp((void*)&search_result.second.iov_base, (void*)&value, search_result.second.iov_len));

        // 插入第九个数据
        // 数据的形状：          30          50          70
        //                10 20      30 40      50  60      70  80  90
        key = 90;
        value = 90;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        // 通过search判断插入是否成功
        search_result = tree.search(iov[0]);
        REQUIRE(search_result.first == true);
        REQUIRE(memcmp((void*)&search_result.second.iov_base, (void*)&value, search_result.second.iov_len));

        // 插入第十个数据，节点分裂(注意：此处两个节点连续分裂)
        // 数据的形状：                        70
        //                      30       50                  90
        //                10  20   30  40   50  60    70  80     90  100
        key = 100;
        value = 100;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        // 通过search判断插入是否成功
        search_result = tree.search(iov[0]);
        REQUIRE(search_result.first == true);
        REQUIRE(memcmp((void*)&search_result.second.iov_base, (void*)&value, search_result.second.iov_len));

        // 测试顶层Search功能
        struct iovec unique_key;
        key = 50;
        key = htobe32(key);
        unique_key.iov_base = &key;
        unique_key.iov_len = sizeof(int);
        search_result = tree.search(unique_key);
        REQUIRE(search_result.first == true);
        REQUIRE(memcmp((void*)&search_result.second.iov_base, (void*)&value, search_result.second.iov_len));
        
        // 清空树
        tree.clear_tree();
        super.setRoot(0);
        REQUIRE(table_two.dataCount() == 0);
        REQUIRE(super.getRoot() == 0);
        // 设置树的阶数
        // ATTENTION:老师代码中写的是200，不知道是否有什么特殊意义
        super.setOrder(200);

        //连续插入数据
        //在我们的测试中对于500阶的索引树，datablock_num可达到4000000
        int success_num = 0;
        // ATTENTION:老师代码中插入数据数选择了1000
        int node_num = 1000;
        for (int i = 0; i < node_num; ++i){
            key = htobe32(i);
            value = htobe32(i << 2);
            iov[0].iov_base = &key;
            iov[0].iov_len = sizeof(key);
            iov[1].iov_base = &value;
            iov[1].iov_len = sizeof(int);
            success_insert = tree.insert(iov[0], iov[1]);
            if (success_insert == true) success_num++;
        }
        REQUIRE(super.getRoot() != 0);
        // 清空树
        tree.clear_tree();
        super.setRoot(0);
        REQUIRE(table_two.dataCount() == 0);
        REQUIRE(super.getRoot() == 0);
    }

    SECTION("Bptree::remove")
    {
        //打开表
        Table table_two;
        table_two.open("table_two");
        Bptree tree;
        tree.set_table(&table_two, 0, 1);
        // 读超级块
        SuperBlock super;
        BufDesp* desp = kBuffer.borrow(table_two.name_.c_str(), 0);
        super.attach(desp->buffer);
        desp->relref();
        // 空树搜索
        REQUIRE(table_two.dataCount() == 0);
        REQUIRE(super.getRoot() == 0);
        // 插入数据构造
        // 具体的remove测试从932行开始
        super.setOrder(5);
        // 插入数据
        int key = 10;
        int value = 10;
        std::vector<struct iovec> iov(2);
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        bool success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        key = 20;
        value = 20;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        key = 30;
        value = 30;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        key = 40;
        value = 40;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        key = 50;
        value = 50;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        key = 60;
        value = 60;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        key = 70;
        value = 70;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        key = 80;
        value = 80;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        key = 90;
        value = 90;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        key = 100;
        value = 100;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        key = 110;
        value = 110;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        // 数据的形状：                            [70]
        //                         [30       50]                    [90]
        //                [10  20]   [30  40]   [50  60]    [70  80]     [90  100  110]
        // 开始remove的测试
        // 第一种情况：要删除的数据不在Bptree中
        key = 120;
        key = htobe32(key);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        bool remove_result = tree.remove(iov[0]);
        REQUIRE(remove_result == false);
        // 第二种情况：要删除的数据所在node数据量足够，可以直接删
        key = 110;
        key = htobe32(key);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        remove_result = tree.remove(iov[0]);
        REQUIRE(remove_result == true);
        // 用search检验是否成功remove
        std::pair<bool, iovec> search_result = tree.search(iov[0]);
        REQUIRE(search_result.first == false);
        // 将110插入回去
        key = 110;
        value = 110;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        // 第三种情况：要删除的数据所在node数据量不足够，向右兄弟借
        key = 80;
        key = htobe32(key);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        remove_result = tree.remove(iov[0]);
        REQUIRE(remove_result == true);
        // 用search检验是否成功remove
        search_result = tree.search(iov[0]);
        REQUIRE(search_result.first == false);
        // 数据的形状变为：                         [70]
        //                         [30       50]                  [100]
        //                [10  20]   [30  40]   [50  60]   [70  90]    [100  110]
        // 改变数据形状
        key = 75;
        value = 75;
        key = htobe32(key);
        value = htobe32(value);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = &value;
        iov[1].iov_len = sizeof(int);
        success_insert = tree.insert(iov[0], iov[1]);
        REQUIRE(success_insert == true);
        // 数据的形状：                            70
        //                       [30       50]                  [90]
        //                [10  20]   [30  40]   [50  60]    [70  75  80]     [90  100]
        // 第四种情况：要删除的数据所在node数据量不足够，向左兄弟借
        key = 100;
        key = htobe32(key);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        remove_result = tree.remove(iov[0]);
        REQUIRE(remove_result == true);
        // 用search检验是否成功remove
        search_result = tree.search(iov[0]);
        REQUIRE(search_result.first == false);
        // 数据的形状变为：                         [70]
        //                         [30       50]                   [80]
        //                [10  20]   [30  40]   [50  60]   [70  75]    [80  90]
        // 第五种情况：要删除的数据所在node数据量不足够，同时左右兄弟均无法借，则触发merge
        key = 30;
        key = htobe32(key);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        remove_result = tree.remove(iov[0]);
        REQUIRE(remove_result == true);
        // 用search检验是否成功remove
        search_result = tree.search(iov[0]);
        REQUIRE(search_result.first == false);
        // 第六种情况：说不清楚
        key = 70;
        key = htobe32(key);
        iov[0].iov_base = &key;
        iov[0].iov_len = sizeof(int);
        remove_result = tree.remove(iov[0]);
        REQUIRE(remove_result == true);
        // 用search检验是否成功remove
        search_result = tree.search(iov[0]);
        REQUIRE(search_result.first == false);
    }
}