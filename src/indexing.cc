#include "db/block.h"
#include "db/table.h"
#include "db/buffer.h"
#include "db/indexing.h"

namespace db{
void Bptree::clear_tree()
{
    // 读取超级块
    SuperBlock superblock;
    BufDesp *desp = kBuffer.borrow(table_->name_.c_str(), 0);
    superblock.attach(desp->buffer);
    desp->relref();

    if (superblock.getNodecounts() == 0) return;

    unsigned int root_id = superblock.getRoot();

    std::queue<unsigned int> find_nodes;
    std::queue<unsigned int> record_nodes;
    find_nodes.push(root_id);
    record_nodes.push(root_id);
    while(!find_nodes.empty()){
        Node node;
        unsigned int cur_node_id = find_nodes.front();
        find_nodes.pop();
        attach_node(node, cur_node_id);
        node.setTable(table_);
        int slots_num_test = node.getSlots();
        for (unsigned int i = 0; i < node.getSlots(); ++i){
            Slot *slot = node.getSlotsPointer() + i;
            Record record;
            record.attach(
                node.buffer_ + be16toh(slot->offset), be16toh(slot->length));
            unsigned char *pkey;
            unsigned int key_len;
            int key;
            record.refByIndex(&pkey, &key_len, KEY_INDEX);
            memcpy(&key, pkey, key_len);
            key = be32toh(key);

            unsigned char *pvalue;
            unsigned int value;
            unsigned int value_len;
            record.refByIndex(&pvalue, &value_len, VALUE_INDEX);
            memcpy(&value, pvalue, value_len);
            value = be32toh(value);

            if(!node.is_leaf()){
                find_nodes.push(value);
                record_nodes.push(value);
            }
        }
        if (!node.is_leaf()){
            unsigned int next_node_id = node.getNext();
            find_nodes.push(next_node_id);
            record_nodes.push(next_node_id);
        }
    }
    while(!record_nodes.empty()){
        table_->deallocate(record_nodes.front());
        record_nodes.pop();
        superblock.setNodecounts(superblock.getNodecounts() - 1);
    }
    superblock.setRoot(0);
}

/* key, value均各对应一个 struct iovec */
// 根据给定key在bptree上查找，返回（是否成功，对应value）
std::pair<bool, struct iovec> Bptree::search(struct iovec key) {
    std::pair<bool, unsigned int> ret = get_root();
    if (!ret.first) return {false, {nullptr, 0}};
    unsigned int leaf_id = find_leaf(key);
    Record record;
    Node leaf_node;
    attach_node(leaf_node, leaf_id);
    // check record
    for (int i = 0; i < leaf_node.getSlots(); ++i) {
        Record test_record;
        leaf_node.refslots(i, test_record);
        unsigned int test_key, test_klen;
        test_record.getByIndex((char*) & test_key, &test_klen, KEY_INDEX);
        unsigned int test_value, test_vlen;
        test_record.getByIndex((char*) & test_value, &test_vlen, VALUE_INDEX);
        Record pause;
    }
    // end
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

    unsigned int child = root;
    unsigned int child_len;

   reset_track();

    while(!cur_node.is_leaf()){
        if (cur_node.getSlots() == 0) return 0; // 理论上不该出现
        track.push(cur_node.getSelf());
        // 'lb' stands for 'lowerbound'
        unsigned int lb_index = cur_node.searchRecord(key.iov_base, key.iov_len);
        bool if_same = cur_node.same_key(key, lb_index); 
        
        // lb_index == 0 走left_node
        if (lb_index == 0 && !if_same) {
            child = cur_node.get_left();
            child = be32toh(child);
            attach_node(cur_node, child);
        }

       // 如果给定的key与lb_key不同说明该key应在前面的record中
       lb_index -= if_same ? 0 : 1;

        Record lb_record;
        int test_slots_num = cur_node.getSlots();
        cur_node.refslots(lb_index, lb_record);
        lb_record.getByIndex((char *)&child, &child_len, VALUE_INDEX);
        // TODO: key和lb_key的长度，keylen, keylen_lb应该是一样的，但现在有着不同的类型和不同的名词名称
        child = be32toh(child);
        attach_node(cur_node, child);
    }

   return child;
}

Node Bptree::node_append(Node *node){
    Node new_node;
    bool is_leaf = node->is_leaf();
    unsigned int new_id = table_->allocate();
    attach_node(new_node, new_id);
    if (is_leaf) {
        new_node.setNext(node->getNext());
        node->setNext(new_node.getSelf());
        new_node.set_leaf(true);
    }
    else new_node.setNext(0);
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

bool Bptree::insert(struct iovec key, struct iovec value){
   // 读取超级块
   SuperBlock superblock;
   BufDesp *desp = kBuffer.borrow(table_->name_.c_str(), 0);
   superblock.attach(desp->buffer);
   desp->relref();

    // 用于存储key-value对
    std::vector<struct iovec> iov(2);
    iov[0] = key;
    iov[1] = value;

    // B+树为空树
    if (superblock.getNodecounts() == 0) {
        unsigned int newroot = table_->allocate();
        superblock.setRoot(newroot);
        Node cur_node;
        cur_node.setTable(table_);
        attach_node(cur_node, newroot);
        cur_node.set_leaf(true);
        cur_node.insertRecord(iov);
        superblock.setNodecounts(superblock.getNodecounts() + 1);
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
   if (former_node.same_key(key, lb_index)) return false;

    unsigned int node_id = leaf_id;
    unsigned int new_value;

   // 每个循环内，对node_id所在node插入key-value
   while(!track.empty()){
       // 插入k-v对
       Node cur_node;
       attach_node(cur_node, node_id);
       iov[0] = key;
       iov[1] = value;
       cur_node.insertRecord(iov);

       // 判断是否分裂
       if (cur_node.getSlots() < superblock.getOrder() - 1) return true;

        // 执行分裂操作
        // 1. 创建新node
        Node next_node = node_append(&cur_node);
        // 2. 将cur_node数据分开，其中(order + 1) / 2条record放入next_node
        unsigned short mid_record = cur_node.getSlots() / 2;
        while(cur_node.getSlots() > (superblock.getOrder() - 1) / 2){
            Record record;
            cur_node.refslots(mid_record, record);
            next_node.copyRecord(record);
            cur_node.deallocate(mid_record);
        }
        
        // 获取next_node的首record的key-value
        Record head_record;
        next_node.refslots(0, head_record); // 不确定ref还是copy
        unsigned int head_key, key_len;
        head_record.getByIndex((char *)&head_key, &key_len, 0);

       // 更新key
       key.iov_base = &head_key;
       key.iov_len = sizeof(head_key);

        // 更新value
        new_value = next_node.getSelf();
        new_value= htobe32(new_value);
        value.iov_base = &new_value;
        value.iov_len = sizeof(new_value);

       // 更新 node_id
       node_id = track.top();
       track.pop();
   }

   // 根节点分裂（此时, node_id == super.get_root()）

   // 插入k-v对
   Node cur_node;
   unsigned int cur_node_id = node_id;
   attach_node(cur_node, cur_node_id);
   iov[0] = key;
   iov[1] = value;
   cur_node.insertRecord(iov);

   // 判断是否分裂
   if (cur_node.getSlots() < superblock.getOrder() - 1) return true;

   // 执行分裂操作
   // 1. 创建新node
   Node next_node = node_append(&cur_node, cur_node.is_leaf());
   // 2. 将cur_node数据分开，一部分放入next_node
   // 具体来说，假设最大record数为n，则后(n + 1) / 2条record放入next_node
   unsigned short mid_record = cur_node.getSlots() / 2;
   while(cur_node.getSlots() > (superblock.getOrder() - 1) / 2){
       Record record;
       cur_node.refslots(mid_record, record);
       next_node.copyRecord(record);
       cur_node.deallocate(mid_record);
   }

   // 获取next_node的首record的key-value
   Record head_record;
   next_node.refslots(0, head_record); // 不确定ref还是copy
   unsigned int head_key, key_len;
   head_record.getByIndex((char *)&head_key, &key_len, 0);

   // 获取next_node的id
   unsigned int next_node_id = next_node.getSelf();
   next_node_id = htobe32(next_node_id);

    // 创建新的根节点
    unsigned int new_root_id = table_->allocate();
    superblock.setRoot(new_root_id);
    Node new_root;
    attach_node(new_root, new_root_id);

   // 在新的根节点中加入新的record
   iov[0].iov_base = &head_key;
   iov[0].iov_len = sizeof(head_key);
   iov[1].iov_base = &next_node_id;
   iov[1].iov_len = sizeof(next_node_id);
   new_root.insertRecord(iov);

   // 更新根节点的左孩子域
   unsigned int left_child_id = cur_node.getSelf();
   new_root.setNext(left_child_id);

   reset_track();

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
    node.setTable(table_);
}

}