# `simpler` 接入计划配套说明：M1 最小 Workload 定义

## 1. 文档目的

本说明用于固定 M1 阶段的“最小 workload”定义，避免实施过程中对“什么叫跑通了 `simpler`”出现不同口径。

本说明只服务于 M1：

1. 验证 `simpler` 在当前环境中可独立运行。
2. 收敛运行依赖和最小调用入口。
3. 不评估 guest 集成。
4. 不评估双节点。
5. 不评估 UB/QEMU 平台对齐。

---

## 2. M1 的唯一目标

M1 的唯一目标是：

**证明 `simpler` 可以在 host 环境中通过 `a2a3sim` 路径稳定执行一个最小、可重复、可观测的 workload。**

这里的“跑通”必须同时满足：

1. runtime 能成功初始化。
2. device context 能成功建立。
3. 至少一次 execution 能被成功发起。
4. execution 能产生可判定的完成结果。
5. 资源能被正常清理。

---

## 3. 最小 Workload 的定义

M1 的最小 workload 必须满足以下约束。

### 3.1 功能约束

1. 只包含单次 execution。
2. 不依赖多 device。
3. 不依赖多 stream。
4. 不依赖双节点。
5. 不依赖跨节点内存。
6. 不依赖复杂图调度。
7. 不依赖 Python harness 作为最终执行入口。

### 3.2 数据约束

1. 输入数据规模固定且尽量小。
2. 输出结果必须可判定正确与否。
3. 输出判定不依赖人工观察日志。
4. 输入和输出必须能写入最终文档，作为稳定基线。

### 3.3 运行约束

1. 使用 `a2a3sim`。
2. 仅在 host 环境执行。
3. 不要求接 simulator 的 `ChipBackend`。
4. 不要求接 guest。

---

## 4. M1 冻结结论：唯一最小 Workload

M1 的唯一最小 workload 冻结为：

**`modules/simpler/examples/a2a3/host_build_graph/vector_example`**

推荐运行入口基线：

1. kernel 目录：
   - [`kernel_config.py`](/Volumes/repos/pypto_workspace/modules/simpler/examples/a2a3/host_build_graph/vector_example/kernels/kernel_config.py)
2. golden 脚本：
   - [golden.py](/Volumes/repos/pypto_workspace/modules/simpler/examples/a2a3/host_build_graph/vector_example/golden.py)
3. 当前 README 中给出的基线入口形式：
   - [README.md](/Volumes/repos/pypto_workspace/modules/simpler/README.md)

冻结理由：

1. 它是 `README` 直接给出的 `a2a3sim` quick start 路径。
2. 它使用 `host_build_graph`，复杂度最低，最适合先验证“`simpler` 本体可独立运行”。
3. 输入输出最简单，golden 结果固定，PASS/FAIL 最容易程序化判定。
4. 它不会过早把 `tensormap_and_ringbuffer` 的运行时复杂性卷入 M1。

固定 PASS 语义：

1. runtime 初始化成功
2. 最小 execution 成功完成
3. 输出张量 `f` 被程序化判定正确
4. 相同输入重复运行结果稳定一致

---

## 5. 该 workload 的固定计算语义

该 workload 的计算语义固定为：

`f = (a + b + 1) * (a + b + 2)`

固定输入语义：

1. `a = 2.0`
2. `b = 3.0`
3. 输出 `f` 初始为零张量

固定结果语义：

1. `a + b = 5`
2. `f = (5 + 1) * (5 + 2) = 42`
3. 输出结果应为全 `42.0` 的 `float32` 张量

该语义定义来源于：

- [golden.py](/Volumes/repos/pypto_workspace/modules/simpler/examples/a2a3/host_build_graph/vector_example/golden.py)

---

## 6. 备选项与取舍结论

推荐采用：

**单输入、单输出、单次计算、结果可直接比对的 vector 类样例。**

优先级建议：

1. 主基线：
   - [`host_build_graph/vector_example`](/Volumes/repos/pypto_workspace/modules/simpler/examples/a2a3/host_build_graph/vector_example/golden.py)
2. 第一备选：
   - [`tensormap_and_ringbuffer/vector_example`](/Volumes/repos/pypto_workspace/modules/simpler/examples/a2a3/tensormap_and_ringbuffer/vector_example/test_vector_example.py)
3. 第二备选：
   - [`host_build_graph/matmul`](/Volumes/repos/pypto_workspace/modules/simpler/examples/a2a3/host_build_graph/matmul/golden.py)

选择理由：

### 6.1 主基线：`host_build_graph/vector_example`

保留原因：

1. 这是当前最简单、最稳定、最容易解释的 `a2a3sim` 示例。
2. 最适合作为“`simpler` 能不能先独立跑起来”的唯一判断基线。
3. 输入输出最简单，失败时最容易定位是环境问题、依赖问题还是 runtime 基础问题。

### 6.2 第一备选：`tensormap_and_ringbuffer/vector_example`

文件入口：

