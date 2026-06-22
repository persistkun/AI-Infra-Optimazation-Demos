#include <iostream>
#include <vector>
#include <cstdint>
#include <cassert>
#include <cmath>

// 模拟 FP16 半精度浮点数（C++标准用 float 模拟其数值，但物理上占用2字节，这里简化演示）
typedef float fp16_t; 

class HeterogeneousQuantEngine {
public:
    // 💥 任务：手撕 FP4 到 FP16 的反量化与 Stride 指针转换内核
    // fp4_compressed_data: 物理显存中连续存储的 FP4 数据（1个字节包含2个FP4元素）
    // num_elements: 需要转换的元素总数
    // scale: 量化缩放因子
    static std::vector<fp16_t> dequantize_fp4_to_fp16(
        const std::vector<uint8_t>& fp4_compressed_data, 
        int num_elements, 
        fp16_t scale) 
    {
        std::vector<fp16_t> fp16_output(num_elements, 0.0f);

        // 【TODO 1】精准控制指针与 Stride
        // 因为 1 个字节包含 2 个 FP4 元素，所以循环变量和 Stride 的计算至关重要
        for (int i = 0; i < num_elements; ++i) {
            // 1. 找到当前元素落在 fp4_compressed_data 的第几个字节（byte_idx）
            int byte_idx = i / 2;
            
            // 2. 判断当前元素是该字节的高 4 位（High nibble）还是低 4 位（Low nibble）
            bool is_high = (i % 2 == 0);

            uint8_t raw_byte = fp4_compressed_data[byte_idx];
            uint8_t fp4_val = 0;

            // 【TODO 2】手撕位运算，提取 4-bit 原始数据
            if (is_high) {
                // 提取高 4 位，并右移到低位
                fp4_val = (raw_byte >> 4) & 0x0F;
            } else {
                // 提取低 4 位
                fp4_val = raw_byte & 0x0F;
            }

            // 【TODO 3】反量化映射
            // 模拟简单的线性反量化：fp16 = fp4_val * scale
            // 真实情况（如文中的E2M1）会更复杂，这里要求写出核心转换射影
            fp16_output[i] = static_cast<fp16_t>(fp4_val) * scale;
        }

        return fp16_output;
    }
};

int main() {
    std::cout << "[Heterogeneous Quant] 启动第 9 题异构转换内核测试..." << std::endl;

    // 模拟物理块池中被压缩的 FP4 连续内存
    // 假设有 4 个元素，物理量化值分别为: [12, 5, 8, 2]
    // 字节 0 存储 [12, 5] -> 12 占高4位 (12 << 4 = 192)，5 占低4位 (5) -> 192 + 5 = 197 (0xC5)
    // 字节 1 存储 [8, 2]  -> 8  占高4位 (8 << 4 = 128)，2 占低4位 (2) -> 128 + 2 = 130 (0x82)
    std::vector<uint8_t> fp4_pool = {0xC5, 0x82}; 
    int num_elements = 4;
    fp16_t scale = 0.5f; // 缩放因子

    // 执行低比特反量化转换
    std::vector<fp16_t> restored_kv = HeterogeneousQuantEngine::dequantize_fp4_to_fp16(fp4_pool, num_elements, scale);

    std::cout << "反量化还原后的 FP16 张量数据: ";
    for (float val : restored_kv) {
        std::cout << val << " ";
    }
    std::cout << std::endl;

    // 💥 严格断言验证：
    // 元素 0: 12 * 0.5 = 6.0
    // 元素 1: 5  * 0.5 = 2.5
    // 元素 2: 8  * 0.5 = 4.0
    // 元素 3: 2  * 0.5 = 1.0
    assert(std::abs(restored_kv[0] - 6.0f) < 1e-5);
    assert(std::abs(restored_kv[1] - 2.5f) < 1e-5);
    assert(std::abs(restored_kv[2] - 4.0f) < 1e-5);
    assert(std::abs(restored_kv[3] - 1.0f) < 1e-5);

    std::cout << "🎉 恭喜！第 9 题异构混合精度指针转换内核顺利通关！" << std::endl;
    return 0;
}