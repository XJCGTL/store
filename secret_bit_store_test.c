#include <am.h>
#include <klib.h>

#define PAGE_SIZE 4096
#define LOOP_COUNT 100  // 循环次数以平滑抖动
#define WARMUP_RUNS 2   // 预热运行次数
#define TRAIN_RUNS 100  // 分支预测器训练次数（需足够多以稳定训练预测器）
#define TEST_RUNS 5     // 正式测试运行次数

// 秘密比特串（静态变量）
// 第一位（LSB）: 0 -> 执行对齐地址的sw; 1 -> 执行未对齐地址的sw
static uint32_t secret_bits = 0b00000001;

// 读取周期计数器
static inline uint64_t read_cycles() {
    uint64_t val;
    asm volatile("rdcycle %0" : "=r"(val));
    return val;
}

// 全局地址变量，防止编译器优化掉地址计算
volatile uint32_t *g_misalign_addr;        // 固定的未对齐地址（延时链用）
volatile uint32_t *g_aligned_addr;         // 对齐地址（秘密位为0时使用）
volatile uint32_t *g_secret_misalign_addr; // 未对齐地址（秘密位为1时使用）
volatile uint32_t *g_safe_addr;            // 合法安全地址（训练/推测路径使用）

// 训练标志：1 = 训练模式（分支不跳转，访问合法地址）；0 = 攻击模式（分支跳转至2f，访问秘密地址）
volatile int g_train_flag = 1;

// 根据秘密比特串第一位执行存储操作，并测量执行时间
// Spectre风格：训练阶段(g_train_flag=1)分支不跳转，访问合法地址；
// 攻击阶段(g_train_flag=0)分支跳转至2f，访问秘密地址。
uint64_t measure_secret_store() {
    uint64_t start, end;
    register uint32_t store_val = 0xDEADBEEF;
    register uint32_t secret = secret_bits;
    register int train = g_train_flag;
    start = read_cycles();
    for (int i = 0; i < LOOP_COUNT; i++) {
        asm volatile (
            /* 延时链：fdiv.s 高延迟，令后续地址和分支计算均依赖其结果 */
            "fmv.w.x ft0, %[sval]\n\t"
            "fmv.w.x ft1, %[sval]\n\t"
            "fdiv.s ft2, ft0, ft1\n\t"
            "fmv.x.w t4, ft2\n\t"
            "andi t4, t4, 0\n\t"         /* t4 = 0，但CPU须等fdiv.s完成才能确定 */
            "add t5, %[mis], t4\n\t"
            /* 第一条store指令：存储未对齐地址，地址依赖延时结果 */
            "sw %[sval], 0(t5)\n\t"
            /* Spectre风格分支：条件依赖延时结果(t4)与训练标志 */
            /* 训练时 train=1：t6=1，beqz不跳转（fall-through执行合法路径）  */
            /* 攻击时 train=0：t6=0，beqz跳转至2f（实际执行秘密访问路径）   */
            /* 由于延时链(fdiv.s)使分支条件计算滞后，分支预测器在结果到来前 */
            /* 已投机执行fall-through路径；训练使其预测"不跳转"，攻击时实际  */
            /* 跳转到2f，但投机执行的fall-through已产生时序侧信道（Spectre） */
            "add t6, t4, %[train]\n\t"
            "beqz t6, 2f\n\t"
            /* 训练/投机执行路径：访问合法安全地址，不涉及秘密值 */
            "sw %[sval], 0(%[safe])\n\t"
            "j 3f\n\t"
            "2:\n\t"
            /* 攻击路径（实际执行）：根据秘密比特访问秘密地址 */
            "andi t0, %[sec], 1\n\t"
            "beqz t0, 1f\n\t"
            /* 秘密位为1：执行未对齐地址的sw指令 */
            "sw %[sval], 0(%[smis])\n\t"
            "j 3f\n\t"
            "1:\n\t"
            /* 秘密位为0：执行对齐地址的sw指令 */
            "sw %[sval], 0(%[aln])\n\t"
            "3:\n\t"
            : : [mis]   "r"(g_misalign_addr),
                [aln]   "r"(g_aligned_addr),
                [smis]  "r"(g_secret_misalign_addr),
                [sec]   "r"(secret),
                [sval]  "r"(store_val),
                [train] "r"(train),
                [safe]  "r"(g_safe_addr)
            : "memory", "ft0", "ft1", "ft2", "t0", "t4", "t5", "t6"
        );
    }
    end = read_cycles();
    return (end - start) / LOOP_COUNT;
}

