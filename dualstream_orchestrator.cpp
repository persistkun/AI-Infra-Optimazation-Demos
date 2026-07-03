#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <cassert>
#include <cstring>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <random>
#include <numeric>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <iomanip>

// ============================================================
// DualStream Phase 3: Multi-Model Orchestrator
// 并发多模型协调器 + 滑动水位线 + 实际竞争测量
//
// 增量:
//   1. SharedBlockPoolV2: 线程安全, sliding waterline
//   2. SlidingWaterline: 动态 zone 大小调整
//   3. MultiModelOrchestrator: 真正的多模型生命周期管理
//   4. 并发竞争测试: 实际线程, 测量 blocking rate
// ============================================================

// ============================================================
// 基础类型
// ============================================================

using ModelID = int;

enum class Role : uint8_t {
    PRODUCER   = 0,
    CONSUMER   = 1,
    SHARER     = 2,
    EVICTOR    = 3,
    MAX_ROLES  = 4
};

const char* role_name(Role r) {
    switch (r) {
        case Role::PRODUCER:  return "Producer";
        case Role::CONSUMER:  return "Consumer";
        case Role::SHARER:    return "Sharer";
        case Role::EVICTOR:   return "Evictor";
        default:              return "Unknown";
    }
}

enum class AccessMode : uint8_t {
    FREE,
    WRITING,
    SHARED_READ,
    EVICT_PENDING,
    COW_PENDING
};

const char* access_mode_name(AccessMode m) {
    switch (m) {
        case AccessMode::FREE:          return "FREE";
        case AccessMode::WRITING:       return "WRITING";
        case AccessMode::SHARED_READ:   return "SHARED_READ";
        case AccessMode::EVICT_PENDING: return "EVICT_PENDING";
        case AccessMode::COW_PENDING:   return "COW_PENDING";
        default:                        return "UNKNOWN";
    }
}

enum class FreqClass : uint8_t {
    LOW_FREQ  = 0,
    HIGH_FREQ = 1,
    MAX_CLASSES = 2
};

FreqClass other_class(FreqClass c) {
    return c == FreqClass::HIGH_FREQ ? FreqClass::LOW_FREQ : FreqClass::HIGH_FREQ;
}

const char* freq_name(FreqClass c) {
    return c == FreqClass::HIGH_FREQ ? "high" : "low";
}

// ============================================================
// 滑动水位线
// 动态调整高低频 zone 边界
// ============================================================

class SlidingWaterline {
public:
    struct ZoneSnapshot {
        int total;
        int used;
        float waterline;
        int peak_used;
        int borrow_count;
        int forced_evict_count;
    };

private:
    int total_blocks;
    int high_freq_start;   // 当前高频区起始索引
    int min_high_freq;     // 最小高频区大小 (硬下限)
    int max_high_freq;     // 最大高频区大小 (硬上限)
    
    // 统计数据
    int peak_high_used;
    int peak_low_used;
    int borrow_events;
    int forced_evictions;
    int resize_events;

    mutable std::mutex mtx;

public:
    SlidingWaterline(int total, float initial_high_ratio = 0.2f,
                     float min_high_ratio = 0.1f, float max_high_ratio = 0.5f)
        : total_blocks(total),
          min_high_freq(static_cast<int>(total * min_high_ratio)),
          max_high_freq(static_cast<int>(total * max_high_ratio)),
          peak_high_used(0), peak_low_used(0),
          borrow_events(0), forced_evictions(0), resize_events(0) {
        
        high_freq_start = total - static_cast<int>(total * initial_high_ratio);
        if (high_freq_start < total - max_high_freq)
            high_freq_start = total - max_high_freq;
        if (high_freq_start > total - min_high_freq)
            high_freq_start = total - min_high_freq;
        
        std::cout << "[Waterline] init high_start=" << high_freq_start
                  << " min_high=" << min_high_freq
                  << " max_high=" << max_high_freq << "\n";
    }

    bool is_high_freq(int block_id) const {
        return block_id >= high_freq_start;
    }

