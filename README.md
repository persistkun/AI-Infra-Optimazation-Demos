# AI-Infra-Optimazation-Demos

大模型推理系统底层学习项目 —  KV Cache 管理、投机解码、Continuous Batching、多模型并发编排，从零用 C++ 实现。

Learning project for LLM inference system internals: KV cache management, speculative decoding, continuous batching, and multi-model orchestration — all from scratch in C++.

## 项目结构 / What's inside

### 核心模块 / Core modules (3 files, ~2000 lines)

| File | Lines | 说明 / Description |
|------|-------|-------------------|
| `mini_vllm_mem.cpp` | 383 | 多角色页级引用计数 KV Cache 管理器。PhysicalBlock 维护 Draft/Target/Policy/Evictor 四角色独立引用计数，实现 Allocate/AddRef/Release/MarkEvictPending 四个操作。测试场景：投机解码零拷贝共享、VLM+Policy 协同推理、OOM 恢复。 |
| `shared_block_pool.cpp` | 703 | 共享显存池 + 频率感知分区。AsyncPageLock：读路径 atomic load 无锁，写路径 CAS + Grace Period。FrequencyZoneAllocator 按频率（>10Hz / ≤10Hz）分区。测试 100Hz+2Hz 异频竞争。 |
| `dualstream_orchestrator.cpp` | 938 | 多模型并发编排器 + 滑动水位线。每模型独立 std::thread，SlidingWaterline 按利用率（>85% 扩展 / <50% 压缩）动态调整分区。测试三模型并发（Draft 50Hz + Target 2Hz + Vision 1Hz）。 |

### 推理算子 / Inference kernels (6 files)

| File | Lines | 说明 / Description |
|------|-------|-------------------|
| `lc_rmsnorm_base.cpp` | 60 | RMSNorm 前向传播，inv_rms 优化（除法→乘法） |
| `lc_heterogeneous_quant.cpp` | 86 | FP4→FP16 反量化，位运算提取 nibble + Stride 指针转换 |
| `lc_speculative_verification.cpp` | 85 | 投机解码 token 验证 + KV Cache 残差回滚（Bonus Token 注入） |
| `lc_speculative_ref_count.cpp` | 74 | 多模式引用计数流转状态机（HOT_FP16→COLD_FP4→FREE 三级） |
| `lc_continuous_batching.cpp` | 111 | Continuous Batching 调度器，迭代级状态机（WAITING→RUNNING→FINISHED）+ 动态插队 |
| `lc_tensor_transpose.cpp` | 88 | 4D 张量转置 [B,S,N,H]→[B,N,S,H]，显式 stride 计算 |

### 数据结构练习 / Data structures (4 files)

| File | Lines | 说明 / Description |
|------|-------|-------------------|
| `lc146_lru_cache.cpp` | 121 | LRU Cache（双向链表 + 哈希表，O(1)），用于 KV Cache 淘汰 |
| `lc706_design_hashmap.cpp` | 93 | 自实现 HashMap（质数桶 7919 + 链地址法），Token→显存块地址映射 |
| `lc23_merge_k_list.cpp` | 82 | 小顶堆 K 路归并，模拟多用户请求流调度 |
| `lc48_rotate_image.cpp` | 68 | 矩阵就地旋转 90°（转置 + 翻转，O(1) 额外空间） |

## 编译运行 / Build & run

每个 `.cpp` 文件独立编译运行：

```bash
g++ -std=c++17 -O2 mini_vllm_mem.cpp -o mini_vllm_mem && ./mini_vllm_mem
g++ -std=c++17 -O2 shared_block_pool.cpp -o shared_block_pool && ./shared_block_pool
g++ -std=c++17 -O2 dualstream_orchestrator.cpp -o dualstream_orchestrator && ./dualstream_orchestrator
# ... (same for all 13 files)
```

全部 13 个模块编译通过，内置 assert 测试全部 PASS。

All 13 modules compile and pass their built-in assertions.

## 关于 / About

从零开始的学习项目，目的是理解大模型推理系统如何在 GPU 显存上管理 KV Cache 并调度多个模型并发运行。多角色引用计数和频率感知分配的想法在 DualStream 论文中有进一步探讨（独立作者，已投稿 JSA，Under Review）。

Built from scratch to understand how LLM inference systems manage GPU memory and schedule concurrent model workloads.

**作者 / Author:** Changkun Wang  
**时间 / Timeline:** 2026.05 – 2026.07  
**状态 / Status:** 13/13 模块编译通过