int main() {
    printf("Starting secret bit store timing test...\n");

    // --- 内存准备 ---
    if ((uintptr_t)_heap.end - (uintptr_t)_heap.start < PAGE_SIZE * 4) {
        printf("Error: Not enough heap space.\n");
        return 1;
    }
    char *buffer = (char *)_heap.start;
    memset(buffer, 0, PAGE_SIZE * 4);

    // 检查首地址是否为整数页的首地址
    if ((uintptr_t)buffer % PAGE_SIZE != 0) {
        printf("Error: Buffer start address is not page-aligned (addr=0x%lx).\n",
               (unsigned long)(uintptr_t)buffer);
        return 1;
    }

    // 初始化全局地址指针
    g_misalign_addr        = (uint32_t *)(buffer + PAGE_SIZE - 2);          // 固定未对齐地址
    g_aligned_addr         = (uint32_t *)(buffer + PAGE_SIZE * 2 + 16);     // 对齐地址
    g_secret_misalign_addr = (uint32_t *)(buffer + PAGE_SIZE * 3 - 2);      // 秘密未对齐地址
    g_safe_addr            = (uint32_t *)(buffer + PAGE_SIZE + 64);         // 合法安全地址（训练路径用）

    // --- Spectre训练阶段：反复训练分支预测器预测"不跳转" ---
    // 训练时 g_train_flag=1：分支不跳转，fall-through访问合法地址g_safe_addr
    // 训练路径不依赖secret_bits，secret_bits的值不影响训练效果
    printf("Training branch predictor (branch not taken)...\n");
    g_train_flag = 1;
    for (int i = 0; i < TRAIN_RUNS; i++) {
        measure_secret_store();
    }

    // 切换为攻击模式：g_train_flag=0，分支实际跳转至2f访问秘密地址
    // 分支预测器因训练仍预测"不跳转"，CPU投机执行合法路径，
    // 实际执行却跳转到2f，产生Spectre风格的时序侧信道
    g_train_flag = 0;

    // --- 预热阶段：使缓存和TLB进入稳定状态（攻击模式）---
    printf("Warming up caches and TLB...\n");
    for (int i = 0; i < WARMUP_RUNS; i++) {
        secret_bits = 0;
        measure_secret_store();
        secret_bits = 1;
        measure_secret_store();
    }

    // --- 正式测量阶段：秘密位为0（对齐store）---
    printf("Testing with secret bit = 0 (aligned store)...\n");
    secret_bits = 0;
    uint64_t total_bit0 = 0;
    for (int i = 0; i < TEST_RUNS; i++) {
        total_bit0 += measure_secret_store();
    }
    uint64_t avg_bit0 = total_bit0 / TEST_RUNS;

    // --- 正式测量阶段：秘密位为1（未对齐store）---
    printf("Testing with secret bit = 1 (misaligned store)...\n");
    secret_bits = 1;
    uint64_t total_bit1 = 0;
    for (int i = 0; i < TEST_RUNS; i++) {
        total_bit1 += measure_secret_store();
    }
    uint64_t avg_bit1 = total_bit1 / TEST_RUNS;

    // --- 输出结果 ---
    printf("\n--- Measurement Results (cycles/iteration) ---\n");
    printf("Secret bit = 0 (aligned store):    %d cycles\n", (int)avg_bit0);
    printf("Secret bit = 1 (misaligned store): %d cycles\n", (int)avg_bit1);

    printf("\n--- Analysis ---\n");
    if (avg_bit1 > avg_bit0) {
        int64_t diff = (int64_t)(avg_bit1 - avg_bit0);
        printf("Timing difference detected: %d cycles\n", (int)diff);
        printf("RESULT: Secret bit=1 (misaligned) is slower than bit=0 (aligned).\n");
        printf("Secret bit can be inferred from timing side channel.\n");
    } else if (avg_bit0 > avg_bit1) {
        int64_t diff = (int64_t)(avg_bit0 - avg_bit1);
        printf("Timing difference detected: %d cycles\n", (int)diff);
        printf("RESULT: Secret bit=0 (aligned) is slower than bit=1 (misaligned).\n");
    } else {
        printf("RESULT: No significant timing difference detected.\n");
    }

    return 0;
}