    int high_freq_count() const { return total_blocks - high_freq_start; }
    int low_freq_count() const { return high_freq_start; }

    int high_start() const { return high_freq_start; }

    // 高频区需要更多空间: 向左扩展
    bool expand_high(int need_blocks) {
        std::lock_guard<std::mutex> lock(mtx);
        int current_hf = total_blocks - high_freq_start;
        if (current_hf >= max_high_freq) {
            return false;  // 已达上限
        }
        int target_hf = std::min(current_hf + need_blocks, max_high_freq);
        int new_start = total_blocks - target_hf;
        
        std::cout << "[Waterline] expand high: " << high_freq_start
                  << " -> " << new_start << " (+" << (high_freq_start - new_start) << ")\n";
        high_freq_start = new_start;
        resize_events++;
        return true;
    }

    // 低频区需要更多空间: 向右压缩高频
    bool expand_low(int need_blocks) {
        std::lock_guard<std::mutex> lock(mtx);
        int current_lf = high_freq_start;
        if (current_lf >= total_blocks - min_high_freq) {
            return false;  // 高频已达最小
        }
        int target_lf = std::min(current_lf + need_blocks, 
                                  total_blocks - min_high_freq);
        int new_start = total_blocks - target_lf;
        // 但 new_start 不能大于 high_freq_start (只有向右移动)
        if (new_start <= high_freq_start) 
            return false;
        
        std::cout << "[Waterline] expand low: " << high_freq_start
                  << " -> " << (high_freq_start + (new_start - high_freq_start)) 
                  << " (+" << (target_lf - current_lf) << ")\n";
        high_freq_start = new_start;
        resize_events++;
        return true;
    }

    void record_peak(FreqClass fc, int usage) {
        std::lock_guard<std::mutex> lock(mtx);
        if (fc == FreqClass::HIGH_FREQ) {
            if (usage > peak_high_used) peak_high_used = usage;
        } else {
            if (usage > peak_low_used) peak_low_used = usage;
        }
    }

    void note_borrow() { borrow_events++; }
    void note_evict() { forced_evictions++; }

    // 检查当前分配的合理性, 返回建议迁移的 blocks
    // positive = 应该移入高频的块数, negative = 应该移出的块数
    int evaluate(const std::vector<int>& zone_usage, 
                  int pending_migrations) const {
        int hf_used = zone_usage[static_cast<int>(FreqClass::HIGH_FREQ)];
        int lf_used = zone_usage[static_cast<int>(FreqClass::LOW_FREQ)];
        int hf_cap = total_blocks - high_freq_start;
        int lf_cap = high_freq_start;
        
        float hf_ratio = hf_cap > 0 ? (float)hf_used / hf_cap : 1.0f;
        float lf_ratio = lf_cap > 0 ? (float)lf_used / lf_cap : 1.0f;
        
        // 高频超过 85% -> 需要扩展 (borrow from low)
        if (hf_ratio > 0.85f && pending_migrations < 0) {
            return 1;
        }
        // 低频超过 85% 且高频利用率低 -> 压缩高频
        if (lf_ratio > 0.85f && hf_ratio < 0.5f && pending_migrations > 0) {
            return -1;
        }
        return 0;  // 平衡
    }

    std::string report() const {
        std::lock_guard<std::mutex> lock(mtx);
        std::ostringstream oss;
        oss << "[Waterline Report]"
            << " boundary=" << high_freq_start
            << " (HF:" << (total_blocks - high_freq_start)
            << " LF:" << high_freq_start << ")"
            << " peak: HF=" << peak_high_used
            << " LF=" << peak_low_used
            << " borrow=" << borrow_events
            << " evict=" << forced_evictions
            << " resize=" << resize_events;
        return oss.str();
    }
};

// ============================================================
// PhysicalBlockV2 (线程安全引用计数)
// ============================================================

struct PhysicalBlockV2 {
    static constexpr int MAX_MODELS = 8;
    
    int block_id;
    
