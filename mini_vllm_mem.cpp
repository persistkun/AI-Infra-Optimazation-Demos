#include <iostream>
#include <vector>
#include <atomic>
#include <cassert>
#include <cstring>

// ============================================================
// DualStream: 多角色页级引用计数显存管理器原型
// 对应科研提案: 面向端侧多模型协同推理的页级显存管理系统
// ============================================================

// 角色枚举：每个物理页的引用者身份
enum class Role : uint8_t {
    DRAFT    = 0,  // 投机采样中的 draft 模型（写入者）
    TARGET   = 1,  // 投机采样中的 target 模型（读取+验证者）
    POLICY   = 2,  // 具身智能中的高频控制模型（只读消费者）
    EVICTOR  = 3,  // 系统回收器
    MAX_ROLES = 4
};

const char* role_name(Role r) {
    switch (r) {
        case Role::DRAFT:   return "Draft";
        case Role::TARGET:  return "Target";
        case Role::POLICY:  return "Policy";
        case Role::EVICTOR: return "Evictor";
        default:            return "Unknown";
    }
}

// 物理页控制块
struct PhysicalBlock {
    void*    data;                    // GPU 显存地址（仿真中用 malloc）
    uint16_t ref_counts[4];           // 每个角色的独立引用计数
    Role     owner;                   // 当前写入者（拥有者）
    bool     dirty;                   // 脏页标记
    bool     evict_pending;           // 待回收标记

    PhysicalBlock() : data(nullptr), owner(Role::DRAFT), dirty(false), evict_pending(false) {
        for (int i = 0; i < 4; ++i) ref_counts[i] = 0;
    }
};

// 分配结果
struct AllocResult {
    int      block_id;   // -1 表示失败
    PhysicalBlock* block;
};

// ============================================================
// 核心：MultiRoleBlockManager
// 管理物理页池，支持多角色独立引用计数
// ============================================================
class MultiRoleBlockManager {
public:
    MultiRoleBlockManager(int num_blocks, size_t block_size)
        : total_blocks(num_blocks), block_size(block_size) {
        blocks.resize(num_blocks);
        for (int i = 0; i < num_blocks; ++i) {
            blocks[i].data = std::malloc(block_size);
            if (!blocks[i].data) {
                std::cerr << "[FATAL] malloc failed for block " << i << "\n";
                exit(1);
            }
            std::memset(blocks[i].data, 0, block_size);
            free_list.push_back(i);
        }
        std::cout << "[Init] 创建显存池: " << num_blocks << " 块, 每块 "
                  << block_size << " bytes\n";
    }

    ~MultiRoleBlockManager() {
        for (auto& b : blocks) {
            if (b.data) std::free(b.data);
        }
        std::cout << "[Destroy] 显存池已释放\n";
    }

    // ------------------ TODO 1: Allocate ------------------
    // 从空闲池中分配一个物理块，设置 owner
    // 返回 block_id，失败返回 -1
    AllocResult Allocate(Role owner) {
        // 边界检查：owner 不能是 EVICTOR（回收器不分配）
        if (owner == Role::EVICTOR) {
            std::cerr << "[Error] Evictor 不能分配新页\n";
            return {-1, nullptr};
        }

        // 检查空闲列表
        if (free_list.empty()) {
            std::cerr << "[OOM] 无可用物理块！触发回收...\n";
            TryEvict();
            if (free_list.empty()) {
                std::cerr << "[OOM] 回收后仍无可用块\n";
                return {-1, nullptr};
            }
        }

        // 从空闲列表尾部取一块（更高效，避免移动）
        int block_id = free_list.back();
        free_list.pop_back();

        PhysicalBlock& block = blocks[block_id];
        block.owner = owner;
        block.dirty = false;
        block.evict_pending = false;
        // 拥有者初始引用计数 = 1
        for (int i = 0; i < 4; ++i) block.ref_counts[i] = 0;
        block.ref_counts[static_cast<int>(owner)] = 1;

        std::cout << "[Alloc]  块 #" << block_id << " -> " << role_name(owner)
                  << " (refs: ";
        PrintRefs(block);
        std::cout << ")\n";
        return {block_id, &block};
    }

