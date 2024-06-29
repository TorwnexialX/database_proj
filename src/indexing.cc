#include "db/block.h"
#include "db/table.h"
#include "db/buffer.h"
#include "db/indexing.h"

namespace db{
/* key, value均各对应一个 struct iovec */
// 根据给定key在bptree上查找，返回（是否成功，对应value）
std::pair<bool, struct iovec> Bptree::search(struct iovec key) {
    std::pair<bool, unsigned int> ret = get_root();
    if (!ret.first) return {false, {nullptr, 0}};
    unsigned int leaf_id = find_leaf(key);
    Record record;
    Node leaf_node;
    attach_node(leaf_node, leaf_id);
    unsigned int index = leaf_node.searchRecord(key.iov_base, key.iov_len);
    bool if_same = leaf_node.same_key(key, index);
    // 没有查询到key所对应的value
    if (if_same == false) return {false, {nullptr, 0}};
    // 查询到了key所对应的value
    leaf_node.refslots(index, record);
    unsigned int value;
    unsigned int value_len;
    record.getByIndex((char *) &value, &value_len, VALUE_INDEX);
    return {true, {(void *)&value, value_len}};
}

unsigned int Bptree::find_leaf(struct iovec key){
    std::pair<bool, unsigned int> ret = get_root();
    unsigned int root = ret.second;
    Node cur_node;
    attach_node(cur_node, root);

    unsigned int child;
    unsigned int child_len;

    while(!cur_node.is_leaf()){
        track.push(cur_node.getSelf());
        // 'lb' stands for 'lowerbound'
        unsigned int lb_index = cur_node.searchRecord(key.iov_base, key.iov_len);
        bool if_same = cur_node.same_key(key, lb_index); 
        
        // lb_index == 0 走left_node
        if (lb_index == 0) {
            child = cur_node.get_left();
            child = be32toh(child);
            attach_node(cur_node, child);
        }

        // 如果给定的key与lb_key不同说明该key应在前面的record中
        lb_index -= if_same ? 0 : 1;

        Record lb_record;
        refslots(lb_index, lb_record);
        lb_record.getByIndex((char *)&child, &child_len, VALUE_INDEX);
        // TODO: key和lb_key的长度，keylen, keylen_lb应该是一样的，但现在有着不同的类型和不同的名词名称
        child = be32toh(child);
        attach_node(cur_node, child);
    }

    return child;
}

bool Node::same_key(struct iovec key, unsigned int index){
    Record record;
    if (index >= getSlots()) return false;

    refslots(index, record);
    unsigned char *pkey;
    unsigned int plen;
    record.refByIndex(&pkey, &plen, KEY_INDEX);

    if (memcmp(pkey, key.iov_base, len) == 0) return true;
    else return false;
}

bool Bptree::insert(struct iovec key, struct iovec value){
    // 读取超级块
    SuperBlock superblock;
    BufDesp *desp = kBuffer.borrow(table_->name_.c_str(), 0);
    superblock.attach(desp->buffer);
    desp->relref();

    // 包装成iov
    void *dup_key = new char[key.iov_len];
    memcpy(dup_key, key.iov_base, key.iov_len);
    void *dup_value = new char[value.iov_len];
    memcpy(dup_value, value.iov_base, value.iov_len);
    std::vector<struct iovec> iov(2);
    // 不确定iov_base需不需要转成(unsigned int *)
    iov[0].iov_base = dup_key;
    iov[0].iov_len  = key.iov_len;
    iov[1].iov_base = dup_value;
    iov[1].iov_len  = value.iov_len;

    // B+树为空树
    if (superblock.getNodecounts() == 0) {
        unsigned int newroot = table_->allocate(1);
        superblock.setRoot(newroot);
        Node cur_node;
        cur_node.setTable(table_);
        attach_node(cur_node, newroot);
        cur_node.set_leaf(true);
        cur_node.insertRecord(iov);
        return true;
    }

    // B+树非空
    // 栈，是否需要？？？？？？？？？？

}

std::pair<bool, unsigned int> Bptree::get_root(){
    //读取超级块
    SuperBlock superblock;
    BufDesp *desp = kBuffer.borrow(table_->name_.c_str(), 0);
    superblock.attach(desp->buffer);
    desp->relref();

    // 当前树为空树
    if (superblock.getNodecounts() == 0) return {false, 0};
    // 当前树非空
    else return {true, superblock.getRoot()};
}

void Bptree::attach_node(Node &node, unsigned int node_id){
    node.detach();
    BufDesp *desp = kBuffer.borrow(table_->name_.c_str(), node_id);
    node.attach(desp->buffer);
}

}