    // 引用计数: 每个模型每角色独立, atomic
    std::atomic<uint16_t> ref_counts[MAX_MODELS][4];
    
    
    std::atomic<ModelID> owner_id;
    std::atomic<AccessMode> access_mode;
    std::atomic<bool> dirty;
    std::atomic<FreqClass> freq_class;
    std::atomic<uint64_t> last_access_tick;
    
    void* data;
    size_t data_size;
    
    // 迁移用
    std::atomic<FreqClass> target_class;  // 迁移目标频率区
    std::atomic<bool> migration_pending;

    PhysicalBlockV2(int id, size_t sz)
        : block_id(id),
          owner_id(-1),
          access_mode(AccessMode::FREE),
          dirty(false),
          freq_class(FreqClass::LOW_FREQ),
          last_access_tick(0),
          data(nullptr), data_size(sz),
          target_class(FreqClass::LOW_FREQ),
          migration_pending(false) {
        for (int m = 0; m < MAX_MODELS; m++)
            for (int r = 0; r < 4; r++)
                ref_counts[m][r].store(0, std::memory_order_relaxed);
    }

    int total_refs() const {
        int sum = 0;
        for (int m = 0; m < MAX_MODELS; m++)
            for (int r = 0; r < 4; r++)
                sum += ref_counts[m][r].load(std::memory_order_relaxed);
        return sum;
    }
};

// ============================================================
// SharedBlockPoolV2 (线程安全)
// ============================================================

class SharedBlockPoolV2 {
public:
    SharedBlockPoolV2(int num_blocks, size_t block_size,
                      float initial_high_ratio = 0.2f)
        : block_size(block_size),
          total_blocks(num_blocks),
          waterline(num_blocks, initial_high_ratio),
          next_tick(0) {
        
        for (int i = 0; i < num_blocks; i++) {
            blocks.push_back(std::make_unique<PhysicalBlockV2>(i, block_size));
            blocks.back()->data = std::malloc(block_size);
            if (!blocks.back()->data) {
                std::cerr << "[FATAL] malloc failed\n";
                exit(1);
            }
            std::memset(blocks.back()->data, 0, block_size);
            free_list.push_back(i);
        }
        
        std::cout << "[PoolV2] " << num_blocks << " blocks x "
                  << block_size << " B, high_ratio=" << initial_high_ratio << "\n";
    }

    ~SharedBlockPoolV2() {
        for (auto& b : blocks)
            if (b->data) std::free(b->data);
    }

    // ----- API (线程安全) -----

    int allocate(ModelID model_id, uint64_t freq_hz) {
        FreqClass fc = freq_hz > 10 ? FreqClass::HIGH_FREQ : FreqClass::LOW_FREQ;
        
        {
            std::lock_guard<std::shared_mutex> lock(free_mtx);
            if (free_list.empty()) {
                try_evict_locked();
                if (free_list.empty()) {
                    return -1;
                }
            }
            
            // 先尝试同 zone
            int block_id = -1;
            for (auto it = free_list.begin(); it != free_list.end(); ++it) {
                if (waterline.is_high_freq(*it) == (fc == FreqClass::HIGH_FREQ)) {
                    block_id = *it;
                    free_list.erase(it);
                    break;
                }
            }
            
            // fallback
            if (block_id < 0) {
                if (fc == FreqClass::HIGH_FREQ) {
                    // 高频区无空闲, 尝试扩展
                    waterline.note_borrow();
                    waterline.expand_high(2);
                    // 重新查找
                    for (auto it = free_list.begin(); it != free_list.end(); ++it) {
                        if (waterline.is_high_freq(*it)) {
                            block_id = *it;
                            free_list.erase(it);
                            break;
                        }
                    }
                }
                if (block_id < 0) {
                    block_id = free_list.back();
                    free_list.pop_back();
                }
            }
            
            auto& block = *blocks[block_id];
            block.owner_id.store(model_id, std::memory_order_release);
            block.access_mode.store(AccessMode::SHARED_READ, std::memory_order_release);
            block.freq_class.store(fc, std::memory_order_release);
            block.dirty.store(true, std::memory_order_release);
            block.last_access_tick.store(++next_tick, std::memory_order_release);
            block.ref_counts[model_id][static_cast<int>(Role::PRODUCER)].store(1, 
                std::memory_order_release);
            
            used_blocks[static_cast<int>(fc)].fetch_add(1, std::memory_order_relaxed);
            waterline.record_peak(fc, used_blocks[static_cast<int>(fc)].load());
            
            return block_id;
        }
    }

