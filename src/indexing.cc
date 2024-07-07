#include <iostream>
#include <queue>
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

    if (table_->dataCount() == 0) return;

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
        // check record
        for (int i = 0; i < cur_node.getSlots(); ++i) {
            Record test_record;
            cur_node.refslots(i, test_record);
            unsigned int test_key, test_klen;
            test_record.getByIndex((char*)&test_key, &test_klen, KEY_INDEX);
            unsigned int test_value, test_vlen;
            test_record.getByIndex((char*)&test_value, &test_vlen, VALUE_INDEX);
            Record pause;
        }
        // end
        track.push(cur_node.getSelf());
        // 'lb' stands for 'lowerbound'
        unsigned int lb_index = cur_node.searchRecord(key.iov_base, key.iov_len);
        bool if_same = cur_node.same_key(key, lb_index);

        // lb_index == 0 走left_node
        if (lb_index == 0 && !if_same) {
            child = cur_node.getNext();
            child = be32toh(child);
            attach_node(cur_node, child);
        }

        // 如果给定的key与lb_key不同说明该key应在前面的record中
        lb_index -= if_same ? 0 : 1;

        Record lb_record;
        int test_slots_num = cur_node.getSlots();
        cur_node.refslots(lb_index, lb_record);
        lb_record.getByIndex((char *)&child, &child_len, VALUE_INDEX);
        child = be32toh(child);
        attach_node(cur_node, child);
    }
   return child;
}

