# DualStream — Preprint

**Title:** DualStream: A Shared KV Runtime for Self-Speculative Decoding on Edge GPUs

**Author:** Changkun Wang (独立作者)

**Status:** Submitted to *Journal of Systems Architecture* (Elsevier, SCI 二区), 2026.06.26 — Under Review

**Files:**
- `dualstream_preprint_jsa.pdf` — 投稿版论文 PDF
- `dualstream_jsa.tex` — LaTeX 源文件
- `references.bib` — 参考文献 BibTeX
- `paper_jsa.bbl` — 编译后的参考文献

---

## 关于本文 / About

本文提出 DualStream，一个面向端侧 GPU 的多模型 KV Cache 共享推理运行时。核心思路是利用多角色页级引用计数实现 Draft 模型和 Target 模型之间的零拷贝 KV Cache 共享，配合频率感知分区分配器减少碎片，在 RTX 5070 上实测 OOM 窗口扩展平均 1.87×，端到端吞吐较 Eagle 分离式方案提升 97%。

This paper proposes DualStream, a shared KV cache runtime for multi-model LLM inference on edge GPUs. Key ideas include per-role page-level reference counting for zero-copy KV cache sharing between draft and target models, and a frequency-aware zone allocator to reduce fragmentation. Measured on RTX 5070: 1.87× average OOM window expansion, 97% throughput improvement over Eagle's separate execution.

## C++ 原型 / C++ Prototype

本仓库根目录的 C++ 代码是对论文核心机制的工程验证，包括多角色引用计数显存管理器、SharedBlockPool、多模型并发编排器三个层次。详见仓库 [README](../README.md)。

The C++ code in this repo is a prototype implementation of the paper's core mechanisms. See the repo [README](../README.md) for details.