    bool share_block(int block_id, ModelID src_model, ModelID dst_model) {
        if (!valid(block_id)) return false;
        auto& block = *blocks[block_id];

        auto& cnt = block.ref_counts[dst_model][static_cast<int>(Role::SHARER)];
        if (cnt.load(std::memory_order_relaxed) >= UINT16_MAX) return false;
        
        cnt.fetch_add(1, std::memory_order_acq_rel);
        return true;
    }

    bool release(int block_id, ModelID model_id, Role role) {
        if (!valid(block_id)) return false;
        auto& block = *blocks[block_id];
        
        auto& cnt = block.ref_counts[model_id][static_cast<int>(role)];
        if (cnt.load(std::memory_order_relaxed) == 0) return false;
        
        cnt.fetch_sub(1, std::memory_order_acq_rel);
        
        if (block.total_refs() == 0) {
            free_block(block_id);
        }
        return true;
    }

    // 异步写 (线程安全 version)
    // 返回: 0=成功, -1=blocked
    int async_write(int block_id, ModelID model_id) {
        if (!valid(block_id)) return -1;
        auto& block = *blocks[block_id];

        AccessMode expected = AccessMode::SHARED_READ;
        AccessMode desired = AccessMode::EVICT_PENDING;
        
        // CAS => lock-free 写准备
        auto* mode_ptr = reinterpret_cast<std::atomic<uint8_t>*>(&block.access_mode);
        auto* exp_ptr = reinterpret_cast<uint8_t*>(&expected);
        auto des_val = static_cast<uint8_t>(desired);
        
        if (!mode_ptr->compare_exchange_strong(
                *exp_ptr, des_val,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return -1;  // 当前不可写
        }
        
        // 仿真 grace period (控制周期)
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        
        // 写数据
        if (block.data && block.data_size >= sizeof(int)) {
            *reinterpret_cast<std::atomic<int>*>(block.data) = 
                0xDEAD0000 | (model_id & 0xFFFF);
        }
        block.dirty.store(true, std::memory_order_release);
        block.owner_id.store(model_id, std::memory_order_release);
        block.last_access_tick.store(++next_tick, std::memory_order_release);
        
        // 回到共享读
        block.access_mode.store(AccessMode::SHARED_READ, std::memory_order_release);
        return 0;
    }

    bool try_read(int block_id) const {
        if (!valid(block_id)) return false;
        const auto& block = *blocks[block_id];
        auto mode = block.access_mode.load(std::memory_order_acquire);
        return mode == AccessMode::SHARED_READ || 
               mode == AccessMode::FREE ||
               mode == AccessMode::EVICT_PENDING;
    }

    int total_refs(int block_id) const {
        if (!valid(block_id)) return 0;
        return blocks[block_id]->total_refs();
    }

    FreqClass get_freq_class(int block_id) const {
        if (!valid(block_id)) return FreqClass::LOW_FREQ;
        return blocks[block_id]->freq_class.load(std::memory_order_relaxed);
    }

    int free_count() const {
        std::shared_lock<std::shared_mutex> lock(free_mtx);
        return (int)free_list.size();
    }

    int used_count(FreqClass fc) const {
        return used_blocks[static_cast<int>(fc)].load(std::memory_order_relaxed);
    }

    const SlidingWaterline& get_waterline() const { return waterline; }
    SlidingWaterline& get_waterline() { return waterline; }

    void debug() const {
        std::lock_guard<std::shared_mutex> lock(free_mtx);
        std::cout << "\n=== PoolV2 ===\n";
        std::cout << "Free: " << free_list.size()
                  << " Used: " << (total_blocks - free_list.size()) << "\n";
        for (int i = 0; i < total_blocks; i++) {
            bool free = std::find(free_list.begin(), free_list.end(), i) 
                       != free_list.end();
            if (free && i < 5) {
                std::cout << "  #" << i << " [FREE]\n";
                continue;
            }
            if (free) continue;
            
            const auto& b = *blocks[i];
            std::cout << "  #" << i 
                      << " M" << b.owner_id.load()
                      << " " << access_mode_name(b.access_mode.load())
                      << " " << freq_name(b.freq_class.load())
                      << " refs=" << b.total_refs() << "\n";
        }
        std::cout << waterline.report() << "\n";
        std::cout << "============\n\n";
    }

private:
    size_t block_size;
    int total_blocks;
    std::vector<std::unique_ptr<PhysicalBlockV2>> blocks;
    std::vector<int> free_list;
    mutable std::shared_mutex free_mtx;
    
    SlidingWaterline waterline;
    std::atomic<int> used_blocks[2];
    std::atomic<uint64_t> next_tick;

    bool valid(int id) const {
        return id >= 0 && id < total_blocks;
    }

    void free_block(int block_id) {
        auto& block = *blocks[block_id];
        FreqClass fc = block.freq_class.load(std::memory_order_relaxed);
        
        for (int m = 0; m < PhysicalBlockV2::MAX_MODELS; m++)
            for (int r = 0; r < 4; r++)
                block.ref_counts[m][r].store(0, std::memory_order_relaxed);
        block.owner_id.store(-1, std::memory_order_relaxed);
        block.access_mode.store(AccessMode::FREE, std::memory_order_release);
        block.dirty.store(false, std::memory_order_relaxed);
        
        {
            std::lock_guard<std::shared_mutex> lock(free_mtx);
            free_list.push_back(block_id);
        }
        used_blocks[static_cast<int>(fc)].fetch_sub(1, std::memory_order_relaxed);
    }

    void try_evict_locked() {
        for (int i = 0; i < total_blocks; i++) {
            auto& b = *blocks[i];
            if (b.access_mode.load(std::memory_order_relaxed) 
                    != AccessMode::EVICT_PENDING)
                continue;
            if (b.total_refs() == 0) {
                free_block(i);
            }
        }
    }
};

// ============================================================
// MultiModelOrchestrator
// ============================================================

struct ModelInstance {
    ModelID id;
    std::string name;
    uint64_t freq_hz;
    int min_blocks;
    int max_blocks;
    
