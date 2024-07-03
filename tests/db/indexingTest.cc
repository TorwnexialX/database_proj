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
    section("Node::is_leaf");

    section("Node::set_leaf");

    section("Node::get_left");

    section("Node::set_left");

    section("Node::same_key");

    section("Bptree::set_table");

    section("Bptree::attach_node");

    section("Bptree::node_append");

    section("Bptree::get_root");

    section("Bptree::find_leaf");

    section("Bptree::reset_track");
    
    section("Bptree::search");

    section("Bptree::insert");

    section("Bptree::remove");
}