#include <iostream>
#include <vector>
#include <queue>
#include <string>
#include <algorithm>
#include <cassert>

// 1. 定义请求的状态
enum class RequestStatus {
    WAITING,    // 在网络前端/等待队列中
    RUNNING,    // 正在 GPU 中进行 Token 迭代计算
    FINISHED    // 已经吐出终止符或达到最大长度，计算完成
};

// 2. 模拟单个用户请求（Sequence）
struct Sequence {
    int id;
    std::string prompt;
    int cur_len;        // 当前已经生成的 Token 数量
    int max_len;        // 该请求最大需要生成的 Token 数量
    RequestStatus status;

    Sequence(int req_id, std::string p, int max_l) 
        : id(req_id), prompt(p), cur_len(0), max_len(max_l), status(RequestStatus::WAITING) {}
};

// 3. 连续批处理调度引擎
class ContinuousBatchingEngine {
private:
    std::queue<Sequence*> waiting_queue; // 等待队列
    std::vector<Sequence*> running_batch; // 当前正在 GPU 里并发计算的 Batch
    int max_batch_size;                   // GPU 显存限制的最大并发 Batch 数

public:
    ContinuousBatchingEngine(int max_b) : max_batch_size(max_b) {}

    // 添加新请求到等待队列
    void add_request(Sequence* seq) {
        waiting_queue.push(seq);
    }

    // 💥 完美收网：手撕大模型流式生成 Iteration 级别调度状态机
    bool step_iteration() {
        // 1. 清理已经完成的请求 (Erase-remove idiom)
        // 保证物理向量连续，防止产生内存碎片，这是压榨吞吐的前提
        running_batch.erase(
            std::remove_if(running_batch.begin(), running_batch.end(),
                [](const Sequence* seq) { return seq->status == RequestStatus::FINISHED; }),
            running_batch.end()
        );

        // 2. 动态插队（一旦有空闲坑位，立马把等待队列头部的请求唤入 GPU）
        while (running_batch.size() < static_cast<size_t>(max_batch_size) && !waiting_queue.empty()) {
            Sequence* next_seq = waiting_queue.front();
            waiting_queue.pop();
            next_seq->status = RequestStatus::RUNNING;
            running_batch.push_back(next_seq);
        }

        // 如果两边都空了，说明所有推理任务彻底执行完毕
        if (running_batch.empty() && waiting_queue.empty()) {
            return false;
        }

        // 3. 模拟 GPU 执行了一次 Token 生成的 Iteration（相当于跑了一次 Forward 线性层+Attention）
        std::cout << "--- GPU Iteration Step ---" << std::endl;
        for (auto* seq : running_batch) {
            seq->cur_len++;
            std::cout << " [Request " << seq->id << "] 正在并发生成... 进度: " 
                      << seq->cur_len << "/" << seq->max_len << std::endl;
            
            // 4. 状态机转换边界控制：如果当前请求吐出的长度达到了上限，将其标记为 FINISHED
            if (seq->cur_len >= seq->max_len) {
                seq->status = RequestStatus::FINISHED;
            }
        }
        return true;
    }

    int get_running_size() const { return running_batch.size(); }
};

int main() {
    // 限制 GPU 最大并发 BatchSize 为 2（模拟真实的显存上限限制）
    ContinuousBatchingEngine engine(2);

    // 模拟 3 个不同变长长度的请求并发打入
    Sequence seq1(1, "帮我写一篇长文...", 4); // 需要 4 次迭代
    Sequence seq2(2, "你好", 2);           // 需要 2 次迭代（短请求，应该提前释放扣除）
    Sequence seq3(3, "算一下 1+1", 2);     // 需要 2 次迭代（排队等待插队）

    engine.add_request(&seq1);
    engine.add_request(&seq2);
    engine.add_request(&seq3);

    std::cout << "[ContinuousBatching] 引擎启动，动态调度开始..." << std::endl;

    int total_iterations = 0;
    while (engine.step_iteration()) {
        total_iterations++;
    }

    std::cout << "\n所有请求处理完毕。总计执行迭代轮数: " << total_iterations << std::endl;
    
    // 💥 严格断言：
    // 如果是传统的 Static Batching，总轮数必须等于 4 + 2 = 6 轮
    // 这里的 Continuous Batching 通过极致的插队流水线，将总迭代轮数优化到正好 4 轮！
    assert(total_iterations == 4);

    std::cout << "🎉 恭喜！第 7 题 Continuous Batching 调度内核测试通过！" << std::endl;
    return 0;
}