#include <iostream>
#include <unordered_map>

class LRUCache {
private:
    // 定义双向链表的节点，模拟虚拟显存块控制句柄
    struct Node {
        int key;        // 用户 Session ID 或 Token ID
        int value;      // 对应的物理显存块指针/地址
        Node* prev;
        Node* next;
        Node(int k, int v) : key(k), value(v), prev(nullptr), next(nullptr) {}
    };

    int capacity;       // 显存池最大允许承载的块容量
    Node* head;         // 哨兵头节点（代表最近最常使用的活跃显存块）
    Node* tail;         // 哨兵尾节点（代表最久未使用的、即将被淘汰的显存块）
    
    // 哈希表：快速定位节点在链表中的物理位置，实现 O(1) 查找
    std::unordered_map<int, Node*> cache_map;

    // 内部私有指针操作 A：将一个节点拉到链表头部（升级为最活跃请求）
    void move_to_head(Node* node) {
        remove_node(node);
        add_to_head(node);
    }

    // 内部私有指针操作 B：将节点从当前链表中断开
    void remove_node(Node* node) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }

    // 内部私有指针操作 C：就地挂载到头节点后面
    void add_to_head(Node* node) {
        node->next = head->next;
        node->prev = head;
        head->next->prev = node;
        head->next = node;
    }

    // 内部私有指针操作 D：淘汰尾部最久未使用的节点，防止显存爆炸
    Node* pop_tail() {
        Node* res = tail->prev;
        remove_node(res);
        return res;
    }

public:
    LRUCache(int cap) : capacity(cap) {
        // 使用伪头部和伪尾部哨兵节点，规避高并发下复杂的边界指针判空，提速执行效率
        head = new Node(0, 0);
        tail = new Node(0, 0);
        head->next = tail;
        tail->prev = head;
    }
    
    ~LRUCache() {
        // 严格析构，手动释放链表内存，严防任何一滴内存泄漏
        Node* curr = head;
        while (curr != nullptr) {
            Node* next_node = curr->next;
            delete curr;
            curr = next_node;
        }
        std::cout << "💀 [内存安全] LRU 显存调度器析构完成，所有句柄安全销毁。\n";
    }

    // 获取显存块地址
    int get_block(int key) {
        if (cache_map.find(key) == cache_map.end()) {
            return -1; // 未命中，说明已被换出到 CPU 内存
        }
        Node* node = cache_map[key];
        move_to_head(node); // 被访问后，其优先级自动飙升至头部
        return node->value;
    }

    // 分配显存块
    void put_block(int key, int value) {
        if (cache_map.find(key) != cache_map.end()) {
            // 块已存在，更新值并挪到头部
            Node* node = cache_map[key];
            node->value = value;
            move_to_head(node);
        } else {
            // 新的块请求进来
            Node* new_node = new Node(key, value);
            cache_map[key] = new_node;
            add_to_head(new_node);

            // 如果触发显存池容量上限，执行冷酷淘汰机制
            if (cache_map.size() > capacity) {
                Node* obsolete = pop_tail(); // 踢掉最久不说话的块
                cache_map.erase(obsolete->key); // 擦除哈希索引
                delete obsolete; // 物理释放，规避 OOM
                std::cout << "⚠️ [显存置换] 显存已满！触发 LRU 策略自动剔除旧块。\n";
            }
        }
    }
};

int main() {
    std::cout << "--- 🚀 大模型云端分布式引擎：KV Cache LRU 淘汰机制演练 ---\n\n";

    // 模拟一个容量只能装下 2 个用户长文本 KV Cache 的极限显存池
    LRUCache vllm_mem_pool(2);

    std::cout << "📥 分配用户 1 和用户 2 的显存物理块...\n";
    vllm_mem_pool.put_block(1, 101); // 用户 1 占用 101 号物理块
    vllm_mem_pool.put_block(2, 102); // 用户 2 占用 102 号物理块

    std::cout << "🔍 查询用户 1 的块地址: " << vllm_mem_pool.get_block(1) << " (用户1活跃度升级)\n";

    std::cout << "\n📥 突发大量新 Token！强行分配新用户 3 的显存块...\n";
    vllm_mem_pool.put_block(3, 103); // 此时显存池满了，用户 2 应该被淘汰

    std::cout << "🔍 验证淘汰结果：查询用户 2 的块地址 (期望输出 -1): " << vllm_mem_pool.get_block(2) << "\n";
    std::cout << "🔍 验证生存结果：查询用户 1 的块地址 (期望输出 101): " << vllm_mem_pool.get_block(1) << "\n\n";

    return 0;
}