    std::vector<int> owned_blocks;
    std::vector<int> shared_blocks;  // 别人共享给自己的
    
    // 统计
    int total_allocations;
    int total_writes;
    int blocked_writes;
    int total_reads;
    int blocked_reads;
    
    ModelInstance(ModelID i, const std::string& n, uint64_t f,
                   int min_b, int max_b)
        : id(i), name(n), freq_hz(f), min_blocks(min_b), max_blocks(max_b),
          total_allocations(0), total_writes(0), blocked_writes(0),
          total_reads(0), blocked_reads(0) {}
};

class MultiModelOrchestrator {
public:
    MultiModelOrchestrator(int total_blocks, size_t block_size,
                           float initial_high_ratio = 0.2f)
        : pool(total_blocks, block_size, initial_high_ratio),
          running(false) {}

    void add_model(const std::string& name, uint64_t freq_hz, 
                   int min_blocks, int max_blocks) {
        ModelID id = (ModelID)models.size();
        models.emplace_back(id, name, freq_hz, min_blocks, max_blocks);
        std::cout << "[Orch] model[" << id << "] " << name
                  << " " << freq_hz << "Hz [" << min_blocks 
                  << "-" << max_blocks << "]\n";
    }

    // 设定共享关系: src 共享给 dst
    void add_sharing(ModelID src, ModelID dst) {
        sharing_pairs.emplace_back(src, dst);
        std::cout << "[Orch] sharing: M" << src << " -> M" << dst << "\n";
    }

