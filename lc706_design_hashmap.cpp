#include <iostream>
#include <vector>//内存紧挨着,连续的内存块,访问效率高,但是插入删除效率低
#include <list>//每个数字单独占据内存块,访问效率低,但是插入删除效率高,零散内存管理
#include <utility>

class ImplKVBlockHashMap {
private:
    // 工业级精算：选择质数作为桶的数量，可以最大程度减少哈希冲突
    static const int BUCKET_SIZE = 7919; 
    //质数只能是被1和自己整除法,他在是一万以内最大的质数,所有桶的数量足够,适合KV映射的分布,减少冲突,提高访问效率\也更加平稳

    
    // 每一个桶里面是一个双向链表，存储键值对 <Token_ID, Block_Addr>
    std::vector<std::list<std::pair<int, int>>> bucket_array;

    // 极致轻量级的哈希函数：直接取模
    //此处的const代表这个函数不会修改类的成员变量，保证了线程安全和函数的纯粹性,只是做计算,算出在哪个桶

    int get_hash_index(int key) const {
        return key % BUCKET_SIZE;
    }
//先提前摆好,resize,不然是空的

public:
    ImplKVBlockHashMap() {
        bucket_array.resize(BUCKET_SIZE);
    }

    // 插入或更新：全流程引用传递，杜绝多余拷贝
    void put_kv(int key, int value) {
        int index = get_hash_index(key);
        //&直接引用,不复制,直接操作原有数据,避免了数组拷贝的性能损失
        auto& bucket = bucket_array[index]; // 拿引用，不发生数组拷贝
        
        for (auto& kv_pair : bucket) {
            if (kv_pair.first == key) {
                kv_pair.second = value; // 键存在，更新显存块地址
                return;
            }
        }
        // 键不存在，在链表头部就地构造节点（emplace_front 比 push_front 更省内存）
        bucket.emplace_front(key, value);//emplace_front 直接在链表开头,下次查找能够优先遍历新数据

    }

    // 快速寻址：时间复杂度 O(1)
    int get_value(int key) const {
        int index = get_hash_index(key);
        const auto& bucket = bucket_array[index]; // const 引用，只读防篡改
        //:从后边拿前边的东西
        for (const auto& kv_pair : bucket) {
            if (kv_pair.first == key) {
                return kv_pair.second; // 命中，返回显存块物理地址
            }
        }
        return -1; // 未命中，说明该 Token 尚未被分配显存
    }

    // 显存块释放：动态擦除
    void remove_kv(int key) {
        int index = get_hash_index(key);
        auto& bucket = bucket_array[index];
        //it迭代器 it = bucket.begin() 从链表头部开始遍历,直到找到匹配的key,然后erase擦除这个节点,it !=bucket.end()直到最后节点
        
        for (auto it = bucket.begin(); it != bucket.end(); ++it) {
            if (it->first == key) {
                bucket.erase(it); // 安全擦除指针映射
                return;
            }
        }
    }
};

int main() {
    std::cout << "--- 🌟 虚拟显存块映射哈希表（LeetCode 706）底层演练 ---\n\n";

    ImplKVBlockHashMap kv_scheduler;

    std::cout << "⏳ [调度中] 正在为用户请求分配 Token 显存物理映射...\n";
    kv_scheduler.put_kv(10086, 2048); // Token ID 10086 映射到 2048 号显存块
    kv_scheduler.put_kv(10087, 4096);

    std::cout << "🔍 [寻址验证] Token 10086 映射的显存块物理地址为: " 
              << kv_scheduler.get_value(10086) << "\n";
    std::cout << "🔍 [寻址验证] Token 10087 映射的显存块物理地址为: " 
              << kv_scheduler.get_value(10087) << "\n";

    std::cout << "\n💀 [释放测试] 释放 Token 10086 的显存占用...\n";
    kv_scheduler.remove_kv(10086);
    std::cout << "🔍 [验证释放] 重新查询 Token 10086 的地址 (期望输出 -1): " 
              << kv_scheduler.get_value(10086) << "\n";

    return 0;
}