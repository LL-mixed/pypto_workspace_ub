# pypto-serving 实现计划

基于 `reference_sglang_vllm.md` 与 `design goal.md` 的分析与实施路线图。

---

## 一、参考文档要点摘要（reference_sglang_vllm.md）

### 1.1 接口与角色

| 方面 | vLLM | SGLang |
|------|------|--------|
| 主接口 | OpenAI 兼容 HTTP（如 `/v1/chat/completions`） | 同上 + Python DSL |
| 引擎定位 | 无状态推理引擎，无内置会话/历史 | 同左；长期上下文由前端+DB 负责 |
| 短期上下文 | Automatic Prefix Caching（按 block 哈希复用 KV） | RadixAttention（Radix Tree 共享前缀 KV） |

### 1.2 内存与缓存（本实现简化为两层）

- **KV Cache 仅两层**：**(1) GPU 显存**（热数据，参与计算）；**(2) persistent_kv_memory_pool**（本地文件/后续可分片 SSD，冷数据）。**不设 Host 内存（L2）层**，驱逐时直接从 GPU 落盘到 persistent 池，需要时从 persistent 池读回 GPU。
- **Radix Tree**：前缀一致则共享已计算的 KV，只对“分叉”后的 token 做 Prefill；不缓存“输出字符串”，只缓存前缀的 KV，以支持 temperature 等采样。Radix 元数据可放主存并持久化到 meta_data.dat，与 KV 两层无关。
- **会话/历史**：引擎不维护“用户”或“会话”；每次请求由客户端携带完整上下文（含历史），引擎只做计算与缓存复用。

### 1.3 执行流程

- **Prefill**：整段 prompt 一次性并行计算，得到初始 KV Cache，并预测第一个 token。
- **Decode**：自回归循环——用上一 token + KV Cache 预测下一 token，追加到 Cache，直到 EOS / stop sequences / max_tokens。
- **完成判定**：EOS token、用户 stop 序列、max_tokens、或启发式（如重复）。

### 1.4 自回归循环在 PTO2 内闭环（autoregressive_loop_wrapper）

为**避免每个 token 都回 Host 带来的额外开销**，引入 **autoregressive_loop_wrapper**：整条自回归循环在 **PTO2 内部**（设备侧 orchestration / runtime）跑完，Host 只参与两次——一次提交请求（含 Prefill + 循环参数 max_tokens/stop 等）、一次收取完整输出序列。

- **无 wrapper 时**：Host 每步调用「decode 一个 token」→ 设备执行 → 返回 token → Host 判断是否停止 → 再发起下一步（多次 Host↔设备往返）。
- **有 wrapper 时**：Host 发起一次「带 autoregressive_loop_wrapper 的请求」；PTO2 内执行：Prefill → Decode step 1 → Decode step 2 → … → 直到 EOS/stop/max_tokens，全部在设备侧完成；最后将完整 token 序列（或最终状态）一次性返回 Host。**Autoregression 无需回到 Host，无逐 token 的 Host 开销。**

实现上可在 PTO2 的 orchestration 层（或扩展 runtime 原语）提供 `autoregressive_loop_wrapper`：内部循环调用 Decode kernel，在设备上做 EOS/stop/max_tokens 判断，循环结束后再返回；Host 与引擎的接口保持「一次请求 → 一次响应」。

---

## 二、设计目标到组件的映射（design goal.md）

| 设计目标 | 对应实现组件 | 说明 |
|----------|----------------|------|
| (1) C/C++ 高性能核心 | 推理引擎核心、Prefill/Decode 路径、KV 访问路径 | 全部用 C/C++，无 Python 在热路径 |
| (2) OpenAI 风格前端 + 测试直通路径 | Frontend 模块 + Test IPC Path | 先实现无网络的直接 IPC 注入，便于测试与压测 |
| (3) Radix Tree KV 历史管理 | RadixTree + meta_data.dat + persistent_kv_memory_pool | KV 仅两层：GPU 显存 + persistent 文件池；元数据 meta_data.dat 可持久化；无 Host 内存层 |
| (4) 使用 simpler 的 pto2 执行模型 | 集成 ../simpler 的 PTO2 runtime | 通过 PTO2 提交 Prefill/Decode 等 kernel，不重写调度器 |
| (5) 禁止 Python 在自回归路径 | 约束：prefill→decode、KV 访问仅 C/C++ | 架构与 code review 保证 |
| (6) 长期上下文无关 | 仅 Radix Tree 持久化；无用户/会话状态 | 与 reference 文档一致 |
| (7) 通过测试路径注入数据 | Test Path 输入/输出 API | 用于集成测试与 golden 校验 |
| (8) 复制并改造 simpler 的 golden.py | pypto-serving 的 golden + 测试路径调用 | 从 test path 注入输入，从 test path 取回输出并校验 |

