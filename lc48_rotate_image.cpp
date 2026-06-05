#include <iostream>
#include <vector>//动态数组,随便存多少,自动扩容,自动释放内存
#include <algorithm>

class TensorOptimizer {
public:
    // 旋转多维矩阵：传引用 (std::vector<std::vector<int>>&)，杜绝海量数据拷贝
    //::从std标准库里拿东西,&matrix是引用,不复制,直接操作原有数据
    void rotate_matrix_inplace(std::vector<std::vector<int>>& matrix) {
        int n = matrix.size();
        if (n <= 1) return;

        // --- 核心算法：转置 (Transpose) + 左右翻转 (Flip) = 顺时针旋转 90 度 ---
        
        // 1. 沿主对角线转置：把行变成列
        // 工业底层逻辑：让内存访问尽可能在局部连续（利用 CPU/GPU 的 Cache L1 行缓存）
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                // 就地交换，不申请额外内存，空间复杂度 O(1)
                std::swap(matrix[i][j], matrix[j][i]);
            }
        }

        // 2. 每一行进行左右对称翻转
        for (int i = 0; i < n; ++i) {
            // 使用双指针在原内存块两头往中间逼近
            int left = 0;
            int right = n - 1;
            while (left < right) {
                std::swap(matrix[i][left], matrix[i][right]);
                left++;
                right--;
            }
        }
    }
};

// 辅助测试函数：用来打印矩阵验证结果
void print_tensor(const std::vector<std::vector<int>>& matrix) {
    for (const auto& row : matrix) {
        for (int val : row) {
            std::cout << val << "\t";
        }
        std::cout << "\n";
    }
}

int main() {
    std::cout << "---LeetCode 48 矩阵就地翻转演练 ---\n\n";

    // 模拟一个 3x3 的图像像素特征矩阵（张量）
    std::vector<std::vector<int>> dummy_tensor = {
        {1, 2, 3},
        {4, 5, 6},
        {7, 8, 9}
    };

    std::cout << "📦 原始矩阵布局:\n";
    print_tensor(dummy_tensor);

    // 调用优化器进行压榨旋转
    TensorOptimizer optimizer;
    optimizer.rotate_matrix_inplace(dummy_tensor);

    std::cout << "\n🔄 经过 O(1) 空间就地旋转 90 度后的矩阵布局:\n";
    print_tensor(dummy_tensor);

    return 0;
}