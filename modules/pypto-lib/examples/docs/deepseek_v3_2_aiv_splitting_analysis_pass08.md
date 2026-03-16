# DeepSeek v3.2 AIV Splitting Analysis (Pass08)

## Scope
- `deepseek_v3_2_decode_front_dump/passes_dump/08_after_ExpandMixedKernel.py`
- `deepseek_v3_2_prefill_front_dump/passes_dump/08_after_ExpandMixedKernel.py`
- `deepseek_v3_2_decode_back_dump/passes_dump/08_after_ExpandMixedKernel.py`
- `deepseek_v3_2_prefill_back_dump/passes_dump/08_after_ExpandMixedKernel.py`

## 1. Common Splitting Pattern
- 所有路径都使用 `aiv_runtime_params=["AIV_IDX"]` 做AIV侧运行时切分。
- 典型行维拼装偏移为：
  - `row_base + AIV_IDX * 2`
- 典型列/通道切分偏移为：
  - `k_base + AIV_IDX * 128` 或 `k_base + AIV_IDX * 32`

## 2. Front Path Findings
- Decode/Prefill front均可见：
  - AIV从全局tensor按 `AIV_IDX` 取子块（`tensor.view`）
  - AIC执行matmul/mac
  - AIV进行 `tensor.assemble` 回拼
- Top-k与稀疏注意力消费处于同一前端融合语义链（Pass08层面表现为可调度group化结构）。

## 3. Back Path Findings
- Decode/Prefill back在 `wo` 与 `w_down` 两段都采用相同AIV/AIC搬运范式。
- Prefill back额外携带token维 `p0_0` 偏移，属于时序维拓展，不改变AIV切分主轴规则。

## 4. Consistency Check (vs ExpandMixedKernel fix intent)
- 当前Pass08中 `tensor.assemble` 偏移表达式已呈现“按被切分轴注入AIV偏移”的形式。
- 未观察到“固定只改offset[0]导致轴不一致”的明显回归特征。
- 建议后续补一个最小回归case，覆盖非axis0 split + assemble重写，做自动化守护。

## 5. Residual Risk
- 目前仍无法完成最终codegen（`comm.aic_initialize_pipe`注册缺失），因此本分析结论基于Pass08 IR静态结构。

