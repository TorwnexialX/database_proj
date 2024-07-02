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

Node Bptree::node_append(Node *node){
    Node new_node;
    unsigned int new_id = table_.allocate(1);
    attach_node(new_node, new_id);
    new_node.setNext(node->getNext());
    node->setNext(new_node.getSelf());
    return new_node;
}

bool Node::same_key(struct iovec key, unsigned int index){
    Record record;
    if (index >= getSlots()) return false;

    refslots(index, record);
    unsigned char *pkey;
    unsigned int len;
    record.refByIndex(&pkey, &len, KEY_INDEX);

    if (memcmp(pkey, key.iov_base, len) == 0) return true;
    else return false;
}

void Bptree::insert_to_index(struct iovec key, unsigned int new_node_id){
    // 获取超级块
    SuperBlock super;
    BufDesp *desp = kBuffer.borrow(table_->name_.c_str(), 0);
    super.attach(desp->buffer);
    desp->relref();

    // 如果栈不为空
    while(!track.empty()){
        // 获取栈顶元素
        unsigned int node_parent = track.top();
        track.pop();

        // 包装成iov
        new_node_id = htobe32(new_node_id);
        std::vector<struct iovec> iov(2);
        iov[0].iov_base = key.iov_base;
        iov[0].iov_len = key.iov_len;
        iov[1].iov_base = &new_node_id;
        iov[1].iov_len = sizeof(new_node_id);
        new_node_id = be32toh(new_node_id);

        // TODO: 普通分裂
        // TODO: 根节点分裂的特殊情况

        // 将给定键值对插入cur_record
        Node cur_node;
        attach_node(cur_node, node_parent);
        std::pair<bool, unsigned int> insert_result = cur_node.insertRecord(iov);

        // 非根节点分裂

        // 根节点分裂：特殊，设置最左侧子节点域
    }

    // 分裂情况


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
    // 需要栈，插入数据需要不断回溯查看是否存在需要分块的情况
    Node former_node;
    reset_track();
    unsigned int leaf_id = find_leaf(key);
    attach_node(former_node, leaf_id);
    unsigned int lb_index = former_node.searchRecord(key.iov_base, key.iov_len);

    // 如果要插入的记录项已存在
    if same_key(key, lb_index) return false;
    // 待插入记录在node最左边情况的index赋为0则肯定在后续移动过程中归到左边
    unsigned int index = lb_index == 0 ? 0 : lb_index - 1;

    // 如果要插入的记录项不存在
    if (former_node.getSlots() == superblock.getOrder() - 1){
        // 该former_node已经满了，需要分裂
        // 1. 创建新Node
        Node next_node = node_append(&former_node);
        next_node.set_leaf(true);

        // 2. 原node数据分开，一部分放入next_node
        unsigned short mid_record = (former_node.getSlots() + 1) / 2;

        // 将mid_record节点放入next_node节点中，插入的new_record放入former_node中
        if (index < mid_record - 1){
            while(former_node.getSlots() > mid_record - 1){
                Record record; // 待移动的record
                former_node.refslots(mid_record - 1, record);
                next_node.copyRecord(record);
                // ATTENTION: 以上copyRecord在老师代码里用的是单独的IndexBlock::copyRecord
                former_node.deallocate(mid_record - 1);
            }
            former_node.insertRecord(iov);
        }
        else{ // mid_record节点放入former_node中，插入的new_record放入next_node中
            while(former_node.getSlots() > mid_record){
                Record record;
                former_node.refslots(mid_record, record);
                next_node.copyRecord(record);
                former_node.deallocate(mid_record);
            }
            next_node.insertRecord(iov);
        }
        // 3. 将中间的节点加入父节点中
        // 如果bptree只有一个节点（即根节点就是叶子结点）
        if (former_node.getSelf() == superblock.getRoot()){
            Node new_root;
            unsigned int new_root_id = table_.allocate(1);
            attach_node(new_root, new_root_id);
            superblock.setRoot(new_root_id);
            track.push(new_root_id);
            // new root 设置next(最左)
            // new root 插入parent
        }

        /* 向上添加过程为写入 */
        // Record record;
        // next_node.refSlots(KEY_INDEX, record);
        // void *pkey = new char [key.iov_len];
        // unsigned int key_len
        // record.getByIndex((char *) pkey, (unsigned int *) &key.iov_len, 0);

        // insert_to_index(key, next_node.getSelf());
    } 
    else former_node.insertRecord(iov); // 直接插入

    return true;
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