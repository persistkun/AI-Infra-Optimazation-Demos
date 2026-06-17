#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>

// 模拟大模型底层的 4D 张量（Tensor）
class Tensor4D {
public:
    int batch, seq_len, num_heads, head_dim;
    std::vector<float> data; // 物理上是一维连续存储的

    Tensor4D(int b, int s, int n, int h) 
        : batch(b), seq_len(s), num_heads(n), head_dim(h) {
        // 分配总内存大小
        data.resize(batch * seq_len * num_heads * head_dim);
        // 初始化数据，填充自增数字以便验证重排是否正确
        for (size_t i = 0; i < data.size(); ++i) {
            data[i] = static_cast<float>(i);
        }
    }

    // 💥 任务 1：实现朴素的高维到一维物理内存的 Offset 映射
    // 布局为标准行优先 (Row-Major)：[b, s, n, h]
    int get_index(int b, int s, int n, int h) const {
        // 每一维的下标乘以它后面所有维度的长度之积
        return b * (seq_len * num_heads * head_dim) 
             + s * (num_heads * head_dim) 
             + n * (head_dim) 
             + h;
    }
};

// 张量重排引擎
class TensorEngine {
public:
    static Tensor4D transpose_BSNH_to_BNSH(const Tensor4D& src) {
        // 💥 修正：传参时就必须把 src.num_heads 和 src.seq_len 的位置对调！
        // 这样目标张量的物理布局才是正宗的 [B, N, S, H]
        Tensor4D dst(src.batch, src.num_heads, src.seq_len, src.head_dim);

        // 开始重排流转
        for (int b = 0; b < src.batch; ++b) {
            for (int s = 0; s < src.seq_len; ++s) {
                for (int n = 0; n < src.num_heads; ++n) {
                    for (int h = 0; h < src.head_dim; ++h) {
                        // 1. 算出源张量在 [B, S, N, H] 布局下的物理位置
                        int src_idx = src.get_index(b, s, n, h);

                        // 2. 算出目标张量在 [B, N, S, H] 布局下的物理位置
                        // 此时 dst.batch=B, dst.seq_len=N, dst.num_heads=S, dst.head_dim=H
                        // 对应公式：b * (N * S * H) + n * (S * H) + s * H + h
                        int dst_idx = b * (dst.seq_len * dst.num_heads * dst.head_dim)
                                    + n * (dst.num_heads * dst.head_dim)
                                    + s * (dst.head_dim)
                                    + h;

                        // 3. 内存搬运
                        dst.data[dst_idx] = src.data[src_idx];
                    }
                }
            }
        }
        return dst;
    }
};

int main() {
    // BatchSize=2, SeqLen=3, NumHeads=4, HeadDim=2
    Tensor4D input_tensor(2, 3, 4, 2);
    
    std::cout << "[TensorEngine] 原始张量分配成功，大小: " << input_tensor.data.size() << " floats." << std::endl;

    // 执行重排
    Tensor4D output_tensor = TensorEngine::transpose_BSNH_to_BNSH(input_tensor);

    // 验证边界测试点
    int src_val = input_tensor.data[input_tensor.get_index(1, 2, 3, 1)];
    
    std::cout << "验证物理地址映射..." << std::endl;
    std::cout << "源真实数据值 (input[1, 2, 3, 1]): " << src_val << std::endl;
    std::cout << "目标重排数据值 (output[47]): " << output_tensor.data[47] << std::endl;
    
    // 打开断言
    assert(output_tensor.data[47] == src_val); 
    
    std::cout << "🎉 恭喜！第 5 题张量重排内核地基测试通过！" << std::endl;
    return 0;
}