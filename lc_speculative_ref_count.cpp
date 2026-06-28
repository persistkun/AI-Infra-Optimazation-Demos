#include <iostream>
#include <vector>
#include <cassert>

enum BlockState {
    FREE,       // 空闲，在 Free Pool 中
    HOT_FP16,   // 激活状态，保持高精度 FP16
    COLD_FP4    // 冷冻状态，已被压缩为 FP4
};

struct BlockMetadata {
    int block_id;
    int draft_ref_count;  // 草稿模式引用数
    int verify_ref_count; // 验证模式引用数
    BlockState state;
};

class DualStreamMemoryManager {
public:
    // 💥 任务：手撕多模式引用计数动态流转与回收状态机
    // block: 目标物理块的元数据
    // client_type: 释放发起方（0 代表 Draft 模式，1 代表 Verify 模式）
    static void release_block_reference(BlockMetadata& block, int client_type) {
        // 1. 【TODO 1】根据 client_type 扣减对应的引用计数
        if (client_type == 0) {
            if (block.draft_ref_count > 0) block.draft_ref_count--;
        } else if (client_type == 1) {
            if (block.verify_ref_count > 0) block.verify_ref_count--;
        }

        // 2. 【TODO 2】实现降级流转逻辑（Tiered Eviction）
        // 如果 draft 和 verify 还有人在用，保持 HOT_FP16。
        // 如果某一模式（通常是Draft验证失败回滚，或者Verify追上进度后）释放了引用，
        // 导致总引用数减少，但没有完全归零：
        // 策略：如果总引用数 > 0 且其中一方归零，触发论文中的“动态降级”，状态转为 COLD_FP4。
        if (block.draft_ref_count == 0 && block.verify_ref_count > 0) {
            block.state = COLD_FP4;
        }

        // 3. 【TODO 3】彻底释放内存归还 Free Pool
        // 如果双方的引用计数全部归零（不再有任何模式挂载这个 KV Block）
        // 状态转为 FREE
        if (block.draft_ref_count == 0 && block.verify_ref_count == 0) {
            block.state = FREE;
        }
    }
};

int main() {
    std::cout << "[Speculative RefCount] 启动第 10 题多模式内存回收内核测试..." << std::endl;

    // 模拟一个刚分配给投机序列的 HOT 物理块
    // 此时 Draft 模式和 Verify 模式都在共享它（引用计数均为 1）
    BlockMetadata test_block = { 42, 1, 1, HOT_FP16 };

    // 模拟场景 A：大模型并行验证发现小模型后面猜错了，触发残差回滚
    // 调度器通知：Draft 模式取消对该 Block 的超前挂载
    DualStreamMemoryManager::release_block_reference(test_block, 0); // client_type = 0: Draft

    // 💥 断言验证 1：此时 Verify 还在持有它，但由于 Draft 退出，该块应该被无缝降级为 COLD_FP4 压缩存储
    assert(test_block.draft_ref_count == 0);
    assert(test_block.state == COLD_FP4);
    std::cout << " -> 阶段 1 通关：成功触发 COLD_FP4 降级压缩机制！" << std::endl;

    // 模拟场景 B：Verify 模式最终读取并处理完了这个块，彻底释放引用
    DualStreamMemoryManager::release_block_reference(test_block, 1); // client_type = 1: Verify

    // 💥 断言验证 2：两边全部用完，该块彻底回收到 Free Pool
    assert(test_block.verify_ref_count == 0);
    assert(test_block.state == FREE);
    std::cout << " -> 阶段 2 通关：双模式引用归零，物理块完美回收！" << std::endl;

    std::cout << "🎉 恭喜！第 10 题多模式引用计数回收内核顺利通关！" << std::endl;
    return 0;
}