#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>

class SpeculativeEngine {
public:
    // 💥 完美收网：手撕投机解码验证与 KV Cache 残差回滚内核
    // draft_tokens: 小模型猜的 K 个 Token
    // verify_predictions: 大模型并行验证吐出的预测（长度为 K+1，包含最后纠正的一个）
    // kv_cache_len: 当前请求在物理池里的物理 KV Cache 长度（传入引用，需要动态回滚修改）
    static std::vector<int> verify_and_rollback(
        const std::vector<int>& draft_tokens, 
        const std::vector<int>& verify_predictions, 
        int& kv_cache_len) 
    {
        std::vector<int> accepted_tokens;
        int K = draft_tokens.size();
        int actual_accepted_count = 0;

        // 1. 【TODO 1 完美填满】逐个比对投机 Token
        // 大模型对第 i 个投机 Token 的预期正确答案，在 verify_predictions[i]
        for (int i = 0; i < K; ++i) {
            if (draft_tokens[i] == verify_predictions[i]) {
                // 小模型猜对了！临时贴入接受列表，增加计数
                accepted_tokens.push_back(draft_tokens[i]);
                actual_accepted_count++;
            } else {
                // 💥 小模型翻车！拒绝采样失败，立刻中断后续验证（因为后面都是基于错误 Token 投机的）
                break;
            }
        }

        // 2. 【TODO 2 完美填满】大模型强行纠偏（Bonus Token 注入）
        // 无论小模型全对还是中途翻车，大模型在失败位（索引等于 actual_accepted_count）
        // 都已经通过一次 Forward 并行算好了绝对正确的下一个 Token。
        int bonus_token = verify_predictions[actual_accepted_count];
        accepted_tokens.push_back(bonus_token);

        // 3. 【TODO 3 完美填满】物理显存 KV Cache 残差回滚
        // 算法公式：修正后的长度 = 初始长度 + 实际接受的投机数 + 1个纠正Bonus Token
        // 注意：传入的 kv_cache_len 已经是被小模型强行推高 K 之后的错误状态！
        int initial_len = kv_cache_len - K; // 先逆向推出投机前的初始基础长度
        kv_cache_len = initial_len + actual_accepted_count + 1;

        return accepted_tokens;
    }
};

int main() {
    // 模拟小模型一口气无脑猜了 4 个 Token (K = 4)
    std::vector<int> draft_tokens = {102, 57, 89, 301};

    // 模拟大模型并行的验证预测结果（长度为 K+1 = 5）
    // 索引 0: 验证 draft[0](102)，大模型认为是 102 (对)
    // 索引 1: 验证 draft[1](57)，大模型认为是 57  (对)
    // 索引 2: 验证 draft[2](89)，大模型认为是 999 (错！小模型在这里猜错了！)
    // 索引 3: 后面的验证失效，但大模型根据 999 并行算出了下一个绝对正确的 Token 是 44
    std::vector<int> verify_predictions = {102, 57, 999, 44, 12};

    // 假设在这一轮投机迭代开始前，基础长度是 100
    // 小模型投机时，把 KV Cache 预先强行推到了 100 + 4 = 104
    int kv_cache_len = 104;

    std::cout << "[Speculative Verification] 启动第 8 题投机验证与回滚内核测试..." << std::endl;

    // 执行裁决与回滚
    std::vector<int> final_tokens = SpeculativeEngine::verify_and_rollback(draft_tokens, verify_predictions, kv_cache_len);

    std::cout << "验证结果:" << std::endl;
    std::cout << " 实际接受的 Token 序列: ";
    for (int t : final_tokens) std::cout << t << " ";
    std::cout << "\n 修正后的物理 KV Cache 长度: " << kv_cache_len << std::endl;

    // 💥 严格断言：
    // 应该接受: 102 (对), 57 (对), 以及大模型纠偏的 999 (大模型纠正值)，共 3 个 Token
    assert(final_tokens.size() == 3);
    assert(final_tokens[2] == 999);
    
    // KV Cache 物理长度应该从 104 正确回滚到：100 (基础) + 2 (猜对的) + 1 (大模型纠正的) = 103！
    assert(kv_cache_len == 103);

    std::cout << "🎉 恭喜！第 8 题投机解码残差回滚内核顺利通关！" << std::endl;
    return 0;
}