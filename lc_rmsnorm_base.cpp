#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>

class RMSNormEngine {
public:
    // 💥 完美收网：手撕大模型单层 RMSNorm 前向传播核心
    // 公式: y = (x / RMS(x)) * gamma
    // 其中 RMS(x) = sqrt( (1/N) * sum(x_i^2) + epsilon )
    static void rmsnorm_forward(const float* input, float* output, const float* gamma, int hidden_dim, float eps = 1e-5f) {
        
        // 1. 计算当前隐藏维（hidden_dim）所有特征的平方和 (Sum of Squares)
        float square_sum = 0.0f;
        for (int i = 0; i < hidden_dim; ++i) {
            square_sum += input[i] * input[i];
        }

        // 2. 计算均方根 RMS，并直接优化为逆因子（inv_rms = 1.0 / RMS）
        // 这样可以把第 3 步循环里的“除法”全部变成“乘法”，暴压时钟周期
        float mean_square = square_sum / static_cast<float>(hidden_dim);
        float inv_rms = 1.0f / std::sqrt(mean_square + eps); 

        // 3. 执行归一化并乘以可学习参数 gamma (Element-wise Scale)
        for (int i = 0; i < hidden_dim; ++i) {
            // y_i = (x_i * inv_rms) * gamma_i
            output[i] = (input[i] * inv_rms) * gamma[i];
        }
    }
};

int main() {
    // 模拟大模型标准隐藏层维度（Hidden Dimension），比如 Llama-7B 的某个小切片
    const int hidden_dim = 8;
    
    // 模拟输入 Tensor 的一行数据 (1个 Token 的 Hidden States)
    std::vector<float> input = {1.0f, 2.0f, 3.0f, 4.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> output(hidden_dim, 0.0f);
    
    // 模拟可学习的缩放权重 Gamma（初始化全为 1.0，即不做额外缩放）
    std::vector<float> gamma(hidden_dim, 1.0f);

    std::cout << "[RMSNorm] 启动第 6 题核心算子地基测试..." << std::endl;

    // 执行手撕的 RMSNorm
    RMSNormEngine::rmsnorm_forward(input.data(), output.data(), gamma.data(), hidden_dim);

    // 验证测试点：
    // 输入平方和 = 1+4+9+16+1+4+9+16 = 60
    // 平均平方和 = 60 / 8 = 7.5
    // RMS = sqrt(7.5 + 1e-5) ≈ 2.738612
    // 第一个输出 output[0] = 1.0 / 2.738612 ≈ 0.365148
    std::cout << "验证算子输出结果精度..." << std::endl;
    std::cout << "output[0] 真实计算值: " << output[0] << " (预期接近 0.365148)" << std::endl;

    // 工业级严格断言精度边界（防止浮点数微小误差，用 abs 差值判断）
    assert(std::abs(output[0] - 0.365148f) < 1e-4f);

    std::cout << "🎉 恭喜！第 6 题 RMSNorm 算子物理底座顺利通关！" << std::endl;
    return 0;
}