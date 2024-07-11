////
// @file blockTest.cc
// @brief
//
// @author niexw
// @email niexiaowen@uestc.edu.cn
//
#include "../catch.hpp"
#include <db/block.h>
#include <db/endian.h>
#include <db/record.h>
#include <db/buffer.h>
#include <db/file.h>
#include <db/table.h>

using namespace db;

TEST_CASE("db/block.h")
{
    SECTION("size")
    {
        REQUIRE(sizeof(CommonHeader) == sizeof(int) * 3);
        REQUIRE(sizeof(Trailer) == 2 * sizeof(int));
        REQUIRE(sizeof(Trailer) % 8 == 0);
        REQUIRE(
            sizeof(SuperHeader) ==
            sizeof(CommonHeader) + sizeof(TimeStamp) + 11 * sizeof(int)); 
            // 由9改成了11，这是由是由B+树部分带来2个4int型的属性扩充
        REQUIRE(sizeof(SuperHeader) % 8 == 0);
        REQUIRE(sizeof(IdleHeader) == sizeof(CommonHeader) + sizeof(int));
        REQUIRE(sizeof(IdleHeader) % 8 == 0);
        REQUIRE(
            sizeof(DataHeader) == sizeof(CommonHeader) + 2 * sizeof(int) +
                                      sizeof(TimeStamp) + 2 * sizeof(short));
        REQUIRE(sizeof(DataHeader) % 8 == 0);
    }

    SECTION("super")
    {
        SuperBlock super;
        unsigned char buffer[SUPER_SIZE];
        super.attach(buffer);
        super.clear(3);

        // magic number：0x64623031
        REQUIRE(buffer[0] == 0x64);
        REQUIRE(buffer[1] == 0x62);
        REQUIRE(buffer[2] == 0x30);
        REQUIRE(buffer[3] == 0x31);

        unsigned short type = super.getType();
        REQUIRE(type == BLOCK_TYPE_SUPER);
        unsigned short freespace = super.getFreeSpace();
        REQUIRE(freespace == sizeof(SuperHeader));

        unsigned int spaceid = super.getSpaceid();
        REQUIRE(spaceid == 3);

        unsigned int idle = super.getIdle();
        REQUIRE(idle == 0);

        TimeStamp ts = super.getTimeStamp();
        char tb[64];
        REQUIRE(ts.toString(tb, 64));
        // printf("ts=%s\n", tb);
        TimeStamp ts1;
        ts1.now();
        REQUIRE(ts < ts1);

        REQUIRE(super.checksum());
    }

    SECTION("data")
    {
        DataBlock data;
        unsigned char buffer[BLOCK_SIZE];

        data.attach(buffer);
        data.clear(1, 3, BLOCK_TYPE_DATA);

        // magic number：0x64623031
        REQUIRE(buffer[0] == 0x64);
        REQUIRE(buffer[1] == 0x62);
        REQUIRE(buffer[2] == 0x30);
        REQUIRE(buffer[3] == 0x31);

        unsigned int spaceid = data.getSpaceid();
        REQUIRE(spaceid == 1);

        unsigned short type = data.getType();
        REQUIRE(type == BLOCK_TYPE_DATA);

        unsigned short freespace = data.getFreeSpace();
        REQUIRE(freespace == sizeof(DataHeader));

        unsigned int next = data.getNext();
        REQUIRE(next == 0);

        unsigned int self = data.getSelf();
        REQUIRE(self == 3);

        TimeStamp ts = data.getTimeStamp();
        char tb[64];
        REQUIRE(ts.toString(tb, 64));
        // printf("ts=%s\n", tb);
        TimeStamp ts1;
        ts1.now();
        REQUIRE(ts < ts1);

        unsigned short slots = data.getSlots();
        REQUIRE(slots == 0);

        REQUIRE(data.getFreeSize() == data.getFreespaceSize());

        REQUIRE(data.checksum());

        REQUIRE(data.getTrailerSize() == 8);
        Slot *pslots =
            reinterpret_cast<Slot *>(buffer + BLOCK_SIZE - sizeof(Slot));
        REQUIRE(pslots == data.getSlotsPointer());
        REQUIRE(data.getFreespaceSize() == BLOCK_SIZE - 8 - sizeof(DataHeader));

        // 假设有5个slots槽位
        data.setSlots(5);
        REQUIRE(data.getTrailerSize() == sizeof(Slot) * 5 + sizeof(int));
        pslots =
            reinterpret_cast<Slot *>(buffer + BLOCK_SIZE - sizeof(Slot)) - 5;
        REQUIRE(pslots == data.getSlotsPointer());
        REQUIRE(
            data.getFreespaceSize() ==
            BLOCK_SIZE - data.getTrailerSize() - sizeof(DataHeader));
    }

    SECTION("allocate")
    {
        DataBlock data;
        unsigned char buffer[BLOCK_SIZE];

        data.attach(buffer);
        data.clear(1, 3, BLOCK_TYPE_DATA);

        // 分配8字节
        std::pair<unsigned char *, bool> alloc_ret = data.allocate(8, 0);
        REQUIRE(alloc_ret.first == buffer + sizeof(DataHeader));
        REQUIRE(data.getFreeSpace() == sizeof(DataHeader) + 8);
        REQUIRE(
            data.getFreeSize() ==
            BLOCK_SIZE - sizeof(DataHeader) - sizeof(Trailer) - 8);
        REQUIRE(data.getSlots() == 1);
        Slot *pslots = data.getSlotsPointer();
        REQUIRE(
            (unsigned char *) pslots ==
            buffer + BLOCK_SIZE - sizeof(int) - sizeof(Slot));
        REQUIRE(be16toh(pslots[0].offset) == sizeof(DataHeader));
        REQUIRE(be16toh(pslots[0].length) == 8);
        REQUIRE(data.getTrailerSize() == 8);

        // 随便写一个记录
        Record record;
        record.attach(buffer + sizeof(DataHeader), 8);
        std::vector<struct iovec> iov(1);
        int kkk = 3;
        iov[0].iov_base = (void *) &kkk;
        iov[0].iov_len = sizeof(int);
        unsigned char h = 0;
        record.set(iov, &h);

        // 分配5字节
        alloc_ret = data.allocate(5, 0);
        REQUIRE(alloc_ret.first == buffer + sizeof(DataHeader) + 8);
        REQUIRE(data.getFreeSpace() == sizeof(DataHeader) + 2 * 8);
        REQUIRE(
            data.getFreeSize() ==
            BLOCK_SIZE - sizeof(DataHeader) - sizeof(Trailer) - 3 * 8);
        REQUIRE(data.getSlots() == 2);
        pslots = data.getSlotsPointer();
        REQUIRE(
            (unsigned char *) pslots ==
            buffer + BLOCK_SIZE - sizeof(int) - 2 * sizeof(Slot));
        REQUIRE(be16toh(pslots[0].offset) == sizeof(DataHeader) + 8);
        REQUIRE(be16toh(pslots[0].length) == 8);
        REQUIRE(be16toh(pslots[1].offset) == sizeof(DataHeader));
        REQUIRE(be16toh(pslots[1].length) == 8);
        REQUIRE(data.getTrailerSize() == 16);

        record.attach(buffer + sizeof(DataHeader) + 8, 8);
        kkk = 4;
        iov[0].iov_base = (void *) &kkk;
        iov[0].iov_len = sizeof(int);
        record.set(iov, &h);

        // 分配711字节
        alloc_ret = data.allocate(711, 0);
        REQUIRE(alloc_ret.first == buffer + sizeof(DataHeader) + 8 * 2);
        REQUIRE(data.getFreeSpace() == sizeof(DataHeader) + 2 * 8 + 712);
        REQUIRE(
            data.getFreeSize() ==
            BLOCK_SIZE - sizeof(DataHeader) - sizeof(Trailer) - 3 * 8 - 712);
        REQUIRE(data.getSlots() == 3);
        pslots = data.getSlotsPointer();
        REQUIRE(
            (unsigned char *) pslots ==
            buffer + BLOCK_SIZE - sizeof(int) - 3 * sizeof(Slot));
        REQUIRE(be16toh(pslots[0].offset) == sizeof(DataHeader) + 16);
        REQUIRE(be16toh(pslots[0].length) == 712);
        REQUIRE(data.getTrailerSize() == 16);

        record.attach(buffer + sizeof(DataHeader) + 2 * 8, 712);
        char ggg[711 - 4];
        iov[0].iov_base = (void *) ggg;
        iov[0].iov_len = 711 - 4;
        record.set(iov, &h);
        REQUIRE(record.length() == 711);

        // 回收第2个空间
        unsigned short size = data.getFreeSize();
        data.deallocate(1);
        REQUIRE(data.getFreeSize() == size + 8);
        record.attach(buffer + sizeof(DataHeader) + 8, 8);
        REQUIRE(!record.isactive());

        REQUIRE(data.getSlots() == 2);
        pslots = data.getSlotsPointer();
        REQUIRE(
            (unsigned char *) pslots ==
            buffer + BLOCK_SIZE - sizeof(int) - 2 * sizeof(Slot));
        REQUIRE(be16toh(pslots[0].offset) == sizeof(DataHeader) + 16);
        REQUIRE(be16toh(pslots[0].length) == 712);
        REQUIRE(be16toh(pslots[1].offset) == sizeof(DataHeader));
        REQUIRE(be16toh(pslots[1].length) == 8);
        REQUIRE(data.getTrailerSize() == 16);

        data.shrink();
        size = data.getFreeSize();
        REQUIRE(
            size ==
            BLOCK_SIZE - sizeof(DataHeader) - data.getTrailerSize() - 8 - 712);
        unsigned short freespace = data.getFreeSpace();
        REQUIRE(freespace == sizeof(DataHeader) + 8 + 712);

        REQUIRE(data.getSlots() == 2);
        pslots = data.getSlotsPointer();
        REQUIRE(
            (unsigned char *) pslots ==
            buffer + BLOCK_SIZE - sizeof(int) - 2 * sizeof(Slot));
        REQUIRE(be16toh(pslots[0].offset) == sizeof(DataHeader));
        REQUIRE(be16toh(pslots[0].length) == 8);
        REQUIRE(be16toh(pslots[1].offset) == sizeof(DataHeader) + 8);
        REQUIRE(be16toh(pslots[1].length) == 712);
        REQUIRE(data.getTrailerSize() == 16);

        record.attach(buffer + sizeof(DataHeader) + 8, 8);
        REQUIRE(record.isactive());

        // 回收第3个空间
        size = data.getFreeSize();
        data.deallocate(1);
        REQUIRE(data.getFreeSize() == size + 712 + 8);
        record.attach(buffer + sizeof(DataHeader) + 8, 8);
        REQUIRE(!record.isactive());

        REQUIRE(data.getSlots() == 1);
        pslots = data.getSlotsPointer();
        REQUIRE(
            (unsigned char *) pslots ==
            buffer + BLOCK_SIZE - sizeof(int) - sizeof(Slot));
        REQUIRE(be16toh(pslots[0].offset) == sizeof(DataHeader));
        REQUIRE(be16toh(pslots[0].length) == 8);
        REQUIRE(data.getTrailerSize() == 8);

        // 回收第1个空间
        size = data.getFreeSize();
        data.deallocate(0);
        REQUIRE(data.getFreeSize() == size + 8);
        record.attach(buffer + sizeof(DataHeader), 8);
        REQUIRE(!record.isactive());

        // shrink
        data.shrink();
        size = data.getFreeSize();
        REQUIRE(
            size == BLOCK_SIZE - sizeof(DataHeader) - data.getTrailerSize());
        freespace = data.getFreeSpace();
        REQUIRE(freespace == sizeof(DataHeader));
    }

    SECTION("sort")
    {
        char x[3] = {'k', 'a', 'e'};
        std::sort(x, x + 3);
        REQUIRE(x[0] == 'a');
        REQUIRE(x[1] == 'e');
        REQUIRE(x[2] == 'k');
    }

    SECTION("reorder")
    {
        DataBlock data;
        unsigned char buffer[BLOCK_SIZE];

        data.attach(buffer);
        data.clear(1, 3, BLOCK_TYPE_DATA);

        // 假设表的字段是：id, char[12], varchar[512]
        std::vector<struct iovec> iov(3);
        DataType *type = findDataType("BIGINT");

        // 第1条记录
        long long id = 12;
        type->htobe(&id);
        iov[0].iov_base = &id;
        iov[0].iov_len = 8;
        iov[1].iov_base = "John Carter ";
        iov[1].iov_len = 12;
        const char *addr = "(323) 238-0693"
                           "909 - 1/2 E 49th St"
                           "Los Angeles, California(CA), 90011";
        iov[2].iov_base = (void *) addr;
        iov[2].iov_len = strlen(addr);

        // 分配空间
        unsigned short len = (unsigned short) Record::size(iov);
        std::pair<unsigned char *, bool> alloc_ret = data.allocate(len, 0);
        // 填充记录
        Record record;
        record.attach(alloc_ret.first, len);
        unsigned char header = 0;
        record.set(iov, &header);
        // 重新排序
        data.reorder(type, 0);

        REQUIRE(data.getFreeSpace() == sizeof(DataHeader) + len + 3);
        Slot *slot =
            (Slot *) (buffer + BLOCK_SIZE - sizeof(int) - sizeof(Slot));
        REQUIRE(be16toh(slot->offset) == sizeof(DataHeader));
        REQUIRE(be16toh(slot->length) == len + 3);
        REQUIRE(data.getSlots() == 1);

        // 第2条记录
        id = 3;
        type->htobe(&id);
        iov[0].iov_base = &id;
        iov[0].iov_len = 8;
        iov[1].iov_base = "Joi Biden    ";
        iov[1].iov_len = 12;
        const char *addr2 = "(323) 751-1875"
                            "7609 Mckinley Ave"
                            "Los Angeles, California(CA), 90001";
        iov[2].iov_base = (void *) addr2;
        iov[2].iov_len = strlen(addr2);

        // 分配空间
        unsigned short len2 = len;
        len = (unsigned short) Record::size(iov);
        alloc_ret = data.allocate(len, 0);
        // 填充记录
        record.attach(alloc_ret.first, len);
        record.set(iov, &header);
        REQUIRE(be16toh(slot->offset) == sizeof(DataHeader));
        REQUIRE(be16toh(slot->length) == len + 5);
        --slot;
        REQUIRE(be16toh(slot->offset) == sizeof(DataHeader) + len2 + 3);
        REQUIRE(be16toh(slot->length) == len + 5);
        // 重新排序
        data.reorder(type, 0);

        slot = (Slot *) (buffer + BLOCK_SIZE - sizeof(int) - 2 * sizeof(Slot));
        REQUIRE(be16toh(slot->offset) == sizeof(DataHeader) + len2 + 3);
        REQUIRE(be16toh(slot->length) == len + 5);
        ++slot;
        REQUIRE(be16toh(slot->offset) == sizeof(DataHeader));
        REQUIRE(be16toh(slot->length) == len2 + 3);

        // 按照name排序
        type = findDataType("CHAR");
        data.reorder(type, 1);
        slot = (Slot *) (buffer + BLOCK_SIZE - sizeof(int) - 2 * sizeof(Slot));
        REQUIRE(be16toh(slot->offset) == sizeof(DataHeader));
        REQUIRE(be16toh(slot->length) == len2 + 3);
        ++slot;
        REQUIRE(be16toh(slot->offset) == sizeof(DataHeader) + len2 + 3);
        REQUIRE(be16toh(slot->length) == len + 5);
    }

    SECTION("lowerbound")
    {
        char x[4] = {'a', 'c', 'e', 'k'};
        char s = 'e';
        char *ret = std::lower_bound(x, x + 4, s);
        REQUIRE(ret == x + 2);

        // b总是搜索值
        struct Comp
        {
            char val;
            bool operator()(char a, char b)
            {
                REQUIRE(b == -1);
                return a < val;
            }
        };
        Comp comp;
        comp.val = 'd';
        s = -1;
        ret = std::lower_bound(x, x + 4, s, comp);
        REQUIRE(ret == x + 2);
    }

    SECTION("search")
    {
        DataBlock data;
        unsigned char buffer[BLOCK_SIZE];

        data.attach(buffer);
        data.clear(1, 3, BLOCK_TYPE_DATA);

        // 假设表的字段是：id, char[12], varchar[512]
        std::vector<struct iovec> iov(3);
        DataType *type = findDataType("BIGINT");

        // 第1条记录
        long long id = 12;
        type->htobe(&id);
        iov[0].iov_base = &id;
        iov[0].iov_len = 8;
        iov[1].iov_base = "John Carter ";
        iov[1].iov_len = 12;
        const char *addr = "(323) 238-0693"
                           "909 - 1/2 E 49th St"
                           "Los Angeles, California(CA), 90011";
        iov[2].iov_base = (void *) addr;
        iov[2].iov_len = strlen(addr);

        // 分配空间
        unsigned short len = (unsigned short) Record::size(iov);
        std::pair<unsigned char *, bool> alloc_ret = data.allocate(len, 0);
        // 填充记录
        Record record;
        record.attach(alloc_ret.first, len);
        unsigned char header = 0;
        record.set(iov, &header);
        // 重新排序
        data.reorder(type, 0);
        // 重设校验和
        data.setChecksum();

        // 第2条记录
        id = 3;
        type->htobe(&id);
        iov[0].iov_base = &id;
        iov[0].iov_len = 8;
        iov[1].iov_base = "Joi Biden    ";
        iov[1].iov_len = 12;
        const char *addr2 = "(323) 751-1875"
                            "7609 Mckinley Ave"
                            "Los Angeles, California(CA), 90001";
        iov[2].iov_base = (void *) addr2;
        iov[2].iov_len = strlen(addr2);

        // 分配空间
        unsigned short len2 = len;
        len = (unsigned short) Record::size(iov);
        alloc_ret = data.allocate(len, 0);
        // 填充记录
        record.attach(alloc_ret.first, len);
        record.set(iov, &header);
        // 重新排序
        data.reorder(type, 0);

        Slot *slot =
            (Slot *) (buffer + BLOCK_SIZE - sizeof(int) - 2 * sizeof(Slot));
        REQUIRE(be16toh(slot->offset) == sizeof(DataHeader) + len2 + 3);
        REQUIRE(be16toh(slot->length) == len + 5);
        ++slot;
        REQUIRE(be16toh(slot->offset) == sizeof(DataHeader));
        REQUIRE(be16toh(slot->length) == len2 + 3);

        // 搜索
        id = htobe64(3);
        unsigned short ret = type->search(buffer, 0, &id, sizeof(id));
        REQUIRE(ret == 0);
        id = htobe64(12);++
        ret = type->search(buffer, 0, &id, sizeof(id));
        REQUIRE(ret == 1);
        id = htobe64(2);
        ret = type->search(buffer, 0, &id, sizeof(id));
        REQUIRE(ret == 0);
    }

    SECTION("insert")
    {
        Table table;
        table.open("table");

        // 从buffer中出借table:0
        BufDesp *bd = kBuffer.borrow("table", 0);
        REQUIRE(bd);
        // 将bd上buffer挂到super上
        SuperBlock super;
        super.attach(bd->buffer);
        int id = super.getFirst();
        REQUIRE(id == 1);
        int idle = super.getIdle();
        REQUIRE(idle == 0);
        // 释放buffer
        kBuffer.releaseBuf(bd);

        // 加载第1个data
        DataBlock data;
        // 设定block的meta
        data.setTable(&table);
        // 关联数据
        bd = kBuffer.borrow("table", 1);
        data.attach(bd->buffer);

        // 检查block，table表是空的，未添加任何表项
        REQUIRE(data.checksum());
        unsigned short size = data.getFreespaceSize();
        REQUIRE(
            BLOCK_SIZE - sizeof(DataHeader) - data.getTrailerSize() == size);

        // table = id(BIGINT)+phone(CHAR[20])+name(VARCHAR)
        // 准备添加
        DataType *type = findDataType("BIGINT");
        std::vector<struct iovec> iov(3);
        long long nid;
        char phone[20];
        phone[1] = '1';
        char addr[128];

        // Section之间是否独立（数据之间互相用，重定义）
        // 4个slots,每个slots有3个record?
        // slots扮演的角色？
        // 第1条记录
        nid = 7;
        type->htobe(&nid);
        iov[0].iov_base = &nid;
        iov[0].iov_len = 8;
        iov[1].iov_base = phone;
        iov[1].iov_len = 20;
        iov[2].iov_base = (void *) addr;
        iov[2].iov_len = 128;
        unsigned short osize = data.getFreespaceSize();
        unsigned short nsize = data.requireLength(iov);
        REQUIRE(nsize == 168);
        std::pair<bool, unsigned short> ret = data.insertRecord(iov);
        REQUIRE(ret.first);
        REQUIRE(ret.second == 0);
        REQUIRE(data.getFreespaceSize() == osize - nsize);
        REQUIRE(data.getSlots() == 1);
        Slot *slots = data.getSlotsPointer();
        Record record;
        record.attach(
            data.buffer_ + be16toh(slots[0].offset), be16toh(slots[0].length));
        REQUIRE(record.length() == Record::size(iov));
        REQUIRE(record.fields() == 3);
        long long xid;
        unsigned int len;
        record.getByIndex((char *) &xid, &len, 0);
        REQUIRE(len == 8);
        type->betoh(&xid);
        REQUIRE(xid == 7);
        unsigned char *pid;
        xid = 0;
        record.refByIndex(&pid, &len, 0);
        REQUIRE(len == 8);
        memcpy(&xid, pid, len);
        type->betoh(&xid);
        REQUIRE(xid == 7);

        // 第2条记录
        nid = 3;
        type->htobe(&nid);
        iov[0].iov_base = &nid;
        iov[0].iov_len = 8;
        iov[1].iov_base = phone;
        iov[1].iov_len = 20;
        iov[2].iov_base = (void *) addr;
        iov[2].iov_len = 128;
        osize = data.getFreespaceSize();
        nsize = data.requireLength(iov);
        REQUIRE(nsize == 176);
        ret = data.insertRecord(iov);
        REQUIRE(ret.first);
        REQUIRE(ret.second == 0);
        REQUIRE(data.getFreespaceSize() == osize - nsize);
        REQUIRE(data.getSlots() == 2);
        slots = data.getSlotsPointer();
        record.attach(
            data.buffer_ + be16toh(slots[0].offset), be16toh(slots[0].length));
        REQUIRE(record.length() == Record::size(iov));
        REQUIRE(record.fields() == 3);
        record.getByIndex((char *) &xid, &len, 0);
        REQUIRE(len == 8);
        type->betoh(&xid);
        REQUIRE(xid == 3);
        xid = 0;
        record.refByIndex(&pid, &len, 0);
        REQUIRE(len == 8);
        memcpy(&xid, pid, len);
        type->betoh(&xid);
        REQUIRE(xid == 3);

        // 第3条
        nid = 11;
        type->htobe(&nid);
        iov[0].iov_base = &nid;
        iov[0].iov_len = 8;
        iov[1].iov_base = phone;
        iov[1].iov_len = 20;
        iov[2].iov_base = (void *) addr;
        iov[2].iov_len = 128;
        osize = data.getFreespaceSize();
        nsize = data.requireLength(iov);
        REQUIRE(nsize == 168);
        ret = data.insertRecord(iov);
        REQUIRE(ret.first);
        REQUIRE(ret.second == 2);
        REQUIRE(data.getFreespaceSize() == osize - nsize);
        REQUIRE(data.getSlots() == 3);
        slots = data.getSlotsPointer();
        record.attach(
            data.buffer_ + be16toh(slots[2].offset), be16toh(slots[2].length));
        REQUIRE(record.length() == Record::size(iov));
        REQUIRE(record.fields() == 3);
        record.getByIndex((char *) &xid, &len, 0);
        REQUIRE(len == 8);
        type->betoh(&xid);
        REQUIRE(xid == 11);
        xid = 0;
        record.refByIndex(&pid, &len, 0);
        REQUIRE(len == 8);
        memcpy(&xid, pid, len);
        type->betoh(&xid);
        REQUIRE(xid == 11);

        // 第4条 3 7 11
        nid = 5;
        type->htobe(&nid);
        iov[0].iov_base = &nid;
        iov[0].iov_len = 8;
        iov[1].iov_base = phone;
        iov[1].iov_len = 20;
        iov[2].iov_base = (void *) addr;
        iov[2].iov_len = 128;
        osize = data.getFreespaceSize();
        nsize = data.requireLength(iov);
        REQUIRE(nsize == 176);
        ret = data.insertRecord(iov);
        REQUIRE(ret.first);
        REQUIRE(ret.second == 1);
        REQUIRE(data.getFreespaceSize() == osize - nsize);
        REQUIRE(data.getSlots() == 4);
        slots = data.getSlotsPointer();
        record.attach(
            data.buffer_ + be16toh(slots[1].offset), be16toh(slots[1].length));
        REQUIRE(record.length() == Record::size(iov));
        REQUIRE(record.fields() == 3);
        record.getByIndex((char *) &xid, &len, 0);
        REQUIRE(len == 8);
        type->betoh(&xid);
        REQUIRE(xid == 5);
        xid = 0;
        record.refByIndex(&pid, &len, 0);
        REQUIRE(len == 8);
        memcpy(&xid, pid, len);
        type->betoh(&xid);
        REQUIRE(xid == 5);

        // 键重复，无法插入
        ret = data.insertRecord(iov);
        REQUIRE(!ret.first);
        REQUIRE(ret.second == (unsigned short) -1);

        // 写入，释放
        kBuffer.writeBuf(bd);
        kBuffer.releaseBuf(bd);
    }

    SECTION("remove"){
        // 验证了删去已有record和不存在record两种情况
        // 通过slots数减少验证record数减少
        // 验证freesize, freespacesize, freespace前后变化
        // 验证tombstone置位
        Table table;
        table.open("table");
        //加载超级块
        BufDesp *bd = kBuffer.borrow("table", 0);
        SuperBlock super;
        super.attach(bd->buffer);
        int id = super.getFirst();
        // 加载第一个DataBlock
        DataBlock data;
        data.setTable(&table);
        BufDesp *bd2 = kBuffer.borrow("table", id);
        data.attach(bd2->buffer);
        bd2->relref();

        /* 删除一个存在的记录 */
        // 删除nid=7的记录，因而先构造该记录
        DataType *type = findDataType("BIGINT");
        std::vector<struct iovec> iov(3);
        long long nid;
        char phone[20];
        phone[1] = '1';
        char addr[128];

        nid = 7;
        type->htobe(&nid);
        iov[0].iov_base = &nid;
        iov[0].iov_len = 8;
        iov[1].iov_base = phone;
        iov[1].iov_len = 20;
        iov[2].iov_base = (void *) addr;
        iov[2].iov_len = 128;

        // 删除前对record引用，nid=7的记录索引为2（因为已经存储的nid是:3 5 7 11）
        Record record;
        unsigned char *buffer = data.buffer_;
        Slot *slots = data.getSlotsPointer();
        record.attach(
            data.buffer_ + be16toh(slots[2].offset),
            be16toh(slots[2].length)); // nid=7，排序完后索引为2

        // 删除前保存内存情况(freesize, freespacesize, freespace)
        unsigned short before_freespacesize = data.getFreespaceSize(); // Block的FreeSpaceSize
        unsigned short before_freesize = data.getFreeSize(); // Block的FreeSize
        unsigned short before_freespace = data.getFreeSpace();

        std::pair<bool, unsigned short> remove_result = data.removeRecord(iov[0]);

        // 删除后的内存情况(freesize, freespacesize, freespace)
        unsigned short after_freesize = data.getFreeSize(); // 176改变来源于 `deallocate()`
        unsigned short after_freespacesize = data.getFreespaceSize();
        unsigned short after_freespace = data.getFreeSpace();

        REQUIRE(remove_result.first);
        REQUIRE(remove_result.second == 2);
        REQUIRE(data.getSlots() == 3); // metahead中record数量 只找到了slots
        REQUIRE(after_freespacesize - before_freespacesize == 8); // slots由4->3，trailer空间4*4+4->4*3+4，这样不用多对齐一次，所以少8
        REQUIRE(after_freesize - before_freesize == 168+8); // 168+8 record所需大小+为了对齐向上取的8
        REQUIRE(*record.buffer_ == RECORD_MASK_TOMBSTONE); // tombstone置位
        REQUIRE(before_freespace == after_freespace); // freespace不变

        // 把原来记录加上
        data.insertRecord(iov);

        /* 删除不存在的记录 */
        long long inexisted_nid = 10;
        type->htobe(&inexisted_nid);
        struct iovec inexisted_key;
        inexisted_key.iov_base = &inexisted_nid;
        inexisted_key.iov_len = sizeof(inexisted_nid);
        
        remove_result = data.removeRecord(inexisted_key);
        REQUIRE(remove_result.first == false);
        REQUIRE(remove_result.second == 0);
              
    }

    SECTION("update"){
        // 更新record为同长record
        // 更新不存在record
        Table table;
        table.open("table");
        //加载超级块
        BufDesp *bd = kBuffer.borrow("table", 0);
        SuperBlock super;
        super.attach(bd->buffer);
        int id = super.getFirst();
        // 加载第一个DataBlock
        DataBlock data;
        data.setTable(&table);
        BufDesp *bd2 = kBuffer.borrow("table", id);
        data.attach(bd2->buffer);
        bd2->relref();

        /* 更新nid=7的record为同长record */
        Record record;
        data.refslots(2, record); // nid=7的record索引为2
        unsigned char *pkey;
        unsigned int plen;
        record.refByIndex(&pkey, &plen, 1); // `phone[1]=='1'`出现在iov(1)中
        REQUIRE(pkey[1] == '1');

        // 设置更新内容情况（只改了phone[1]，不改nid是因为nid为主键）
        DataType *type = findDataType("BIGINT");
        std::vector<struct iovec> iov(3);
        long long nid;
        char phone[20];
        phone[1] = '2';
        char addr[128];
        nid = 7;
        type->htobe(&nid);
        iov[0].iov_base = &nid;
        iov[0].iov_len = 8;
        iov[1].iov_base = phone;
        iov[1].iov_len = 20;
        iov[2].iov_base = (void *) addr;
        iov[2].iov_len = 128;

        // 更新前内存情况
        unsigned short before_freespacesize = data.getFreespaceSize(); // Block的FreeSpaceSize
        unsigned short before_freesize = data.getFreeSize(); // Block的FreeSize
        unsigned short before_freespace = data.getFreeSpace();
        unsigned short record_size = data.requireLength(iov);
        REQUIRE(record_size == 168);

        std::pair<bool, unsigned short> update_result = data.updateRecord(iov);

        // 更新后内存情况
        unsigned short after_freesize = data.getFreeSize();
        unsigned short after_freespacesize = data.getFreespaceSize();
        unsigned short after_freespace = data.getFreeSpace();

        REQUIRE(update_result.first);
        REQUIRE(update_result.second == 2);
        REQUIRE(data.getSlots() == 4);
        REQUIRE(before_freespacesize - after_freespacesize == record_size); 
        REQUIRE(before_freesize == after_freesize); 
        REQUIRE(after_freespace == before_freespace + record_size); 

        // 检查内容更新情况
        data.refslots(2, record);
        record.refByIndex(&pkey, &plen, 1);
        REQUIRE(pkey[1] != '1');
        REQUIRE(pkey[1] == '2');

        /* 更新record为过长record */
        iov[2].iov_len = (size_t) BLOCK_SIZE; // 更新过长（超过block大小）
        update_result = data.updateRecord(iov);
        REQUIRE(!update_result.first);
        //此时Record被标记为Tomestone，但是没有插入，需要分裂blk
        if (!update_result.first) {
            iov[2].iov_base = (void *) addr;
            iov[2].iov_len = 128;
            data.insertRecord(iov);
        }

        /* 更新不存在的record */
        nid = 10;
        update_result = data.updateRecord(iov);
        unsigned short nslots = data.getSlots();
        REQUIRE(update_result.first == false);
        REQUIRE(update_result.second == 0);
        nid = 7; // 还原nid
    }

    SECTION("iterator")
    {
        Table table;
        table.open("table");

        // 加载第1个data
        DataBlock data;
        // 设定block的meta
        data.setTable(&table);
        // 关联数据
        BufDesp *bd = kBuffer.borrow("table", 1);
        data.attach(bd->buffer);

        DataBlock::RecordIterator ri = data.beginrecord();
        REQUIRE(ri.index == 0);
        unsigned char *pkey;
        unsigned int len;
        ri->refByIndex(&pkey, &len, 0);
        long long key;
        memcpy(&key, pkey, len);
        key = be64toh(key);
        REQUIRE(key == 3); // 3 5 7 11

        ++ri;
        ri->refByIndex(&pkey, &len, 0);
        memcpy(&key, pkey, len);
        key = be64toh(key);
        REQUIRE(key == 5); // 3 5 7 11

        ri++;
        ri->refByIndex(&pkey, &len, 0);
        memcpy(&key, pkey, len);
        key = be64toh(key);
        REQUIRE(key == 7); // 3 5 7 11

        --ri;
        ri->refByIndex(&pkey, &len, 0);
        memcpy(&key, pkey, len);
        key = be64toh(key);
        REQUIRE(key == 5); // 3 5 7 11

        ri--;
        ri->refByIndex(&pkey, &len, 0);
        memcpy(&key, pkey, len);
        key = be64toh(key);
        REQUIRE(key == 3); // 3 5 7 11

        --ri;
        bool ret = ri == data.endrecord();
        REQUIRE(ret);

        ri += 2;
        ri->refByIndex(&pkey, &len, 0);
        memcpy(&key, pkey, len);
        key = be64toh(key);
        REQUIRE(key == 5); // 3 5 7 11

        ri -= 3;
        ri->refByIndex(&pkey, &len, 0);
        memcpy(&key, pkey, len);
        key = be64toh(key);
        REQUIRE(key == 11); // 3 5 7 11

        kBuffer.releaseBuf(bd);
    }
}