    // ------------------ TODO 2: AddRef ------------------
    // 某个角色增加对某块的引用
    // 返回增加后的引用计数
    uint16_t AddRef(int block_id, Role role) {
        if (!ValidBlock(block_id)) return 0;

        PhysicalBlock& block = blocks[block_id];
        int idx = static_cast<int>(role);
        
        // 如果块正在回收中，非 EVICTOR 不能加引用
        if (block.evict_pending && role != Role::EVICTOR) {
            std::cout << "[Warn]   块 #" << block_id << " 待回收中，"
                      << role_name(role) << " 跳过\n";
            return 0;
        }

        uint16_t old = block.ref_counts[idx];
        // 防溢出保护
        if (old == UINT16_MAX) {
            std::cerr << "[Error]  块 #" << block_id << " ref_count 溢出!\n";
            return old;
        }
        block.ref_counts[idx] = old + 1;

        std::cout << "[AddRef] 块 #" << block_id << " "
                  << role_name(role) << " " << old << " -> " << old + 1
                  << " (total: " << TotalRefs(block) << ")\n";
        return old + 1;
    }

    // ------------------ TODO 3: Release ------------------
    // 某个角色减少对某块的引用
    // 如果所有角色引用归零, 回收该块
    uint16_t Release(int block_id, Role role) {
        if (!ValidBlock(block_id)) return 0;

        PhysicalBlock& block = blocks[block_id];
        int idx = static_cast<int>(role);

        if (block.ref_counts[idx] == 0) {
            std::cout << "[Warn]   块 #" << block_id << " "
                      << role_name(role) << " 引用已为 0\n";
            return 0;
        }

        uint16_t old = block.ref_counts[idx];
        block.ref_counts[idx] = old - 1;
        uint16_t new_ref = old - 1;

        std::cout << "[Release]块 #" << block_id << " "
                  << role_name(role) << " " << old << " -> " << new_ref
                  << " (total: " << TotalRefs(block) << ")\n";

        // 如果所有角色引用归零，物理回收
        if (TotalRefs(block) == 0) {
            FreeBlock(block_id);
        }

        return new_ref;
    }

    // ------------------ 辅助方法 ------------------

    // 标记块为"待回收"（VLM 写完数据后调用）
    bool MarkEvictPending(int block_id) {
        if (!ValidBlock(block_id)) return false;
        PhysicalBlock& block = blocks[block_id];
        block.evict_pending = true;
        std::cout << "[Mark]   块 #" << block_id << " 标记为待回收\n";

        // 如果只有 EVICTOR 持有引用，立刻回收
        if (TotalRefs(block) == block.ref_counts[static_cast<int>(Role::EVICTOR)]) {
            // 除了 EVICTOR 没人持有，安全回收
            FreeBlock(block_id);
        }
        return true;
    }

    // 打印当前显存池状态
    void DebugPrint() const {
        std::cout << "\n=== 显存池状态 ===\n";
        std::cout << "总块数: " << total_blocks
                  << ", 空闲: " << free_list.size()
                  << ", 已用: " << (total_blocks - free_list.size()) << "\n";
        for (int i = 0; i < total_blocks; ++i) {
            // 跳过空闲块
            bool is_free = false;
            for (int f : free_list) {
                if (f == i) { is_free = true; break; }
            }
            if (is_free) continue;

            const auto& b = blocks[i];
            std::cout << "  块 #" << i
                      << " | owner: " << role_name(b.owner)
                      << " | refs: [";
            PrintRefs(b);
            std::cout << "] | total: " << TotalRefs(b)
                      << (b.dirty ? " [脏]" : "")
                      << (b.evict_pending ? " [待回收]" : "")
                      << "\n";
        }
        std::cout << "=================\n\n";
    }

private:
    int total_blocks;
    size_t block_size;
    std::vector<PhysicalBlock> blocks;
    std::vector<int> free_list;  // 空闲块 ID 池

    bool ValidBlock(int id) const {
        if (id < 0 || id >= total_blocks) {
            std::cerr << "[Error] 非法块 ID: " << id << "\n";
            return false;
        }
        return true;
    }

    uint16_t TotalRefs(const PhysicalBlock& block) const {
        uint16_t sum = 0;
        for (int i = 0; i < 4; ++i) sum += block.ref_counts[i];
        return sum;
    }

    void PrintRefs(const PhysicalBlock& block) const {
        bool first = true;
        for (int i = 0; i < 4; ++i) {
            if (block.ref_counts[i] > 0) {
                if (!first) std::cout << ", ";
                std::cout << role_name(static_cast<Role>(i))
                          << "=" << block.ref_counts[i];
                first = false;
            }
        }
    }

    void FreeBlock(int block_id) {
        PhysicalBlock& block = blocks[block_id];
        for (int i = 0; i < 4; ++i) block.ref_counts[i] = 0;
        block.dirty = false;
        block.evict_pending = false;
        free_list.push_back(block_id);
        std::cout << "[Free]   块 #" << block_id << " 已回收\n";
    }