- [test_vector_example.py](/Volumes/repos/pypto_workspace/modules/simpler/examples/a2a3/tensormap_and_ringbuffer/vector_example/test_vector_example.py)

保留原因：

1. 它比 `host_build_graph/vector_example` 更接近后续真正想对齐的 runtime 语义。
2. `platforms` 已显式包含 `a2a3sim`。
3. 若后续需要在 M1 后快速补一个“更贴近主线”的 smoke，它是最自然的下一候选。

本阶段不选为唯一基线的原因：

1. 它已经引入 `tensormap_and_ringbuffer` 的运行时复杂性。
2. 作为 M1 唯一基线，会把“`simpler` 自身是否可跑”和“复杂 runtime 是否稳定”混在一起。

### 6.3 第二备选：`host_build_graph/matmul`

文件入口：

- [golden.py](/Volumes/repos/pypto_workspace/modules/simpler/examples/a2a3/host_build_graph/matmul/golden.py)

保留原因：

1. 它更像真实 DAG，而不是最简单的 elementwise vector case。
2. 它可作为 M1 之后的扩展 smoke。

本阶段不选为唯一基线的原因：

1. 计算链更长，精度与中间算子更多。
2. 它更适合做“增强信心”的补充验证，不适合做第一锚点。

### 6.4 明确排除的候选类型

本阶段不选以下类型作为 M1 唯一基线：

1. `paged_attention`
2. `batch_paged_attention`
3. `spmd_*`
4. `scalar_data_test`
5. `mixed_example`
6. 其他依赖复杂 orchestration 或更复杂 runtime 语义的样例

排除原因：

1. 它们更偏 runtime 语义、调度语义或复杂数据依赖验证。
2. 它们不适合作为“先证明 `simpler` 本体能独立跑”的第一锚点。
3. 其中部分样例并未把 `a2a3sim` 作为明确平台基线。

---

## 7. M1 的固定通过标准

M1 通过，必须同时满足以下标准。

### 5.1 构建通过

1. `modules/simpler` 可在当前 workspace 中完成构建。
2. 构建命令被固定记录。
3. 构建产物位置被固定记录。

### 5.2 运行通过

1. 最小 workload 可在 host 上完成单次运行。
2. 单次运行能返回明确的成功状态。
3. 输出结果可被程序化判定为正确。

### 5.3 稳定性通过

1. 相同输入重复运行 20 轮。
2. 20 轮全部成功。
3. 输出结果一致。

### 5.4 依赖清晰

1. 所有动态库被明确列出。
2. 所有 binary/blob 被明确列出。
3. 所有环境变量被明确列出。
4. 所有运行前置条件被明确列出。

### 5.5 入口清晰

1. 必须明确最终保留哪个最小调用入口。
2. 若暂时存在多个入口，必须指定一个“唯一基线入口”。
3. Python 脚本可以作为临时验证手段，但不能作为后续 simulator 接入的正式边界。

---

## 8. 当前入口收敛规则

当前阶段允许的入口策略如下：

1. `README` 中的 `run_example.py` 可作为最初的验证入口参考。
2. 但 `run_example.py` 不能被长期视为后续 simulator 接入的正式边界。
3. M1 结束时，必须在“临时验证入口”和“后续正式接入入口”之间做明确区分。
4. 后续 M2/M4 的正式适配方向，仍应收敛到最小 C/C++ runner 或等价最小 runtime 调用边界。

---

## 9. M1 的输出模板

M1 必须输出以下四份材料。

### 6.1 构建说明

至少包含：

1. 构建命令
2. 工具链要求
3. 关键环境变量
4. 构建产物路径

### 6.2 最小运行说明

至少包含：

1. 运行命令
2. 必要输入文件
3. 预期输出
4. PASS 判定方式

### 6.3 运行时依赖 manifest

至少包含：

1. `.so` 列表
2. runtime 配置文件
3. AICPU 相关产物
4. AICore 相关产物
5. 所有依赖路径来源

### 6.4 最小 runner 方案

至少包含：

1. 最终推荐入口是 C/C++ 还是其他方式
2. 入口负责哪些动作
3. 入口不负责哪些动作
4. 入口与后续 `ChipBackend` 适配的关系

---

## 10. 明确不接受的 M1 完成态

以下情况都不能算 M1 完成。

1. 只能“偶尔跑起来一次”。
2. 必须靠人工补动态库路径才能跑。
3. 输出结果只能看日志主观判断。
4. 仍然不清楚最终应该通过哪个入口调用 `simpler`。
5. 虽然能跑，但依赖清单不完整。

---

## 11. M1 结束后的准入结论

M1 结束后，必须能明确回答以下问题：

1. `simpler` 在当前环境里是否真正可独立运行。
2. 最小工作负载到底是哪一个。
3. 用什么命令构建和运行。
4. 需要哪些 `.so`、blob、配置和环境变量。
5. 后续 M2 应该围绕哪个最小入口做 `ChipBackend` 适配。

若上述任一问题仍无法明确回答，则 M1 不应进入 M2。
