#include "../catch.hpp"
#include <db/block.h>
#include <db/endian.h>
#include <db/record.h>
#include <db/buffer.h>
#include <db/file.h>
#include <db/table.h>
#include <db/indexing.h>

using namespace db;

std::pair<struct iovec, struct iovec> insert_preparation(int key, int value)
{
    struct iovec key_insert;
    struct iovec value_insert;
    key = htobe32(key);
    key_insert.iov_base = &key;
    key_insert.iov_len = sizeof(int);
    value_insert.iov_base = &value;
    value_insert.iov_len = sizeof(int);
    return {key_insert, value_insert};
}

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
        node.set_leaf(true);
        REQUIRE(node.is_leaf() == true);
        // 设置为非叶子节点
        node.set_leaf(false);
        REQUIRE(node.is_leaf() == false);
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
        super.setNodecounts(1);
        REQUIRE(super.getRoot() == root_id);
        // 读根节点
        Node root_node;
        root_node.setTable(&table_two);
        tree.attach_node(root_node, root_id);
        // 插入三个节点
        int key = 10;
        unsigned int mid_child = table_two.allocate();
        REQUIRE(table_two.dataCount() == 2);
        super.setNodecounts(2);
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
        super.setNodecounts(3);
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
        super.setNodecounts(4);
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
        REQUIRE(super.getNodecounts() == 0);
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
        super.setNodecounts(1);
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
        REQUIRE(super.getNodecounts() == 0);
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
        super.setNodecounts(1);
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
        super.setNodecounts(2);
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
        super.setNodecounts(3);
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
        super.setNodecounts(4);
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
        REQUIRE(super.getNodecounts() == 0);
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

         REQUIRE(super.getNodecounts() == 4);

        
        ////清空手动建的树
        //table_two.deallocate(root_id);
        //table_two.deallocate(left_child);
        //table_two.deallocate(mid_child);
        //table_two.deallocate(right_child);
        //super.setRoot(0);
        //REQUIRE(super.getRoot() == 0);
        //REQUIRE(table_two.dataCount() == 0);
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
        struct iovec key_insert;
        struct iovec value_insert;
        
        // 完成初始化，准备开始测试insert
        super.setOrder(5);
        // 从空树开始插入
        int key = 10;
        unsigned int value = rand() % 9999;
        std::pair<struct iovec, struct iovec> content_insert = insert_preparation(key, value);
        key_insert = content_insert.first;
        value_insert = content_insert.second;
        bool success_insert = tree.insert(key_insert, value_insert);
        // 通过success_insert的值判断插入是否成功
        REQUIRE(success_insert == true);
        // 通过research判断插入是否成功
        std::pair<bool, struct iovec> search_result = tree.search(key_insert);
        REQUIRE(search_result.first == true);
        REQUIRE(search_result.second.iov_base == &value);
        // 是否正确设置了根节点
        REQUIRE(super.getRoot() != 0);
        
        // 继续插入数据
        key = 20;
        value = rand() % 9999;
        content_insert = insert_preparation(key, value);
        key_insert = content_insert.first;
        value_insert = content_insert.second;
        success_insert = tree.insert(key_insert, value_insert);
        // 通过success_insert的值判断插入是否成功
        REQUIRE(success_insert == true);
        // 通过research判断插入是否成功
        search_result = tree.search(key_insert);
        REQUIRE(search_result.first == true);
        REQUIRE(search_result.second.iov_base == &value);

        // 继续插入数据
        key = 30;
        value = rand() % 9999;
        content_insert = insert_preparation(key, value);
        key_insert = content_insert.first;
        value_insert = content_insert.second;
        success_insert = tree.insert(key_insert, value_insert);
        // 通过success_insert的值判断插入是否成功
        REQUIRE(success_insert == true);
        // 通过research判断插入是否成功
        search_result = tree.search(key_insert);
        REQUIRE(search_result.first == true);
        REQUIRE(search_result.second.iov_base == &value);

        // 继续插入数据
        key = 40;
        value = rand() % 9999;
        content_insert = insert_preparation(key, value);
        key_insert = content_insert.first;
        value_insert = content_insert.second;
        success_insert = tree.insert(key_insert, value_insert);
        // 通过success_insert的值判断插入是否成功
        REQUIRE(success_insert == true);
        // 通过research判断插入是否成功
        search_result = tree.search(key_insert);
        REQUIRE(search_result.first == true);
        REQUIRE(search_result.second.iov_base == &value);
        
        // 现在我们已经有数据：10,20,30,40
        // 插入重复数据10，测试无法插入的情况
        key = 10;
        value = rand() % 9999;
        content_insert = insert_preparation(key, value);
        key_insert = content_insert.first;
        value_insert = content_insert.second;
        success_insert = tree.insert(key_insert, value_insert);
        REQUIRE(success_insert == false);

        // TODO:弄清这个顶层Search功能是什么，为什么要这么测试，对代码进行修改
        // 插入新数据，用于测试顶层Search功能(老师的代码这么说)
        key = 50;
        unsigned int unique_value = 312;
        content_insert = insert_preparation(key, value);
        key_insert = content_insert.first;
        value_insert = content_insert.second;
        success_insert = tree.insert(key_insert, value_insert);

        // 插入新数据
        // 现在我们已经有数据：10,20,30,40,50
        key = 60;
        value = rand() % 9999;
        content_insert = insert_preparation(key, value);
        key_insert = content_insert.first;
        value_insert = content_insert.second;
        success_insert = tree.insert(key_insert, value_insert);
        // 通过success_insert的值判断插入是否成功
        REQUIRE(success_insert == true);
        // 通过research判断插入是否成功
        search_result = tree.search(key_insert);
        REQUIRE(search_result.first == true);
        REQUIRE(search_result.second.iov_base == &value);

        // 插入新数据
        // 现在我们已经有数据：10,20,30,40,50,60
        key = 70;
        value = rand() % 9999;
        content_insert = insert_preparation(key, value);
        key_insert = content_insert.first;
        value_insert = content_insert.second;
        success_insert = tree.insert(key_insert, value_insert);
        // 通过success_insert的值判断插入是否成功
        REQUIRE(success_insert == true);
        // 通过research判断插入是否成功
        search_result = tree.search(key_insert);
        REQUIRE(search_result.first == true);
        REQUIRE(search_result.second.iov_base == &value);

        // 插入新数据
        // 现在我们已经有数据：10,20,30,40,50,60,70
        key = 15;
        value = rand() % 9999;
        content_insert = insert_preparation(key, value);
        key_insert = content_insert.first;
        value_insert = content_insert.second;
        success_insert = tree.insert(key_insert, value_insert);
        // 通过success_insert的值判断插入是否成功
        REQUIRE(success_insert == true);
        // 通过research判断插入是否成功
        search_result = tree.search(key_insert);
        REQUIRE(search_result.first == true);
        REQUIRE(search_result.second.iov_base == &value);

        // 插入新数据
        // 现在我们已经有数据：10,15,20,30,40,50,60,70
        key = 25;
        value = rand() % 9999;
        content_insert = insert_preparation(key, value);
        key_insert = content_insert.first;
        value_insert = content_insert.second;
        success_insert = tree.insert(key_insert, value_insert);
        // 通过success_insert的值判断插入是否成功
        REQUIRE(success_insert == true);
        // 通过research判断插入是否成功
        search_result = tree.search(key_insert);
        REQUIRE(search_result.first == true);
        REQUIRE(search_result.second.iov_base == &value);

        // 插入新数据
        // 现在我们已经有数据：10,15,20,25,30,40,50,60,70
        key = 35;
        value = rand() % 9999;
        content_insert = insert_preparation(key, value);
        key_insert = content_insert.first;
        value_insert = content_insert.second;
        success_insert = tree.insert(key_insert, value_insert);
        // 通过success_insert的值判断插入是否成功
        REQUIRE(success_insert == true);
        // 通过research判断插入是否成功
        search_result = tree.search(key_insert);
        REQUIRE(search_result.first == true);
        REQUIRE(search_result.second.iov_base == &value);

        // 插入新数据
        // 现在我们已经有数据：10,15,20,25,30,35,40,50,60,70
        key = 45;
        value = rand() % 9999;
        content_insert = insert_preparation(key, value);
        key_insert = content_insert.first;
        value_insert = content_insert.second;
        success_insert = tree.insert(key_insert, value_insert);
        // 通过success_insert的值判断插入是否成功
        REQUIRE(success_insert == true);
        // 通过research判断插入是否成功
        search_result = tree.search(key_insert);
        REQUIRE(search_result.first == true);
        REQUIRE(search_result.second.iov_base == &value);

        // 插入新数据
        // 现在我们已经有数据：10,15,20,25,30,35,40,45,50,60,70
        key = 55;
        value = rand() % 9999;
        content_insert = insert_preparation(key, value);
        key_insert = content_insert.first;
        value_insert = content_insert.second;
        success_insert = tree.insert(key_insert, value_insert);
        // 通过success_insert的值判断插入是否成功
        REQUIRE(success_insert == true);
        // 通过research判断插入是否成功
        search_result = tree.search(key_insert);
        REQUIRE(search_result.first == true);
        REQUIRE(search_result.second.iov_base == &value);

        // 插入新数据
        // 现在我们已经有数据：10,15,20,25,30,35,40,45,50,55,60,70
        key = 65;
        value = rand() % 9999;
        content_insert = insert_preparation(key, value);
        key_insert = content_insert.first;
        value_insert = content_insert.second;
        success_insert = tree.insert(key_insert, value_insert);
        // 通过success_insert的值判断插入是否成功
        REQUIRE(success_insert == true);
        // 通过research判断插入是否成功
        search_result = tree.search(key_insert);
        REQUIRE(search_result.first == true);
        REQUIRE(search_result.second.iov_base == &value);

        // 插入新数据
        // 现在我们已经有数据：10,15,20,25,30,35,40,45,50,55,60,65,70
        key = 75;
        value = rand() % 9999;
        content_insert = insert_preparation(key, value);
        key_insert = content_insert.first;
        value_insert = content_insert.second;
        success_insert = tree.insert(key_insert, value_insert);
        // 通过success_insert的值判断插入是否成功
        REQUIRE(success_insert == true);
        // 通过research判断插入是否成功
        search_result = tree.search(key_insert);
        REQUIRE(search_result.first == true);
        REQUIRE(search_result.second.iov_base == &value);

        // 测试顶层Search功能
        struct iovec unique_key;
        key = 50;
        key = htobe32(key);
        unique_key.iov_base = &key;
        unique_key.iov_len = sizeof(int);
        search_result = tree.search(key_insert);
        REQUIRE(search_result.second.iov_base == &unique_value);
        system("pause");
        
        // 清空树
        tree.clear_tree();
        super.setRoot(0);
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
            content_insert = insert_preparation(key, value);
            key_insert = content_insert.first;
            value_insert = content_insert.second;
            success_insert = tree.insert(key_insert, value_insert);
            if (success_insert == true) success_num++;
        }
        REQUIRE(super.getRoot() != 0);
    }

    // SECTION("Bptree::remove");
}