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
    SECTION("Node::is_leaf");

    SECTION("Node::set_leaf");

    SECTION("Node::get_left");

    SECTION("Node::set_left");

    SECTION("Node::same_key");

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

        // 打开新table并绑定到Bptree上
        Table table_two;
        table.open("table_two");

        Bptree tree;
        tree.set_table(table_two, 0, 1);
        REQUIRE(tree.table_ == table_two);
        // TODO:判断key_type与value_type
        // REQUIRE(tree.key_type == );
        // REQUIRE(tree.value_type == );
    }

    SECTION("Bptree::attach_node");

    SECTION("Bptree::node_append");

    SECTION("Bptree::get_root");

    SECTION("Bptree::find_leaf");

    SECTION("Bptree::reset_track");
    
    SECTION("Bptree::search")
    {
        // 不明白的点：还需不需要重新弄table，绑定到tree上
        // 读超级块
        SuperBlock super;
        BufDesp *desp = kBuffer.borrow(table_two.name_.c_str(), 0);
        super.attach(desp->buffer);
        desp->relref();
        // 空树搜索
        REQUIRE(table_two.indexCount() == 0);
        REQUIRE(super.getIndexroot() == 0);
        // TODO:起一个合适的名字
        // TODO:设置合适的key_len
        char empty_test[4];
        struct iovec empty_test_key;
        empty_test_key.iov_base = &empty_test;
        empty_test_key.iov_len = 4;
        std::pair<bool, unsigned int> ret = tree.search(empty_test_key);
        REQUIRE(ret.first == false);
        // 构建一个根节点
        unsigned int root_id = table_two.allocate();
        REQUIRE(table_two.indexCount() == 1);
        super.setIndexroot(root_id);
        // 读根节点，根节点设置为叶子节点
        Node root_node;
        root_node.setTable(&table_two);
        attach_node(root_node, root_id);
        root_node.set_leaf(1);
        // 给根节点手动插入记录
        // 记录1
        // 此时树的结构为      10
        //         mid_child
        int key = 10;
        unsigned int mid_child = table_two.allocate();
        REQUIRE(table_two.indexCount() == 2);
        std::vector<struct iovec> iov(2);
        key = htobe32(key);
        mid_child = htobe32(mid_child);
        iov[0].iov_base = &key;
        iov[0].iov_len = 4;
        iov[1].iov_base = &mid_child;
        iov[1].iov_len = 4;
        root_node.insertRecord(iov);
        mid_child = be32toh(mid_child);
        // 记录2
        // 此时树的结构为      10         20
        //                      mid_child    right_child
        key = 20;
        unsigned int right_child = table_two.allocate();
        REQUIRE(table_two.indexCount() == 3);
        key = htobe32(key);
        right_child = htobe32(right_child);
        iov[0].iov_base = &key;
        iov[0].iov_len = 4;
        iov[1].iov_base = &right_child;
        iov[1].iov_len = 4;
        root_node.insertRecord(iov);
        right_child = be32toh(right_child);
        // 根节点是叶子节点
        ret = tree.search(iov);
        REQUIRE(ret.first == true);
        REQUIRE(ret.second == root_id);
        REQUIRE(tree.route.empty());
        // 将根节点设置为非叶子节点
        root_node.set_leaf(0);
        // 记录3
        // 此时树的结构为      10           20
        //       left_child      mid_child     right_child
        unsigned int left_child = table_two.allocate();
        root_node.setNext(left_child);
        // 搜索左边，先将left_child设置为叶子节点
        attach_node(root_node, left_child);
        root_node.set_leaf(1);
        key = 5;
        key = htobe32(key);
        struct iovec iov_search;
        iov_search.iov_base = &key;
        iov_search.iov_len = sizeof(int);
        ret = tree.index_search(iov_search);
        REQUIRE(ret.first == true);
        REQUIRE(ret.second == left_child);
        int track = btree.route.top();
        REQUIRE(track == root_id);
        REQUIRE(tree.track.size() == 1);
        tree.track.pop();
        // 搜索中间，把中间节点设为叶子节点
        attach_node(root_node, mid_child);
        root_node.set_leaf(1);
        key = 15;
        key = htobe32(key);
        iov_search.iov_base = &key;
        iov_search.iov_len = sizeof(int);
        ret = tree.index_search(iov_search);
        REQUIRE(ret.first == true);
        REQUIRE(ret.second == mid_child);
        track = btree.track.top();
        REQUIRE(track == root_id);
        REQUIRE(tree.track.size() == 1);
        tree.track.pop();
        // 搜索右边，把右节点设为叶子节点
        attach_node(root_node, right_child);
        root_node.set_leaf(1);
        key = 25;
        key = htobe32(key);
        iov_search.iov_base = &key;
        iov_search.iov_len = sizeof(int);
        ret = tree.index_search(iov_search);
        REQUIRE(ret.first == true);
        REQUIRE(ret.second == right_child);
        track = tree.track.top();
        REQUIRE(track == root_id);
        REQUIRE(tree.track.size() == 1);
        tree.track.pop();
        //搜索分界点20，测试是否跳转正确
        key = 20;
        key = htobe32(key);
        iov_search.iov_base = &key;
        iov_search.iov_len = sizeof(int);
        ret = tree.index_search(iov_search);
        REQUIRE(ret.first == true);
        REQUIRE(ret.second == right_child);
        track = tree.track.top();
        REQUIRE(track == root_id);
        REQUIRE(tree.track.size() == 1);
        tree.track.pop();
        //搜索分界点10，测试是否跳转正确
        key = 10;
        key = htobe32(key);
        iov_search.iov_base = &key;
        iov_search.iov_len = sizeof(int);
        ret = tree.index_search(iov_search);
        REQUIRE(ret.first == true);
        REQUIRE(ret.second == mid_child);
        track = tree.track.top();
        REQUIRE(track == root_id);
        REQUIRE(tree.track.size() == 1);
        tree.track.pop();
        //清空手动建的树
        table_two.deallocate(root_id);
        table_two.deallocate(left_child);
        table_two.deallocate(mid_child);
        table_two.deallocate(right_child);
        super.setIndexroot(0);
        REQUIRE(super.getIndexroot() == 0);
        REQUIRE(table_two.indexCount() == 0);
    }

    SECTION("Bptree::insert");

    SECTION("Bptree::remove");
}