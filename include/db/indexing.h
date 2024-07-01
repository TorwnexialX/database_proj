// 由DataBlock继承出node，新增is_leaf和left_node变量
// 非叶子节点next无效，用left_node代替
// 叶子节点next有效，left_node无效

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <queue>
#include <stack>
#include "./block.h"
#include "./table.h"
#include "./buffer.h"

#define KEY_INDEX 0
#define VALUE_INDEX 1

namespace db {
    class Node : public DataBlock {
        public:
        bool is_leaf() {
            DataHeader *header = reinterpret_cast<DataHeader *>(buffer_);
            return header->leaf;
        }

        void set_leaf(bool leaf) {
            DataHeader *header = reinterpret_cast<DataHeader *>(buffer_);
            header->leaf = leaf;
        }

        unsigned int get_left() {
            MetaHeader *header = reinterpret_cast<MetaHeader *>(buffer_);
            return be32toh(header->next);
        }

        void set_left(unsigned int left_node) {
            MetaHeader *header = reinterpret_cast<MetaHeader *>(buffer_);
            header->next = htobe32(left_node);
        }

        bool same_key(struct iovec key, unsigned int record_index);
    }

    class Bptree {
        public:
        Table *table_;
        std::stack<unsigned int> track;
        DataType *key_type;
        DataType *value_type;
        Bptree() { table_ = nullptr; }

        /* key, value 各对应一个 struct iovec */
        // 根据给定key在bptree上查找，返回（是否成功，对应value）
        std::pair<bool, struct iovec> search(struct iovec key);
 
        inline void reset_track(){
            while(!track.empty())
                track.pop();
        }

        unsigned int find_leaf(struct iovec key);

        // 在给定节点后连接一个新节点，并返回该新节点
        // ATTENTION: 直接返回节点不确定是否会出现bug
        Node node_append(Node *node);

        void insert_to_index(struct iovec key, unsigned int new_node_id);

        // 根据给定key-value对在bptree上插入，返回是否成功
        bool insert(struct iovec key, struct iovec value);

        // 根据给定key在bptree上删除，返回是否成功
        bool remove(struct iovec key);

        // 获取根节点id
        std::pair<bool, unsigned int> get_root();

        // 将给定node绑定到B+树所在table的第node_id个node
        void attach_node(Node &node, unsigned int node_id);

        inline void set_table(Table *table_, unsigned int pkey, unsigned int pvalue) {
            this->table_ = table_;
            this->key_type = table_->info_->fields[pkey].type;
            this->value_type = table_->info_->fields[pvalue].type;
        }
    }
}