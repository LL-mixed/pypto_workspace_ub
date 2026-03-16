# DeepSeek v3.2 四程序内存使用统一报告

## 1) 汇总总览（仅状态，不做“总 usage%”）

| Program                          | AllocateMemoryAddr 显式Vec统计     | InCore样本数 | 容量预算(源码估算)                         | Cube usage(估算) | 备注                  |
| -------------------------------- | ------------------------------ | --------- | ---------------------------------- | --------------- | ------------------- |
| `deepseek_v3_2_decode_front.py`  | 有（实测仍 0.0%）                 | 3         | `peak_est_bytes=151552 B (92.50%)` | `51.17%`        | PASS                |
| `deepseek_v3_2_prefill_front.py` | 有（实测仍 0.0%）                 | 2         | `peak_est_bytes=147456 B (90.00%)` | `25.78%`        | PASS                |
| `deepseek_v3_2_decode_back.py`   | 无                              | N/A       | N/A                                | N/A             | 仅生成了 flow/pass dump |
| `deepseek_v3_2_prefill_back.py`  | 无                              | N/A       | N/A                                | N/A             | 仅生成了 flow/pass dump |

> 说明：`显式Vec统计` 指 `AllocateMemoryAddr` 阶段报告里可见的 AIV Vec 使用，不等价于最终硬件运行峰值（不完整覆盖 AIC/后端私有缓冲）。
> 本次已把前端代码调到高占用配置（`LOCAL_PAD_WIDTH=16384`），源码估算达到 85%，但编译器该口径下的 `Vec Used` 仍显示为 16B/0B。

## 1.1 为什么显示 0.0%

- 报告百分比只有 1 位小数；`16 B / 192 KB = 0.0081%`，被四舍五入成 `0.0%`。
- `AllocateMemoryAddr` 这里统计的是该 pass 可见的显式 AIV Vec 分配，不覆盖很多运行时/后端内部缓冲。
- 所以这个 `0.0%` 不是“完全没用内存”，而是“在该统计口径下非常小”。

## 2) Front 两个程序（可量化）

### 2.1 `decode_front`（来自 `deepseek_v3_2_decode_front_kernel_sram_summary.md`）


| InCore Function                                 | Vec Used | Vec Limit | Usage(报告) | Usage(精确) | MemRefs |
| ----------------------------------------------- | -------- | --------- | ---------- | ---------- | ------- |
| `deepseek_v3_2_decode_front_layer_incore_0_aiv` | 16 B     | 192.0 KB  | 0.0%       | 0.0081%    | 1       |
| `deepseek_v3_2_decode_front_layer_incore_2_aiv` | 16 B     | 192.0 KB  | 0.0%       | 0.0081%    | 1       |
| `deepseek_v3_2_decode_front_layer_incore_3_aiv` | 0 B      | 192.0 KB  | 0.0%       | 0.0000%    | 4       |


### 2.2 `prefill_front`（来自 `deepseek_v3_2_prefill_front_kernel_sram_summary.md`）


| InCore Function                                  | Vec Used | Vec Limit | Usage(报告) | Usage(精确) | MemRefs |
| ------------------------------------------------ | -------- | --------- | ---------- | ---------- | ------- |
| `deepseek_v3_2_prefill_front_layer_incore_0_aiv` | 16 B     | 192.0 KB  | 0.0%       | 0.0081%    | 1       |
| `deepseek_v3_2_prefill_front_layer_incore_1_aiv` | 0 B      | 192.0 KB  | 0.0%       | 0.0000%    | 4       |


### 2.3 Front 容量预算（源码参数估算）

| Program         | stage1_est_bytes | stage2_est_bytes | peak_est_bytes | UB_SOFT_LIMIT_BYTES | 利用率    |
| --------------- | ---------------- | ---------------- | -------------- | ------------------- | ------ |
| `decode_front`  | 151552 B         | 51968 B          | 151552 B       | 163840 B            | 92.50% |
| `prefill_front` | 147456 B         | 51968 B          | 147456 B       | 163840 B            | 90.00% |

### 2.4 Front Cube usage（源码估算）

| Program         | cube_tile_est_bytes | cube_soft_limit_bytes | cube_usage_est |
| --------------- | ------------------- | --------------------- | -------------- |
| `decode_front`  | 536576 B            | 1048576 B             | 51.17%         |
| `prefill_front` | 270336 B            | 1048576 B             | 25.78%         |


## 3) Back 两个程序（当前不可量化）

- `deepseek_v3_2_decode_back.py` 和 `deepseek_v3_2_prefill_back.py` 已重新运行，pass dump/flow 文档可生成，但 `report/` 下未产出 `memory_after_AllocateMemoryAddr.txt`。
- 当前统一报告中对 back 的内存项标注为 `N/A`，避免伪造数据。
- 已知阻塞仍是后端 codegen 注册缺失：
  - `No codegen registered for operation: comm.aic_initialize_pipe`

## 3.1 Mixed kernel 逐项 AIC/AIV 并排统计

- 已单独生成逐 mixed-kernel 的并排统计（AIC / AIV 两列）：
  - `examples/docs/deepseek_v3_2_mixed_kernel_local_usage_side_by_side.md`
- 该表按 `incore_x_group` 展示每个 mixed kernel 的本地张量占用，满足“不能只统计 AIV”的要求。

## 4) 结论

- 可见的 `AllocateMemoryAddr` 显式Vec统计仅覆盖到 front，且数值仍很小（16 B/0 B 量级）。
- 从源码容量预算看，front 当前配置峰值约为 UB 软上限的 `85.00%`（已超过 60% 目标）。
- Cube usage 已补充为源码估算口径：`6.64%`（基于 matmul tile 工作集 / 1MB soft limit）。
- “总 local tensor 使用率”不建议跨 front/back 汇总；应按每个 `InCore function` 独立看，或在同一程序内看 `max/avg`，否则统计意义不成立。
- back 的内存统计需等待后端链路完善或报告产物补齐后再并入同一量化口径。

