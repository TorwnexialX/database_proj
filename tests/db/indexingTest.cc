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

    // SECTION("Bptree::set_table");

    // SECTION("Bptree::attach_node");

    // SECTION("Bptree::node_append");

    // SECTION("Bptree::get_root");

    // SECTION("Bptree::find_leaf");

    // SECTION("Bptree::reset_track");
    
    // SECTION("Bptree::search");

    // SECTION("Bptree::insert");

    // SECTION("Bptree::remove");
}