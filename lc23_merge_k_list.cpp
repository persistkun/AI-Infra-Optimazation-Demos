#include <iostream>
#include <vector>
#include <queue>

// 1. 定义基本的链表节点（代表单个用户的请求流）
struct ListNode {
    int val;
    ListNode* next;
    ListNode(int x) : val(x), next(nullptr) {}
};

// 2. 核心：定义小顶堆的比较规则
struct CompareNodes {
    bool operator()(ListNode* a, ListNode* b) {
        // 小顶堆规则：谁的 val 小，谁的优先级就高（排在堆顶）
        return a->val > b->val; 
    }
};

class MultiStreamScheduler {
public:
    ListNode* mergeKLists(std::vector<ListNode*>& lists) {
        
        std::priority_queue<ListNode*, std::vector<ListNode*>, CompareNodes> min_heap;
        

        for (ListNode* head : lists) {
            if (head != nullptr) {
                min_heap.push(head); // 把每个流的头节点都放进小顶堆
            }
        }

        ListNode* dummy = new ListNode(0);
        ListNode* tail = dummy;

        while(!min_heap.empty())
        {
            ListNode* cur = min_heap.top();
            min_heap.pop();
            tail->next = cur;
            tail = tail->next;
            if (cur->next != nullptr){
                min_heap.push(cur->next);
            }
        }
        return dummy->next; // 记得最后返回 dummy->next
    }
};

// 辅助函数：用来打印最终合并后的流，验证正确性
void print_stream(ListNode* head) {
    while (head != nullptr) {
        std::cout << head->val;
        if (head->next) std::cout << " -> ";
        head = head->next;
    }
    std::cout << "\n";
}

int main() {
    std::cout << "--- 🛰️ 大模型多流并发动态调度器（LeetCode 23）演练 ---\n\n";

    // 模拟 3 个用户的并发请求流（已各自按时间戳/优先级升序排列）
    ListNode* user1 = new ListNode(1); user1->next = new ListNode(4); user1->next->next = new ListNode(5); // 1 -> 4 -> 5
    ListNode* user2 = new ListNode(1); user2->next = new ListNode(3); user2->next->next = new ListNode(4); // 1 -> 3 -> 4
    ListNode* user3 = new ListNode(2); user3->next = new ListNode(6);                                      // 2 -> 6

    std::vector<ListNode*> streams = {user1, user2, user3};

    MultiStreamScheduler scheduler;
    ListNode* merged_result = scheduler.mergeKLists(streams);

    std::cout << "🏁 [调度合并完成] 全局最优执行流顺序: ";
    print_stream(merged_result);

    // 手动析构内存的粗活我就包了，你专心写核心算法
    while (merged_result != nullptr) {
        ListNode* tmp = merged_result->next;
        delete merged_result;
        merged_result = tmp;
    }
    return 0;
}