---

## 三、系统架构草图

```
                    ┌─────────────────────────────────────────────────┐
                    │           OpenAI-style API (future HTTP)         │
                    └─────────────────────────┬───────────────────────┘
                                              │
  ┌──────────────────────────────────────────▼──────────────────────────────────────────┐
  │                        Frontend (C++): Request Adapter & Router                        │
  │  - 解析 OpenAI 风格请求 (messages, max_tokens, stop, temperature...)                    │
  │  - 路由到: [Test Path] 或 [Network Path将来]                                             │
  └───────────────────────────┬─────────────────────────────────────────┬─────────────────┘
                               │ Test Path (IPC / 进程内直通)             │
                               ▼                                         │
  ┌──────────────────────────────────────────────────────────────────────────────────────┐
  │                     Inference Engine Core (C/C++)                                     │
  │  - Prefill + autoregressive_loop_wrapper：整条自回归在 PTO2 内执行，不每步回 Host       │
  │  - 仅调用 PTO2 / kernel，无 Python                                                     │
  └──┬───────────────────────────────────────────────────────────────────────────────────┘
     │
     │  KV 读写
     ▼
  ┌──────────────────────────────────────────────────────────────────────────────────────┐
  │  Radix Tree + KV Store (C++)                                                          │
  │  - 元数据: meta_data.dat (主存 + 可选持久化)                                            │
  │  - KV 仅两层: GPU 显存 (热) + persistent_kv_memory_pool (文件/SSD，冷)；无 Host 内存层   │
  │  - 前缀匹配、分支、LRU 驱逐时 GPU → persistent，按需从 persistent 读回 GPU               │
  └──┬──────────────────────────────────────────────────────────────────────────────────────┘
     │
     │  kernel 执行
     ▼
  ┌──────────────────────────────────────────────────────────────────────────────────────┐
  │  PTO2 Runtime (../simpler) + autoregressive_loop_wrapper                             │
  │  - 加载 orchestration + kernels                                                       │
  │  - 单次调用内执行：Prefill → [Decode 循环直至 EOS/stop/max_tokens]，循环在设备侧闭环     │
  │  - Host 一次提交、一次取回，无逐 token 往返                                             │
  └──────────────────────────────────────────────────────────────────────────────────────┘

  ┌──────────────────────────────────────────────────────────────────────────────────────┐
  │  Test Driver (Python)                                                                 │
  │  - golden.py 副本: generate_inputs → 通过 Test Path 注入 → 从 Test Path 取输出         │
  │  - compute_golden 做参考值对比                                                        │
  └──────────────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 Prefill/Decode 分离与多计算服务、QoS 分级（扩展架构）

为**提升硬件利用率**，引擎支持 **Prefill 与 Decode 分离**：将请求分别导向**不同的计算服务**，两类服务各自做**批处理优化**，并支持 **QoS 分级**与**同档内按序列长度分组**。

- **Prefill/Decode 分离**  
  - 引擎将请求拆为「Prefill 阶段」与「Decode 阶段」，并**路由到不同的计算服务**（Prefill 服务 vs Decode 服务）。  
  - 分离后可根据负载与硬件特性独立扩缩容、选型（如 Prefill 偏算力密集、Decode 偏访存/延迟敏感），提高整体效率。

- **多计算服务与批处理**  
  - **Prefill 服务**：接收多路请求的 prompt，做** Prefill 批处理**（batch prefill），输出首 token 及 KV（写回 Radix/persistent 池）。  
  - **Decode 服务**：接收多路已 prefill 的会话，做** Decode 批处理**（batch decode），每步对整批生成一个 token，直至各会话达到 EOS/stop/max_tokens。  
  - 两套服务均通过**批处理**提升吞吐与硬件利用率。

- **QoS 分级（TTFT / TPOT 延迟约束）**  
  - 可为 **Prefill 系统**与 **Decode 系统**分别配置**多档实例**，每档对应不同的**批大小（batch 数）**，从而对应不同的延迟上界：  
    - **TTFT（Time To First Token）**：由 Prefill 档位与 prefill batch 大小决定。  
    - **TPOT（Time Per Output Token）**：由 Decode 档位与 decode batch 大小决定。  
  - 例如：低延迟档用小 batch、高优先级；高吞吐档用大 batch、允许更高延迟。引擎或调度层按请求的 QoS 等级将请求发往对应档位的 Prefill/Decode 实例。

- **同档内按序列长度分组（进一步提效）**  
  - 在同一 QoS 档位内，可进一步按**序列长度相近**分组（例如相近 context length 或相近当前 decode 长度），使同一批内的序列长度更接近。  
  - 减少 padding、提高有效计算占比，在相同 TTFT/TPOT 约束下进一步提升效率。

**小结**：引擎负责将请求导向 Prefill 与 Decode 两类计算服务；两类服务均支持批处理；通过不同 batch 数的实例实现 QoS 分级（不同 TTFT/TPOT 边界）；同档内可依序列长度分组以优化批效率。实现上可在 Phase 4 之后以扩展形式落地（见 Phase 7）。

---

## 四、实现阶段与任务分解

### Phase 0：项目骨架与依赖（优先）

| 序号 | 任务 | 产出 |
|------|------|------|
| 0.1 | 创建 pypto-serving 目录结构（src/engine, src/radix, src/frontend, src/test_path, tests, examples） | CMakeLists / 目录树 |
| 0.2 | 确定与 simpler 的依赖方式（子模块 / 拷贝头与库 / 构建时路径） | 文档 + 构建脚本 |
| 0.3 | 最小 C++ 可执行：链接 simpler 的 host runtime，调用 initialize + launch_runtime 一次（用现有 paged_attention 或 vector_example） | 可运行 binary |

**验收**：在 pypto-serving 内能通过 C++ 或脚本拉起 PTO2 并跑通一次现有 example。

---

### Phase 1：测试直通路径（Test Path）

目标：**无网络、直接进程内/IPC 将输入注入引擎并取回输出**，供 golden 校验使用。

| 序号 | 任务 | 产出 |
|------|------|------|
| 1.1 | 定义 Test Path 的 C 接口：`test_path_inject_request(buf, len)` / `test_path_get_response(buf, len)` 或等效（如 token 流、结构化 request/response） | 头文件 + 空实现 |
| 1.2 | 在 Engine 侧实现请求队列（或单请求缓冲）：Test Path 写入请求，Engine 从该队列取请求执行 | 请求注入与消费逻辑 |
| 1.3 | 执行完成后将输出写回 Test Path 可读的缓冲区（或 response 结构），供 Python 端读取 | 响应回写接口 |
| 1.4 | Python 绑定（ctypes 或 pybind11）：从 golden 调用 `inject_request` / `get_response`，实现“进程内直通”或最小 IPC | test_path.py / 小型 runner |

**验收**：Python 脚本能通过 Test Path 注入一组输入并从 Test Path 取回输出（可与 PTO2 现有 paged_attention 输出格式一致，便于后续接 golden）。

---

### Phase 2：Radix Tree 与 KV 存储

目标：**实现 Radix Tree 的元数据与 KV 块管理**，先不接 PTO2 kernel，用 mock 或简单计算验证逻辑。

| 序号 | 任务 | 产出 |
|------|------|------|
| 2.1 | Radix Tree 数据结构（C++）：节点、边、token 序列到节点的映射、前缀匹配与分支 | radix_tree.h/cpp |
| 2.2 | 元数据持久化格式设计：meta_data.dat 的布局（如节点表、边表、块指针） | 格式说明 + 读写代码 |
| 2.3 | KV 两层管理：GPU 显存池（热）+ persistent_kv_memory_pool 文件池（冷）；固定大小块、分配/回收、与 Radix 节点关联；驱逐时 GPU→文件，加载时 文件→GPU；无 Host 内存层 | kv_pool.h/cpp + GPU 池 + 文件 I/O |
| 2.4 | 元数据：meta_data 在主存维护，定期或按需刷到 meta_data.dat | 与 2.2 结合 |
| 2.5 | 前缀查找与插入：给定 token 序列，查找最长匹配前缀、返回对应 KV 块句柄（在 GPU 或需从 persistent 读回）；不匹配则分支并分配新块 | 与 PTO2 解耦的纯逻辑 |

**验收**：单元测试：插入若干前缀、查询前缀、分支、KV 块分配/释放正确；可选：meta_data.dat 写盘后读回验证。

---

### Phase 3：OpenAI 风格请求与 Prefill/Decode 流程

目标：**将“请求”转为一次 Prefill + 设备内自回归循环**，并与 Radix Tree 和 PTO2 对接；循环通过 **autoregressive_loop_wrapper** 在 PTO2 内闭环，不每步回 Host。

| 序号 | 任务 | 产出 |
|------|------|------|
| 3.1 | 请求解析（C++）：从 Test Path 传入的 buffer 解析出 messages / system prompt / max_tokens / stop / temperature 等（先定简单二进制或 JSON 子集） | request_parser |
| 3.2 | 将 messages 转为单一 prompt 序列（token ids）：可先调外部 tokenizer 或读入已 tokenized 的 id 序列 | prompt_builder |
| 3.3 | Prefill 流程：prompt tokens → Radix 前缀查找 → 未命中部分做 Prefill kernel（通过 PTO2 提交）→ 新 KV 写回 Radix/KV 池 | prefill_pipeline |
| 3.4 | **autoregressive_loop_wrapper**：在 PTO2 内封装「Decode 循环」——每步：当前 token + KV → 下一 token，追加 KV，在设备侧判断 EOS/stop/max_tokens；循环结束后将完整输出一次性返回，**不每步回 Host** | autoregressive_loop_wrapper |
| 3.5 | Stop 逻辑（设备侧）：EOS token、用户 stop 序列、max_tokens、可选简单重复检测；在 wrapper 内执行，无需 Host 参与 | stop_condition |

**验收**：通过 Test Path 注入一个短 prompt，能返回一段解码序列（内容可先与 PTO2 现有单步结果一致，再逐步对齐真实生成）。

---

### Phase 4：与 PTO2 的深度集成

目标：**Prefill + 整条自回归循环通过 PTO2 的 autoregressive_loop_wrapper 在设备侧执行**，无 Python、无逐 token Host 往返。

| 序号 | 任务 | 产出 |
|------|------|------|
| 4.1 | 定义 Prefill kernel 的接口（输入：token buffer、KV 块；输出：更新后的 KV、首 token）：与 simpler 现有 AIC/AIV kernel 对接或新增 | kernel 契约 |
| 4.2 | 定义 Decode kernel（输入：上一 token、KV 块；输出：logits/next token、更新 KV） | 同上 |
| 4.3 | **autoregressive_loop_wrapper（PTO2 内）**：在 orchestration 或 runtime 扩展中实现「循环：提交 Decode task → 取回 next token → 设备侧判断 EOS/stop/max_tokens → 未结束则继续下一 Decode」，直到结束；从 Radix 取/还 KV 块均在设备侧完成 | autoregressive_loop_wrapper 实现 |
| 4.4 | Orchestration：一次请求对应「Prefill + autoregressive_loop_wrapper」；Host 只发起一次、收一次完整 token 序列（或句柄） | orchestration 代码 |
| 4.5 | 引擎启动时加载 PTO2 runtime、orchestration SO、kernel binaries（与 simpler 一致），之后 Prefill 与自回归循环均仅在 PTO2 内完成 | 启动与生命周期 |

**验收**：Host 单次提交请求后，PTO2 内跑完 Prefill + 整条 Decode 循环，KV 写入 Radix/KV 池，完整输出经 Test Path 一次返回；无逐 token 的 Host 往返。

---

### Phase 5：golden.py 副本与自动化校验

目标：**复制 simpler 的 golden.py，改为通过 Test Path 注入与接收，并做结果对比**。

| 序号 | 任务 | 产出 |
|------|------|------|
| 5.1 | 从 simpler 复制一份 golden.py（如 examples/tensormap_and_ringbuffer/paged_attention/golden.py）到 pypto-serving（如 examples/golden_paged_attention/golden.py） | 副本 |
| 5.2 | 修改 `generate_inputs`：产出可供 Test Path 使用的格式（与 1.1/3.1 约定一致）；如需 tokenized 输入，在此或单独脚本中完成 | 适配后的 generate_inputs |
| 5.3 | 调用 Test Path：将 generate_inputs 的输入 inject，等待完成后 get_response，得到引擎输出 | 注入与接收逻辑 |
| 5.4 | 保留原 `compute_golden`：对同一输入用 PyTorch/NumPy 算参考值，与 Test Path 返回结果比较（RTOL/ATOL），并输出 __outputs__ | 校验脚本 |
| 5.5 | 与 run_example 类似：支持 ALL_CASES、DEFAULT_CASE、--case、--all | 命令行 runner |

**验收**：运行 pypto-serving 的 golden runner，至少一个 case 通过（与 compute_golden 一致）。

---

### Phase 6：持久化与长期策略（可选扩展）

| 序号 | 任务 | 说明 |
|------|------|------|
| 6.1 | meta_data.dat 的持久化策略 | 启动时加载、关闭时或定期刷盘 |
| 6.2 | persistent_kv_memory_pool 的块大小与扩容策略 | 与 Radix 块大小一致；仅 GPU + 本池两层，无 Host 层 |
| 6.3 | 后续：分片 SSD 块管理 | 将单文件 KV 池扩展为多盘/分片，仍保持两层（GPU + persistent） |

---

### Phase 7：Prefill/Decode 分离、批处理与 QoS 分级（扩展）

目标：**Prefill 与 Decode 分离到多计算服务**，两类服务各自**批处理**，通过**QoS 分级**满足不同 TTFT/TPOT 约束，同档内**按序列长度分组**以提升效率。

| 序号 | 任务 | 产出 |
|------|------|------|
| 7.1 | 请求路由：引擎将请求拆为 Prefill 阶段与 Decode 阶段，并路由到 Prefill 服务 / Decode 服务（多实例可选） | 路由与调度接口 |
| 7.2 | Prefill 服务：支持 batch prefill，多路 prompt 一批计算，输出首 token 与 KV（写回 Radix/persistent）；可多实例、不同 batch 配置 | Prefill 批处理服务 |
| 7.3 | Decode 服务：支持 batch decode，多路会话每步一批生成一个 token，直至各会话结束；可多实例、不同 batch 配置 | Decode 批处理服务 |
| 7.4 | QoS 分级：定义 Prefill/Decode 实例档位（如按 batch 数），每档对应 TTFT/TPOT 延迟上界；请求带 QoS 等级，调度到对应档位实例 | QoS 档位与调度 |
| 7.5 | 同档内按序列长度分组：在同一档位内，将 context length / 当前 decode 长度相近的请求批在一起，减少 padding、提高有效算力占比 | 长度分组批调度 |

**验收**：可部署独立 Prefill 与 Decode 服务；请求按阶段路由并批处理；不同档位实例满足不同 TTFT/TPOT 目标；同档内长度分组后批效率提升可观测。

---

## 五、关键约束检查表

- [ ] **性能关键路径全为 C/C++**：Prefill、Decode、Radix 查找、KV 读写、PTO2 调用均无 Python。
- [ ] **自回归在 PTO2 内闭环**：通过 autoregressive_loop_wrapper，整条 Decode 循环在设备侧执行，Host 一次提交、一次取回，无逐 token 回 Host 的开销。
- [ ] **长期上下文无关**：不维护 user_id/session_id；仅 Radix Tree（及 KV 池）可持久化。
- [ ] **Test Path 先于网络**：首版 OpenAI 风格接口仅通过 Test Path 注入/返回，便于测试与复现。
- [ ] **与 vLLM 行为对齐**：请求/响应格式兼容 OpenAI，便于后续替换或对比；内部实现参考 SGLang Radix + 类 vLLM 的 Prefill/Decode 分离。
- [ ] **扩展：Prefill/Decode 分离与 QoS**：引擎可将 Prefill 与 Decode 导向不同计算服务；两类服务均支持批处理；QoS 分级（不同 batch 档位对应不同 TTFT/TPOT）；同档内按序列长度分组提效。

---

## 六、依赖与顺序小结

```
Phase 0 (骨架) ──► Phase 1 (Test Path) ──┬──► Phase 5 (golden 副本 + 校验)
                     │                    │
                     ▼                    │
                Phase 2 (Radix + KV)      │
                     │                    │
                     ▼                    │
                Phase 3 (请求解析 + Prefill/Decode 流程)
                     │                    │
                     ▼                    │
                Phase 4 (PTO2 深度集成) ──┘
                     │
                     ▼ (扩展)
                Phase 6 (持久化) / Phase 7 (Prefill-Decode 分离、批处理、QoS 分级、长度分组)
```

建议：**0 → 1 → 2 可并行一部分（2 可与 1 重叠）；3 依赖 2；4 依赖 3；5 依赖 1 且最好在 4 之后**，以便端到端用真实 kernel 跑 golden。**Phase 7** 在 4/5 稳定后作为扩展：Prefill/Decode 分离到多服务、批处理、QoS 分级、同档内按序列长度分组。

---

## 七、文档与参考

- **reference_sglang_vllm.md**：vLLM/SGLang 接口、无状态、Prefix/Radix 缓存、Prefill-Decode、停止条件（本实现 KV 仅两层：GPU + persistent，无 L2）。
- **design goal.md**：8 条设计目标与约束。
- **../simpler/docs/pto2_rt.md**：PTO2 运行时、Task Ring、Heap Ring、TensorMap、Orchestration API。
- **../simpler/examples/tensormap_and_ringbuffer/paged_attention/**：kernel_config.py、golden.py、orchestration 与 kernels，作为集成与 golden 复制的模板。
