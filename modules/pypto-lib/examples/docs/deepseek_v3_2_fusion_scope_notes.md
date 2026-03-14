# DeepSeek V3.2 Front Kernel Fusion Scope Notes

本文记录 `deepseek_v3_2_decode_front.py` 与 `deepseek_v3_2_prefill_front.py` 在
`indexer topk + sparse attention` 路径上的 scope 设计原则，用于后续扩展和性能/语义复核。

---

## 1) 目标与背景

当前 front 路径包含三类逻辑：

- Stage A: 当前 token 的 RoPE 与 cache 写入；
- Stage B1/B2: 两阶段 topk（局部 topk + 全局 merge）；
- Stage C: 按 topk 稀疏索引执行 attention 消费。

关键问题：这些阶段是否应拆成多个 kernel，还是尽量融合在同一 `auto_incore` scope。

---

## 2) 当前策略（已落地）

### decode_front

`Stage A/B1/B2/C` 已放在同一个 `auto_incore` scope 内执行，形成融合路径。

### prefill_front

同样采用 `Stage A/B1/B2/C` 同 scope 融合，并与 decode 的阶段注释风格对齐。

---

## 3) 为什么“尽量融合”通常更有利

在当前实现中，融合有三点直接收益：

- 减少中间结果物化：`topk_vals/topk_idx` 不需要跨 kernel 落地再读回；
- 提升局部复用：topk 输出可在同一 scope 内被 Stage C 直接消费；
- 降低编排开销：减少 orchestration 层次上的额外函数边界和调度切换。

注意：融合有利不等于“无条件融合”。若局部 tensor 过大导致 SRAM 压力上升，应重新评估边界。

---

## 4) 何时考虑拆分 scope（反向条件）

出现以下信号时，建议尝试将 B2 或 C 独立为新 scope：

- pass 后 local tensor 峰值明显上升并接近平台瓶颈；
- AIV/AIC 分裂后 duplicated 控制开销显著增加；
- 编译器在大 scope 下产生不稳定切分或寄存器压力异常。

建议采用 AB 实验：

- 方案 Fused: A+B1+B2+C 同 scope；
- 方案 Split: A+B1 同 scope，B2+C 同 scope（或 B2 与 C 分离）；
- 对比 `passes_dump/08` 与 `memory_after_AllocateMemoryAddr.txt` 的 SRAM 峰值和指令结构。

---

## 5) 向 2K/8K/128K 分层 topk 扩展的实现建议

当前是简化版 `2 x 2K -> 2K`。扩展到 `2K/8K/128K` 时建议保持如下原则：

- 优先保持“最终 merge + attention 消费”在同一 scope；
- 可将早期分层（例如 8K 分块排序）独立成前置 scope，避免主 attention scope 过重；
- 每层 merge 都保持固定形状 tensor，避免动态形状导致复杂控制分支膨胀；
- 每次扩展后都重新检查 AIV split 语义，确认 `AIV_IDX` 的分片和写回轴一致。

---

## 6) 已知外部阻塞（与 scope 设计无直接冲突）

当前 4 个 deepseek 程序仍存在统一后端问题：

- `No codegen registered for operation: comm.aic_initialize_pipe`

这会阻断端到端 codegen，但不影响前端 pass 级别的 scope/fusion 结构分析。

---

## 7) 维护建议

- 新增或调整 topk 分层时，先改注释中的 Stage 边界，再改代码；
- 保持 decode/prefill 的 Stage 命名一致（A/B1/B2/C）；
- 每次重构后最少产出两类证据：
  - `passes_dump/08_after_ExpandMixedKernel.py`（语义切分）
  - `report/memory_after_AllocateMemoryAddr.txt`（局部内存压力）

