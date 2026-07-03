# AI-Infra-Optimazation-Demos

Learning & prototyping repo for LLM inference system internals — KV cache management, speculative decoding, continuous batching, and multi-model orchestration, all implemented from scratch in C++.

## What's inside

### Core modules (3 files, ~2000 lines)

| File | Lines | What it does |
|------|-------|-------------|
| `mini_vllm_mem.cpp` | 383 | Page-level KV cache manager with per-role reference counting (Draft/Target/Policy/Evictor). Allocate, AddRef, Release, MarkEvictPending. Tested with speculative decoding zero-copy sharing, VLM+Policy co-inference, and OOM recovery scenarios. |
| `shared_block_pool.cpp` | 703 | Shared memory pool with async page lock (atomic read, CAS write + grace period) and frequency-aware zone allocator (>10Hz → HIGH_FREQ, ≤10Hz → LOW_FREQ). Tested with 100Hz+2Hz heterogeneous workload. |
| `dualstream_orchestrator.cpp` | 938 | Multi-model orchestrator with per-model std::thread, sliding waterline for dynamic zone resizing (>85% expand, <50% shrink), and thread-safe SharedBlockPoolV2. Tested with 3-model concurrent speculative decoding. |

### Inference kernels (6 files)

| File | Lines | What it does |
|------|-------|-------------|
| `lc_rmsnorm_base.cpp` | 60 | RMSNorm forward pass with inv_rms optimization (div→mul) |
| `lc_heterogeneous_quant.cpp` | 86 | FP4→FP16 dequantization via bit-level nibble extraction and stride pointer conversion |
| `lc_speculative_verification.cpp` | 85 | Speculative decoding token verification + KV cache rollback (bonus token injection) |
| `lc_speculative_ref_count.cpp` | 74 | Multi-mode reference counting state machine (HOT_FP16→COLD_FP4→FREE) |
| `lc_continuous_batching.cpp` | 111 | Iteration-level continuous batching scheduler (WAITING→RUNNING→FINISHED) |
| `lc_tensor_transpose.cpp` | 88 | 4D tensor transpose [B,S,N,H]→[B,N,S,H] with explicit stride mapping |

### Data structures (4 files)

| File | Lines | What it does |
|------|-------|-------------|
| `lc146_lru_cache.cpp` | 121 | LRU cache (doubly linked list + hashmap, O(1) get/put) for KV cache eviction |
| `lc706_design_hashmap.cpp` | 93 | Custom hashmap (prime bucket 7919 + chaining) for token→block address mapping |
| `lc23_merge_k_list.cpp` | 82 | K-way merge via min-heap for multi-stream request scheduling |
| `lc48_rotate_image.cpp` | 68 | In-place matrix rotation (transpose + flip, O(1) space) |

## Build & run

Each `.cpp` file is self-contained. Compile and run individually:

```bash
g++ -std=c++17 -O2 mini_vllm_mem.cpp -o mini_vllm_mem && ./mini_vllm_mem
g++ -std=c++17 -O2 shared_block_pool.cpp -o shared_block_pool && ./shared_block_pool
g++ -std=c++17 -O2 dualstream_orchestrator.cpp -o dualstream_orchestrator && ./dualstream_orchestrator
g++ -std=c++17 -O2 lc_rmsnorm_base.cpp -o lc_rmsnorm_base && ./lc_rmsnorm_base
g++ -std=c++17 -O2 lc_heterogeneous_quant.cpp -o lc_heterogeneous_quant && ./lc_heterogeneous_quant
g++ -std=c++17 -O2 lc_speculative_verification.cpp -o lc_speculative_verification && ./lc_speculative_verification
g++ -std=c++17 -O2 lc_speculative_ref_count.cpp -o lc_speculative_ref_count && ./lc_speculative_ref_count
g++ -std=c++17 -O2 lc_continuous_batching.cpp -o lc_continuous_batching && ./lc_continuous_batching
g++ -std=c++17 -O2 lc_tensor_transpose.cpp -o lc_tensor_transpose && ./lc_tensor_transpose
g++ -std=c++17 -O2 lc146_lru_cache.cpp -o lc146_lru_cache && ./lc146_lru_cache
g++ -std=c++17 -O2 lc706_design_hashmap.cpp -o lc706_design_hashmap && ./lc706_design_hashmap
g++ -std=c++17 -O2 lc23_merge_k_list.cpp -o lc23_merge_k_list && ./lc23_merge_k_list
g++ -std=c++17 -O2 lc48_rotate_image.cpp -o lc48_rotate_image && ./lc48_rotate_image
```

All 13 modules compile and pass their built-in assertions.

## About

Built from scratch as a learning project to understand how LLM inference systems manage GPU memory and schedule concurrent model workloads. The multi-role reference counting and frequency-aware allocation ideas are explored further in the DualStream paper (submitted to JSA, under review).

**Author:** Changkun Wang  
**Timeline:** May – July 2026  
**Status:** 13/13 modules compiled & tested