    void TryEvict() {
        // 简单回收策略：找 evict_pending 且非 EVICTOR 引用已释的块
        for (int i = 0; i < total_blocks; ++i) {
            auto& b = blocks[i];
            if (!b.evict_pending) continue;
            // 检查是否只有 EVICTOR 角色持有引用
            bool only_evictor = true;
            for (int r = 0; r < static_cast<int>(Role::MAX_ROLES); ++r) {
                if (r == static_cast<int>(Role::EVICTOR)) continue;
                if (b.ref_counts[r] > 0) { only_evictor = false; break; }
            }
            if (only_evictor && b.ref_counts[static_cast<int>(Role::EVICTOR)] > 0) {
                FreeBlock(i);
            }
        }
    }
};

// ============================================================
// 测试场景
// ============================================================

// 场景 A：投机采样（Draft 产出 KV → Target 验证）
void TestSpeculativeDecoding(MultiRoleBlockManager& mgr) {
    std::cout << "\n========= 场景 A: 投机采样 =========\n";

    // Draft 模型分配 3 个物理块
    auto [id1, b1] = mgr.Allocate(Role::DRAFT);
    auto [id2, b2] = mgr.Allocate(Role::DRAFT);
    auto [id3, b3] = mgr.Allocate(Role::DRAFT);

    // Draft 写入完成，共享给 Target
    mgr.AddRef(id1, Role::TARGET);
    mgr.AddRef(id2, Role::TARGET);
    mgr.AddRef(id3, Role::TARGET);

    // Draft 释放自己的引用（零拷贝交付给 Target）
    mgr.Release(id1, Role::DRAFT);
    mgr.Release(id2, Role::DRAFT);
    mgr.Release(id3, Role::DRAFT);

    // Target 验证完成后释放
    mgr.Release(id1, Role::TARGET);
    mgr.Release(id2, Role::TARGET);
    mgr.Release(id3, Role::TARGET);

    mgr.DebugPrint();
}

// 场景 B：VLM + Policy 协同（具身智能）
void TestEmbodiedAI(MultiRoleBlockManager& mgr) {
    std::cout << "\n========= 场景 B: 具身智能 VLM + Policy =========\n";

    // VLM 分配一块用于写入视觉特征
    auto [vlm_block, vlm_b] = mgr.Allocate(Role::DRAFT);

    // Policy 模型需要读取这份特征（零拷贝共享）
    mgr.AddRef(vlm_block, Role::POLICY);

    // VLM 释放写入权（标记待回收，但不立即释放）
    mgr.MarkEvictPending(vlm_block);

    // Policy 以 100Hz 读取... （模拟几次读取后释放）
    std::cout << "  [Policy] 读取视觉特征帧 1...\n";
    std::cout << "  [Policy] 读取视觉特征帧 2...\n";
    std::cout << "  [Policy] 读取视觉特征帧 3...\n";

    mgr.Release(vlm_block, Role::POLICY);

    mgr.DebugPrint();
}

// 场景 C：OOM 压力测试
void TestOOMRecovery(MultiRoleBlockManager& mgr) {
    std::cout << "\n========= 场景 C: OOM 压力测试 =========\n";

    // 分配所有块耗尽池子（我们只用 4 块来测）
    std::vector<int> ids;
    for (int i = 0; ; ++i) {
        auto [id, block] = mgr.Allocate(Role::DRAFT);
        if (id == -1) break;
        ids.push_back(id);
    }
    std::cout << "  已分配 " << ids.size() << " 块，池子耗尽\n";

    // 尝试再分配（应触发 TryEvict，但无待回收块，返回 -1）
    auto [fail_id, fail_b] = mgr.Allocate(Role::DRAFT);
    assert(fail_id == -1);
    std::cout << "  [OK] OOM 时返回 -1\n";

    // 释放一部分
    for (size_t i = 0; i < ids.size() / 2; ++i) {
        mgr.Release(ids[i], Role::DRAFT);
    }

    // 再分配应成功
    auto [new_id, new_b] = mgr.Allocate(Role::TARGET);
    assert(new_id != -1);
    std::cout << "  [OK] 释放后重新分配成功: #" << new_id << "\n";

    // 清理
    for (size_t i = ids.size() / 2; i < ids.size(); ++i) {
        mgr.Release(ids[i], Role::DRAFT);
    }
    mgr.Release(new_id, Role::TARGET);
}

int main() {
    std::cout << "=== DualStream: 多角色页级引用计数显存管理器 ===\n\n";

    // 创建 8 个物理块，每块 64KB（模拟 8GB 端侧设备的微缩版）
    MultiRoleBlockManager mgr(8, 64 * 1024);

    TestSpeculativeDecoding(mgr);
    TestEmbodiedAI(mgr);
    TestOOMRecovery(mgr);

    std::cout << "\n=== 所有测试通过 ===\n";
    return 0;
}