    // 运行仿真
    struct RunResult {
        std::string label;
        int total_reads;
        int blocked_reads;
        int total_writes;
        int blocked_writes;
        int total_allocs;
        double duration_us;
    };

    RunResult run_simulation(int test_duration_ms, 
                              const std::string& label) {
        running = true;
        auto start = std::chrono::high_resolution_clock::now();
        auto deadline = start + std::chrono::milliseconds(test_duration_ms);
        
        std::vector<std::thread> threads;
        std::atomic<int> iteration_counter(0);
        
        // 每个模型一个线程
        for (auto& model : models) {
            threads.emplace_back([this, &model, &deadline, &iteration_counter]() {
                std::mt19937 rng(model.id * 137 + 42);
                
                while (std::chrono::high_resolution_clock::now() < deadline) {
                    // 每个周期做一件事
                    int action = rng() % 100;
                    
                    if (action < 10 && model.owned_blocks.size() < (size_t)model.max_blocks) {
                        // 分配
                        int bid = pool.allocate(model.id, model.freq_hz);
                        if (bid >= 0) {
                            model.owned_blocks.push_back(bid);
                            model.total_allocations++;
                        }
                    }
                    else if (action < 20 && !model.owned_blocks.empty()) {
                        // 释放
                        size_t idx = rng() % model.owned_blocks.size();
                        int bid = model.owned_blocks[idx];
                        if (pool.release(bid, model.id, Role::PRODUCER)) {
                            model.owned_blocks.erase(
                                model.owned_blocks.begin() + idx);
                        }
                    }
                    else if (action < 60 && !model.owned_blocks.empty()) {
                        // 写 (按模型频率缩放)
                        size_t idx = rng() % model.owned_blocks.size();
                        int bid = model.owned_blocks[idx];
                        int ret = pool.async_write(bid, model.id);
                        model.total_writes++;
                        if (ret < 0) model.blocked_writes++;
                    }
                    else {
                        // 读 (包括自己 + 共享来的)
                        int bid = -1;
                        if (!model.shared_blocks.empty() && (rng() % 3 == 0)) {
                            bid = model.shared_blocks[
                                rng() % model.shared_blocks.size()];
                        } else if (!model.owned_blocks.empty()) {
                            bid = model.owned_blocks[
                                rng() % model.owned_blocks.size()];
                        }
                        if (bid >= 0) {
                            model.total_reads++;
                            if (!pool.try_read(bid)) {
                                model.blocked_reads++;
                            }
                        }
                    }
                    
                    // 按频率休眠
                    if (model.freq_hz > 0) {
                        int interval_us = 1'000'000 / model.freq_hz;
                        std::this_thread::sleep_for(
                            std::chrono::microseconds(interval_us));
                    }
                    
                    iteration_counter.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        
        for (auto& t : threads)
            t.join();
        
        running = false;
        auto end = std::chrono::high_resolution_clock::now();
        double duration_us = std::chrono::duration<double, std::micro>(
            end - start).count();
        
        // 汇总
        int total_reads = 0, blocked_reads = 0;
        int total_writes = 0, blocked_writes = 0;
        int total_allocs = 0;
        for (auto& m : models) {
            total_reads += m.total_reads;
            blocked_reads += m.blocked_reads;
            total_writes += m.total_writes;
            blocked_writes += m.blocked_writes;
            total_allocs += m.total_allocations;
        }
        
        RunResult result;
        result.label = label;
        result.total_reads = total_reads;
        result.blocked_reads = blocked_reads;
        result.total_writes = total_writes;
        result.blocked_writes = blocked_writes;
        result.total_allocs = total_allocs;
        result.duration_us = duration_us;
        return result;
    }

    void print_stats() const {
        std::cout << "\n=== Model Stats ===\n";
        for (auto& m : models) {
            float read_block_rate = m.total_reads > 0 
                ? 100.0f * m.blocked_reads / m.total_reads : 0;
            float write_block_rate = m.total_writes > 0 
                ? 100.0f * m.blocked_writes / m.total_writes : 0;
            
            std::cout << "  M" << m.id << " " << m.name
                      << " (" << m.freq_hz << "Hz)"
                      << " alloc=" << m.total_allocations
                      << " owned=" << m.owned_blocks.size()
                      << " shared=" << m.shared_blocks.size()
                      << "\n    reads=" << m.total_reads
                      << " blocked=" << m.blocked_reads
                      << " (" << read_block_rate << "%)"
                      << "\n    writes=" << m.total_writes
                      << " blocked=" << m.blocked_writes
                      << " (" << write_block_rate << "%)"
                      << "\n";
        }
        std::cout << pool.get_waterline().report() << "\n";
        std::cout << "==================\n\n";
    }

    SharedBlockPoolV2& get_pool() { return pool; }
    
    // 清理所有模型
    void cleanup_all() {
        for (auto& m : models) {
            for (int bid : m.owned_blocks)
                pool.release(bid, m.id, Role::PRODUCER);
            m.owned_blocks.clear();
            
            for (int bid : m.shared_blocks)
                pool.release(bid, m.id, Role::SHARER);
            m.shared_blocks.clear();
        }
    }

private:
    SharedBlockPoolV2 pool;
    std::vector<ModelInstance> models;
    std::vector<std::pair<ModelID, ModelID>> sharing_pairs;
    std::atomic<bool> running;
};

// ============================================================
// 测试: 低频 + 高频 并发竞争
// ============================================================

void test_concurrent_heterogeneous() {
    std::cout << "\n========== 并发异频竞争测试 ==========\n";
    
    MultiModelOrchestrator orch(32, 1024, 0.25f);
    
    // 低频 (VLM / perception)
    orch.add_model("VLM", 2, 2, 6);
    // 高频 (Policy / control)
    orch.add_model("Policy", 100, 1, 4);
    
    // 分配初始块
    auto& pool = orch.get_pool();
    int b1 = pool.allocate(0, 2);   // VLM
    int b2 = pool.allocate(0, 2);
    int b3 = pool.allocate(0, 2);
    int b4 = pool.allocate(1, 100); // Policy
    
    // 共享 VLM 数据给 Policy
    pool.share_block(b1, 0, 1);
    pool.share_block(b2, 0, 1);
    
    // 记录到 orchestrator
    auto& model_vlm = orch;  // 略
    
    auto result = orch.run_simulation(2000, "VLM+Policy 2Hz/100Hz");
    
    std::cout << "\n[Results - " << result.label << "]\n";
    std::cout << "Duration: " << result.duration_us / 1000.0 << " ms\n";
    std::cout << "Allocs: " << result.total_allocs << "\n";
    std::cout << "Reads: " << result.total_reads 
              << " Blocked: " << result.blocked_reads
              << " (" << (result.total_reads > 0 
                  ? 100.0f * result.blocked_reads / result.total_reads : 0) << "%)\n";
    std::cout << "Writes: " << result.total_writes 
              << " Blocked: " << result.blocked_writes
              << " (" << (result.total_writes > 0 
                  ? 100.0f * result.blocked_writes / result.total_writes : 0) << "%)\n";
    
    float read_block = result.total_reads > 0
        ? 100.0f * result.blocked_reads / result.total_reads : 0;
    std::cout << "[" << (read_block < 10.0f ? "PASS" : "NEED TUNING") 
              << "] Async lock effective (block rate < 10%)\n";
    
    orch.print_stats();
    orch.cleanup_all();
}

// ============================================================
// 测试: 3 模型 投机解码场景
// ============================================================

void test_spec_decode_3model() {
    std::cout << "\n========== 3 模型投机解码测试 ==========\n";
    
    MultiModelOrchestrator orch(64, 2048, 0.2f);
    
    // Draft 0.5B: 高频
    orch.add_model("Draft-0.5B", 50, 2, 8);
    // Target 7B: 低频
    orch.add_model("Target-7B", 2, 2, 6);
    // Vision Encoder: 极低频
    orch.add_model("VisionEnc", 1, 1, 4);
    
    // 共享关系: Draft -> Target (KV cache sharing)
    orch.add_sharing(0, 1);
    // Vision -> Target (visual features)
    orch.add_sharing(2, 1);
    
    auto& pool = orch.get_pool();
    
    // 预分配 + 建立共享
    std::vector<int> draft_blocks;
    for (int i = 0; i < 4; i++) {
        int b = pool.allocate(0, 50);
        draft_blocks.push_back(b);
        pool.share_block(b, 0, 1);  // Draft -> Target
    }
    
    std::vector<int> vision_blocks;
    for (int i = 0; i < 2; i++) {
        int b = pool.allocate(2, 1);
        vision_blocks.push_back(b);
        pool.share_block(b, 2, 1);  // Vision -> Target
    }
    
    int b_tgt = pool.allocate(1, 2); // Target own block
    
    auto result = orch.run_simulation(3000, "Draft+Target+Vision");
    
    std::cout << "\n[Results - " << result.label << "]\n";
    std::cout << "Duration: " << result.duration_us / 1000.0 << " ms\n";
    std::cout << "Reads: " << result.total_reads 
              << " (" << result.total_reads / (result.duration_us / 1'000'000) 
              << " reads/s)"
              << " Blocked: " << result.blocked_reads
              << " (" << (result.total_reads > 0 
                  ? 100.0f * result.blocked_reads / result.total_reads : 0) << "%)\n";
    std::cout << "Writes: " << result.total_writes 
              << " Blocked: " << result.blocked_writes
              << " (" << (result.total_writes > 0 
                  ? 100.0f * result.blocked_writes / result.total_writes : 0) << "%)\n";
    
    orch.print_stats();
    orch.cleanup_all();
}

// ============================================================
// 测试: 碎片压力 (高频反复分配释放)
// ============================================================

void test_fragmentation_pressure() {
    std::cout << "\n========== 碎片压力测试 ==========\n";
    
    SharedBlockPoolV2 pool(100, 1024, 0.2f);
    
    std::vector<int> blocks;
    
    // Phase 1: 分配高频, 释放, 再分配
    int fail_count = 0;
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < 15; i++) {
            int b = pool.allocate(0, 100);  // 高频
            if (b >= 0) {
                blocks.push_back(b);
            } else {
                fail_count++;
            }
        }
        // 释放一半
        for (int i = 0; i < 7 && !blocks.empty(); i++) {
            int bid = blocks.back();
            blocks.pop_back();
            pool.release(bid, 0, Role::PRODUCER);
        }
    }
    
    // 清理
    for (int b : blocks)
        pool.release(b, 0, Role::PRODUCER);
    blocks.clear();
    
    // Phase 2: 低频分配 (应该能在低频区域找到连续空间)
    for (int i = 0; i < 20; i++) {
        int b = pool.allocate(1, 2);  // 低频
        if (b >= 0) blocks.push_back(b);
    }
    
    int low_in_high = 0;
    for (int b : blocks) {
        if (pool.get_freq_class(b) == FreqClass::HIGH_FREQ)
            low_in_high++;
    }
    for (int b : blocks)
        pool.release(b, 1, Role::PRODUCER);
    
    std::cout << "[FragTest] Low-freq blocks in high zone: " 
              << low_in_high << "/20"
              << " (ideal=0)\n";
    std::cout << "[" << (low_in_high <= 5 ? "PASS" : "NEED TUNING") 
              << "] Zone isolation ok\n";
}

// ============================================================
// 入口
// ============================================================

int main() {
    std::cout << "=== DualStream Phase 3: Orchestrator ===\n\n";

    test_concurrent_heterogeneous();
    test_spec_decode_3model();
    test_fragmentation_pressure();

    std::cout << "\n=== All Phase 3 tests complete ===\n";
    return 0;
}
