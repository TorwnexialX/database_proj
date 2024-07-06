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

   reset_track();

   while(!cur_node.is_leaf()){
       track.push(cur_node.getSelf());
       // 'lb' stands for 'lowerbound'
       unsigned int lb_index = cur_node.searchRecord(key.iov_base, key.iov_len);
       bool if_same = cur_node.same_key(key, lb_index); 
       
       // ATTENTION: 以下修正有误，查看另一branch是否正确，不行就用当前borrow_lsib的
       // lb_index == 0 走left_node
       if (lb_index == 0) {
           child = cur_node.get_left();
           child = be32toh(child);
           attach_node(cur_node, child);
       }

       // 如果给定的key与lb_key不同说明该key应在前面的record中
       lb_index -= if_same ? 0 : 1;

       Record lb_record;
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

   // B+树为空树
   if (superblock.getDataCounts() == 0) {
       unsigned int newroot = table_->allocate();
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
   if (former_node.same_key(key, lb_index)) return false;

   unsigned int node_id = leaf_id;

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
       Node next_node = node_append(&cur_node, cur_node.is_leaf());
       // 如果当前节点为叶子结点，则next_node需要设置为叶子结点
       if (cur_node.is_leaf() == true) next_node.set_leaf(true);
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
       node_id = htobe32(node_id);
       value.iov_base = &node_id;
       value.iov_len = sizeof(node_id);

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
}

bool remove(struct iovec key){
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
   if (!former_node.same_key(key, lb_index)) return false;

   // 要删除的项在该叶节点中，直接删除其中对应record
   leaf_node.deallocate(leaf_id);

   if (leaf_node.getSelf() == superblock.getRoot()) return true;

   // 每个record中最小的项数
   unsigned int min_keys = (superblock.getOrder() + 1) / 2 - 1;

   // 对于当前项数小于最小值时
   while (leaf_node.getSlots() < min_keys) {
      // 栈空，说明已经回溯到了根节点
      if (track.empty()) {
         // 根节点为空，说明树目前只有一个空的根节点，故将该树设置为空树
         if (leaf_node.getSlots() == 0) {
            superblock.setRoot(0);
            superblock.getDataCounts(0);
         }
         return true;
      }

      // 获取parent节点
      unsigned int parent_id = track.top();
      track.pop();
      Node parent_node;
      attach_node(parent_node, parent_id);

      // 获取sibling节点
      unsigned int sibling_id;
      Node sibling_node;

      // 在parent中查询当前key对应的record
      unsigned int sibling_info_idx = parent_node.searchRecord(key.iov_base, key.iov_len);
      unsigned int sibling_key;

      // 默认从左sibling中借键

      // 对于sibling_info_idx > 0的情况，parent中该key对应record的左record中存储着左sibling的信息
      if (sibling_info_idx > 0) {
         sibling_info_idx--;
         // 获得左sibling的info的record
         Record sibling_info;
         parent_node.refslots(sibling_info_idx, sibling_info);
         unsigned int sibling_key_len;
         sibling_info.getByIndex((char *)&sibling_key, &sibling_key_len, KEY_INDEX);
         sibling_key = be32toh(sibling_key);
      }

      // 获得sibling节点id
      sibling_id = (sibling_info_idx == 0) ? leaf_node.get_left() : sibling_key;

      // 对next域为0(无效)的node进行处理
      if (sibling_id == 0) {
         // 获取grand节点
         unsigned int grand_id = track.top();
         Node grand_node;
         attach_node(grand_node, grand_id);

         // 获取uncle节点
         unsigned int uncle_id;
         Node uncle_node;

         // 在grand中查询当前key对应的record
         unsigned int up1_idx = parent_node.searchRecord(key.iov_base, key.iov_len);
         unsigned int up1_key, up1_key_len;
         Record up1_record;
         parent_node.refslots(up1_idx, up1_record);
         up1_record.getByIndex(&up1_key, &up1_key_len, KEY_INDEX);
         unsigned int up2_idx = grand_node.searchRecord(up1_key, up1_key_len);
         unsigned int uncle_idx = up2_idx - 1; // grand中存uncle_node信息的record的idx
         
         //// TODO：
         unsigned int uncle_info_idx = grand_node.searchRecord(&sibling_key, sizeof(sibling_key));
         unsigned int uncle_key;

         // 默认从左sibling中借键

         // 对于sibling_info_idx > 0的情况，parent中该key对应record的左record中存储着左sibling的信息
         if (sibling_info_idx > 0) {
            sibling_info_idx--;
            // 获得左sibling的info的record
            Record sibling_info;
            parent_node.refslots(sibling_info_idx, sibling_info);
            unsigned int sibling_key_len;
            sibling_info.getByIndex((char *)&sibling_key, &sibling_key_len, KEY_INDEX);
            sibling_key = be32toh(sibling_key);
         }

         // 获得sibling节点id
         sibling_id = (sibling_info_idx == 0) ? leaf_node.get_left() : sibling_key;
      }
      attach_node(sibling_node, sibling_id);

      if (sibling_node.getSlots() > min_keys) {
         if (sibling_info_idx == 0) {
            leaf_node.copyRecord(sibling_node);
            sibling_node.deallocate(sibling_node.getSlots() - 1);
         } 
         else {
            leaf_node.copyRecord(0);
            sibling_node.deallocate(0);
         }
         return true;
      } 
      else {
         if (sibling_info_idx == 0) {
               sibling_node.setNext(leaf_node.getNext());
               for (unsigned int i = 0; i < leaf_node.getSlots(); i++) {
                  sibling_node.copyRecord(i);
               }
               leaf_node.deallocateAll();
         } 
         else {
               leaf_node.setNext(sibling_node.getNext());
               for (unsigned int i = 0; i < sibling_node.getSlots(); i++) {
                  leaf_node.copyRecord(i);
               }
               sibling_node.deallocateAll();
         }
         parent_node.deallocate(sibling_info_idx);
         leaf_node = parent_node;
      }
    }
    return true;
}

// 没有pop，在remove中需要加pop动作
bool Bptree::borrow_lsib(Node &current_node, struct iovec key) {
   // 获取父节点
   unsigned int parent_id = track.top();
   Node parent_node;
   attach_node(parent_node, parent_id);

   // 查找当前节点在父节点中的索引
   unsigned int current_index = parent_node.searchRecord(key.iov_base, key.iov_len);
   bool if_same = parent_node.same_key(key, current_index);
   current_index -= (if_same || current_index == 0) ? 0 : 1;
   if (current_index == 0) return false; // 没有左兄弟


   // 获取左兄弟节点的id信息
   Record lsib_info;
   unsigned int lsib_info_idx = current_index - 1;
   unsigned int lsib_id, lsib_id_len;
   parent_node.refslots(lsib_info_idx, lsib_info);
   lsib_info.getByIndex((char *)&lsib_id, &lsib_id_len, VALUE_INDEX);
   lsib_id = be32toh(lsib_id);

   // 获取左兄弟
   Node left_sibling;
   attach_node(left_sibling, lsib_id);

   // 检查左兄弟是否有足够的项可以借
   SuperBlock superblock;
   BufDesp *desp = kBuffer.borrow(table_->name_.c_str(), 0);
   superblock.attach(desp->buffer);
   desp->relref();

   // 兄弟项不够借
   unsigned int min_entries = (superblock.getOrder() + 1) / 2 - 1;
   if (left_sibling.getSlots() <= min_entries) return false;

   // 将左兄弟的最右侧项复制到当前节点
   Record last_record;
   left_sibling.refslots(left_sibling.getSlots() - 1, last_record);
   current_node.copyRecord(last_record);
   left_sibling.deallocate(left_sibling.getSlots() - 1);

   // 更新父节点中的相关键值

   // 1. 获取待更新项的键
   Record first_record;
   current_node.refslots(0, first_record);
   unsigned char *new_key;
   unsigned int new_key_len;
   first_record.getByIndex(&new_key, &new_key_len, KEY_INDEX);

   // 2. 获取待更新项的值
   unsigned int new_value = current_node.getSelf();
   new_value = htobe32(new_value);

   // 3. 删除父节点中旧项
   parent_node.deallocate(current_index);

   // 4. 父节点中添加新项
   std::vector<struct iovec> new_kv(2);
   new_kv[0].iov_base = (void*) new_key;
   new_kv[0].iov_len = new_key_len;
   new_kv[1].iov_base = (void *) &new_value;
   new_kv[1].iov_len = sizeof(new_value);
   parent_node.insertRecord(new_kv);

   return true; // 借项成功
}

bool Bptree::borrow_rsib(Node &current_node, struct iovec key) {
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
   if (current_index == parent_node.getSlots() - 1) return false;

   // 获取右兄弟的id信息
   Record rsib_info;
   rsib_info_idx = rsib_info_idx == 0 ? 0 : current_index + 1;
   unsigned int rsib_id, rsib_id_len;
   parent_node.refslots(rsib_info_idx, rsib_info);
   lsib_info.getByIndex((char *)&rsib_id, &rsib_id_len, VALUE_INDEX);
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
   unsigned int min_entries = (superblock.getOrder() + 1) / 2 - 1;
   if (right_sibling.getSlots() <= min_entries) return false;

   // 将右兄弟的最左侧项复制到当前节点
   Record first_record;
   right_sibling.refslots(0, first_record);
   current_node.copyRecord(first_record);
   right_sibling.deallocate(0);

   // 更新父节点中的相关键值

   // 1. 获取待更新项的键
   right_sibling.refslots(0, first_record);
   unsigned char *new_key;
   unsigned int new_key_len;
   first_record.getByIndex(&new_key, &new_key_len, KEY_INDEX);

   // 2. 获取待更新项的值
   unsigned int new_value = right_sibling.getSelf();
   new_value = htobe32(new_value);

   // 3. 删除父节点中旧项
   parent_node.deallocate(rsib_info_idx);

   // 4. 父节点中添加新项
   std::vector<struct iovec> new_kv(2);
   new_kv[0].iov_base = (void*) new_key;
   new_kv[0].iov_len = new_key_len;
   new_kv[1].iov_base = (void *) &new_value;
   new_kv[1].iov_len = sizeof(new_value);
   parent_node.insertRecord(new_kv);

   return true; // 借项成功
}

bool Bptree::merge()
}