Node Bptree::node_append(Node *node){
    Node new_node;
    short is_leaf = node->is_leaf();
    unsigned int new_id = table_->allocate();
    attach_node(new_node, new_id);
    if (is_leaf) {
        new_node.setNext(node->getNext());
        node->setNext(new_node.getSelf());
        new_node.set_leaf(1);
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
    if (superblock.getDataCounts() == 0) {
        unsigned int newroot = table_->allocate();
        superblock.setRoot(newroot);
        Node cur_node;
        cur_node.setTable(table_);
        attach_node(cur_node, newroot);
        cur_node.set_leaf(1);
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
       if (cur_node.getSlots() < superblock.getOrder()) return true;

        // 执行分裂操作
        // 1. 创建新node
        Node next_node = node_append(&cur_node);
        // 2. 将cur_node数据分开，其中(order + 1) / 2条record放入next_node
        unsigned short mid_record = cur_node.getSlots() / 2;
        while(cur_node.getSlots() > superblock.getOrder() / 2){
            Record record;
            cur_node.refslots(mid_record, record);
            next_node.copyRecord(record);
            cur_node.deallocate(mid_record);
        }

        // 对于非叶子节点，要删除next_node的第一个冗余record
        if (!cur_node.is_leaf()){
            // 将第一个冗余record中的value信息提取出来
            Record redundant_head;
            unsigned int new_left, new_left_len;
            next_node.refslots(0, redundant_head);
            redundant_head.getByIndex((char*) &new_left, &new_left_len, VALUE_INDEX);

            // 将该信息设置为next_node的next域
            new_left = be32toh(new_left);
            next_node.setNext(new_left);

            // 删除第一个冗余record
            next_node.deallocate(0);
        }

        // 获取next_node的首record的key-value
        Record head_record;
        next_node.refslots(0, head_record);
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
   if (cur_node.getSlots() < superblock.getOrder()) return true;

   // 执行分裂操作
   // 1. 创建新node
   Node next_node = node_append(&cur_node);
   // 2. 将cur_node数据分开，一部分放入next_node
   // 具体来说，假设最大record数为n，则后(n + 1) / 2条record放入next_node
   unsigned short mid_record = cur_node.getSlots() / 2;
   while(cur_node.getSlots() > superblock.getOrder() / 2){
       Record record;
       cur_node.refslots(mid_record, record);
       next_node.copyRecord(record);
       cur_node.deallocate(mid_record);
   }

   if (!cur_node.is_leaf()) {
       // 将第一个冗余record中的value信息提取出来
       Record redundant_head;
       unsigned int new_left, new_left_len;
       next_node.refslots(0, redundant_head);
       redundant_head.getByIndex((char*)&new_left, &new_left_len, VALUE_INDEX);

       // 将该信息设置为next_node的next域
       new_left = be32toh(new_left);
       next_node.setNext(new_left);

       // 删除第一个冗余record
       next_node.deallocate(0);
   }

   // 获取next_node的首record的key-value
   Record head_record;
   next_node.refslots(0, head_record);
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
   if (superblock.getDataCounts() == 0) return {false, 0};
   // 当前树非空
   else return {true, superblock.getRoot()};
}

void Bptree::attach_node(Node &node, unsigned int node_id){
    node.detach();
    BufDesp *desp = kBuffer.borrow(table_->name_.c_str(), node_id);
    node.attach(desp->buffer);
    node.setTable(table_);
}

bool Bptree::remove(struct iovec key){
    // 读取超级块
    SuperBlock superblock;
    BufDesp *desp = kBuffer.borrow(table_->name_.c_str(), 0);
    superblock.attach(desp->buffer);
    desp->relref();

    // B+树为空树
    if (superblock.getDataCounts() == 0) return false;

    // B+树非空
    Node leaf_node;
    reset_track();
    unsigned int leaf_id = find_leaf(key);
    attach_node(leaf_node, leaf_id);
    unsigned int lb_index = leaf_node.searchRecord(key.iov_base, key.iov_len);

    // 如果要删除的记录项不存在
    if (!leaf_node.same_key(key, lb_index)) return false;

    // 要删除的项在该叶节点中，直接删除其中对应record
    unsigned &leaf_index = lb_index;
    leaf_node.deallocate(leaf_index);

    // 当前叶节点即根节点，说明树只有一个节点，则删除工作到此结束
    if (leaf_node.getSelf() == superblock.getRoot()) return true;

   // 每个record中最小的项数
   unsigned int min_keys = superblock.getOrder() / 2;

    Node cur_node = leaf_node;
    while (cur_node.getSlots() < min_keys) {
        // 到达根节点则不再向上迭代
        if (cur_node.getSelf() == superblock.getRoot()) break;

        std::pair<bool, unsigned int> borrow_result;
        bool &stop = borrow_result.first;
        unsigned int& sib_id = borrow_result.second;
        // 借左兄弟的项
        borrow_result = borrow_lsib(cur_node, key);
        unsigned int left_right = (sib_id != 0) ? 0 : 1;
        if (stop) return true;
        // 借右兄弟的项
        borrow_result = borrow_rsib(cur_node, key);
        if (stop) return true;
        // 需要合并
        if (left_right == 0) {
            Node lsib;
            attach_node(lsib, sib_id);
            std::pair<struct iovec, unsigned int> merge_result;
            merge_result = merge(lsib, cur_node);
            key = merge_result.first;
            unsigned int parent_id = merge_result.second;
            attach_node(cur_node, parent_id);
        }
        else if (left_right == 1) {
            Node rsib;
            attach_node(rsib, sib_id);
            std::pair<struct iovec, unsigned int> merge_result;
            merge_result = merge(cur_node, rsib);
            key = merge_result.first;
            unsigned int parent_id = merge_result.second;
            attach_node(cur_node, parent_id);
        }
        track.pop();

        min_keys = (superblock.getOrder() - 1) / 2;
    }

    return true;
}

std::pair<bool, unsigned int>
Bptree::borrow_lsib(Node &current_node, struct iovec key) {
   // 获取父节点
   unsigned int parent_id = track.top();
   Node parent_node;
   attach_node(parent_node, parent_id);

   // 查找当前节点在父节点中的索引
   unsigned int current_index = parent_node.searchRecord(key.iov_base, key.iov_len);
   bool if_same = parent_node.same_key(key, current_index);
   unsigned int lsib_id = 0;
   // 若节点为同Parent的最左节点，则没有左兄弟
   if (current_index == 0 && !if_same) return {false, 0};
   // 若节点为同parent的第二左节点，则左兄弟在next中指出
   if (current_index == 0 && if_same) lsib_id = parent_node.getNext();
   // 其他情况下对current_index做lower_bound修正
   else current_index -= if_same ? 0 : 1;

   // 获取左兄弟节点的id信息
    if (lsib_id == 0){
        Record lsib_info;
        unsigned int lsib_info_idx = current_index - 1;
        unsigned int lsib_id_len;
        parent_node.refslots(lsib_info_idx, lsib_info);
        lsib_info.getByIndex((char *)&lsib_id, &lsib_id_len, VALUE_INDEX);
        lsib_id = be32toh(lsib_id);
    }

   // 获取左兄弟
   Node left_sibling;
   attach_node(left_sibling, lsib_id);

   // 检查左兄弟是否有足够的项可以借
   SuperBlock superblock;
   BufDesp *desp = kBuffer.borrow(table_->name_.c_str(), 0);
   superblock.attach(desp->buffer);
   desp->relref();

   // 兄弟项不够借
   unsigned int min_entries = current_node.is_leaf() ? superblock.getOrder() / 2 : (superblock.getOrder() - 1) / 2;
   if (left_sibling.getSlots() <= min_entries) return {false, lsib_id};

   // 将左兄弟的最右侧项复制到当前节点
   Record last_record;
   left_sibling.refslots(left_sibling.getSlots() - 1, last_record);
   current_node.copyRecord(last_record);
   left_sibling.deallocate(left_sibling.getSlots() - 1);

   // 更新父节点中的相关键值

   // 1. 获取待更新项的键
   Record first_record;
   current_node.refslots(0, first_record);
   unsigned int new_key, new_key_len;
   first_record.getByIndex((char *) & new_key, &new_key_len, KEY_INDEX);

   // 2. 获取待更新项的值
   unsigned int new_value = current_node.getSelf();
   new_value = htobe32(new_value);

   // 3. 删除父节点中旧项
   parent_node.deallocate(current_index);

   // 4. 父节点中添加新项
   std::vector<struct iovec> new_kv(2);
   new_kv[0].iov_base = (void*) &new_key;
   new_kv[0].iov_len = new_key_len;
   new_kv[1].iov_base = (void *) &new_value;
   new_kv[1].iov_len = sizeof(new_value);
   parent_node.insertRecord(new_kv);

   return {true, lsib_id}; // 借项成功
}

std::pair<bool, unsigned int>
Bptree::borrow_rsib(Node &current_node, struct iovec key) {
   // 获取父节点
   unsigned int parent_id = track.top();
   Node parent_node;
   attach_node(parent_node, parent_id);

   // 查找当前节点在父节点中的索引
   unsigned int current_index = parent_node.searchRecord(key.iov_base, key.iov_len);

   // 对current_index进行修正(因为其最开始的index是lower_bound的)
   bool if_same = parent_node.same_key(key, current_index);
   unsigned int rsib_info_idx = 1;
   if (current_index == 0 && !if_same) rsib_info_idx = 0;
   else current_index -= if_same ? 0 : 1;

   // 没有右兄弟则失败
   if (current_index == parent_node.getSlots() - 1) return {false, 0};

   // 获取右兄弟的id信息
   Record rsib_info;
   rsib_info_idx = rsib_info_idx == 0 ? 0 : current_index + 1;
   unsigned int rsib_id, rsib_id_len;
   parent_node.refslots(rsib_info_idx, rsib_info);
   rsib_info.getByIndex((char *)&rsib_id, &rsib_id_len, VALUE_INDEX);
   rsib_id = be32toh(rsib_id);

   // 获取右兄弟
   Node right_sibling;
   attach_node(right_sibling, rsib_id);

   // 检查右兄弟是否有足够的项可以借
   SuperBlock superblock;
   BufDesp *desp = kBuffer.borrow(table_->name_.c_str(), 0);
   superblock.attach(desp->buffer);
   desp->relref();

   // 兄弟项不够借
   unsigned int min_entries = current_node.is_leaf() ? superblock.getOrder() / 2 : (superblock.getOrder() - 1) / 2;
   if (right_sibling.getSlots() <= min_entries) return {false, rsib_id};

   // 将右兄弟的最左侧项复制到当前节点
   Record first_record;
   right_sibling.refslots(0, first_record);
   current_node.copyRecord(first_record);
   right_sibling.deallocate(0);

   // 更新父节点中的相关键值

   // 1. 获取待更新项的键
   right_sibling.refslots(0, first_record);
   unsigned int new_key, new_key_len;
   first_record.getByIndex((char *) & new_key, &new_key_len, KEY_INDEX);

   // 2. 获取待更新项的值
   unsigned int new_value = right_sibling.getSelf();
   new_value = htobe32(new_value);

   // 3. 删除父节点中旧项
   parent_node.deallocate(rsib_info_idx);

   // 4. 父节点中添加新项
   std::vector<struct iovec> new_kv(2);
   new_kv[0].iov_base = (void*) &new_key;
   new_kv[0].iov_len = new_key_len;
   new_kv[1].iov_base = (void *) &new_value;
   new_kv[1].iov_len = sizeof(new_value);
   parent_node.insertRecord(new_kv);

   return {true, rsib_id}; // 借项成功
}

std::pair<struct iovec, unsigned int>
Bptree::merge(Node &left_node, Node &right_node){
    // 将右节点中的records都复制到左节点中
    while(right_node.getSlots() > 0){
        Record temp;
        right_node.refslots(0, temp);
        left_node.copyRecord(temp);
        right_node.deallocate(0);
    }

    // 获取父节点
    unsigned int parent_id = track.top();
    Node parent_node;
    attach_node(parent_node, parent_id);

    // 获取右节点的首record键值
    Record head_record;
    right_node.refslots(0, head_record);
    unsigned int head_key, head_key_len;
    head_record.getByIndex((char *) &head_key, &head_key_len, KEY_INDEX);
    struct iovec key;
    key.iov_base = &head_key;
    key.iov_len = head_key_len;

    // 查找右节点在父节点中的索引
    unsigned int right_idx = parent_node.searchRecord(key.iov_base, key.iov_len);
    bool if_same = parent_node.same_key(key, right_idx);
    right_idx -= if_same ? 0 : 1;

    // 获得rigt_record的key
    Record right_record;
    parent_node.refslots(right_idx, right_record);
    unsigned int keylen;
    right_record.getByIndex((char *) &key.iov_base, &keylen, KEY_INDEX);

    // 删去右节点在parent中对应的record，并删除右节点
    parent_node.deallocate(right_idx);
    table_->deallocate(right_node.getSelf());

    return {key, parent_id};
}

// 用于可视化B+树的函数
void Bptree::visualize() {
    std::pair<bool, unsigned int> root_info = get_root();
    if (!root_info.first) {
        std::cout << "当前树为空树" << std::endl;
        return;
    }

    unsigned int root_id = root_info.second;
    std::queue<unsigned int> node_queue;
    node_queue.push(root_id);

    while (!node_queue.empty()) {
        int level_size = (int) node_queue.size();
        while (level_size--) {
            unsigned int current_node_id = node_queue.front();
            node_queue.pop();

            Node current_node;
            attach_node(current_node, current_node_id);

            std::cout << "[Node " << current_node_id << "] ";
            for (int i = 0; i < current_node.getSlots(); ++i) {
                Record record;
                current_node.refslots(i, record);

                unsigned char *pkey;
                unsigned int key_len;
                record.refByIndex(&pkey, &key_len, KEY_INDEX);
                unsigned int key;
                memcpy(&key, pkey, key_len);
                key = be32toh(key);

                unsigned char* pvalue;
                unsigned int value_len;
                record.refByIndex(&pvalue, &value_len, VALUE_INDEX);
                unsigned int value;
                memcpy(&value, pvalue, value_len);
                value = be32toh(value);

                std::cout << "(" << key << "," << value << ")" <<" ";
            }
            std::cout << " | ";

            if (!current_node.is_leaf()) {
                for (int i = 0; i < current_node.getSlots(); ++i) {
                    Record record;
                    current_node.refslots(i, record);

                    unsigned char *pvalue;
                    unsigned int value_len;
                    int value;
                    record.refByIndex(&pvalue, &value_len, VALUE_INDEX);
                    memcpy(&value, pvalue, value_len);
                    value = be32toh(value);

                    node_queue.push(value);
                }
                if (current_node.getNext() != 0) {
                    node_queue.push(current_node.getNext());
                }
            }
        }
        std::cout << std::endl;
    }
}


}