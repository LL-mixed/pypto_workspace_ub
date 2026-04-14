# `simpler` 接入计划配套说明：M3 Copy / Dispatch / Completion 时序规则

## 1. 文档目的

本说明用于固定 M3 阶段的首版数据路径与时序规则，避免后续在 copy 是同步还是异步、completion 什么时候产生、失败到底算哪一层问题上反复争论。

本说明只定义第一版可实施规则，不追求一步到位覆盖所有未来能力。

---

## 2. M3 的唯一目标

M3 的唯一目标是：

**把 `h2d_copy`、`dispatch`、`d2h_copy`、`poll_completion` 组织成一个可实现、可调试、可验收的数据路径模型。**

M3 完成后，实现人员必须能够不争论地回答：

1. 数据从哪里进入 device 侧。
2. execution 在什么前提下可以开始。
3. 结果何时可被认为完成。
4. completion 到底表示哪一层完成。
5. 超时、失败、晚到 completion 如何处理。

---

## 3. 首版数据域模型

M3 固定采用双内存域模型。

### 3.1 Host Memory Domain

1. simulator 可直接访问的数据域。
2. 输入初始数据位于这里。
3. 输出最终结果也必须回到这里。

### 3.2 Device Memory Domain

1. `simpler` execution 可消费的数据域。
2. `dispatch` 只能读取或写入这个域中的有效数据。
3. 该域中的地址或句柄必须可被映射与追踪。

### 3.3 固定规则

1. host 数据默认不能被 `dispatch` 直接消费。
2. execution 前必须满足输入已在 device 域可用。
3. 输出在回到 host 域之前，不应被 simulator 上层当成最终结果。

---

## 4. 首版时序规则

M3 固定采用以下最小闭环时序：

1. 分配或绑定 host 输入 buffer
2. 执行 `h2d_copy`
3. 确认 device 侧输入有效
4. 发起 `dispatch`
5. 轮询或等待 execution completion
6. 若 execution 成功，执行 `d2h_copy`
7. 轮询或等待 copy completion
8. 结果回到 host 域后，才允许上层判定 workload 完成

---

## 5. 首版同步/异步规则

为了避免实现初期复杂度失控，M3 固定以下首版规则。

### 5.1 `h2d_copy`

首版允许实现为：

1. 同步完成后返回
2. 或异步发起，但必须有可轮询 completion

固定要求：

1. 不允许在 `h2d_copy` 尚未完成时直接发起依赖其输入的 `dispatch`。
2. 必须能明确判断 copy 是否已完成。

### 5.2 `dispatch`

1. 只有在输入依赖满足后才能发起。
2. `dispatch` 的完成只表示 execution 阶段完成。
3. `dispatch` 完成不自动表示结果已经回到 host 域。

### 5.3 `d2h_copy`

1. 只有在 execution 成功完成后才允许发起。
2. 若没有 `d2h_copy`，则 host 上层不得判定最终结果就绪。

### 5.4 `poll_completion`

1. 必须能区分当前是在等待 copy 还是等待 execution。
2. 必须能区分未完成、成功、失败、超时。
3. 不允许把不同类型 completion 混成一个不可区分状态。

---

## 6. completion 语义固定规则

### 6.1 execution completion

execution completion 只表示：

1. kernel 或等价执行单元完成
2. execution 阶段成功或失败已知

它不表示：

1. 输出已经回到 host
2. 整个 workload 对上层已完成

### 6.2 copy completion

copy completion 只表示：

1. 一次 copy 动作完成
2. 数据已到达目标内存域，或 copy 失败

### 6.3 workload completion

workload completion 只在以下条件同时满足后成立：

1. execution completion success
2. 必要的 `d2h_copy` completion success
3. host 侧结果可被读取与校验

---

## 7. 异常规则

M3 首版必须显式定义以下异常。

### 7.1 `h2d_copy` 失败

处理规则：

1. 不得发起依赖该输入的 `dispatch`
2. 该 workload 直接进入失败态
3. 必须产生明确错误分类

### 7.2 execution 失败

处理规则：

1. 不得发起依赖成功输出的 `d2h_copy`
2. 该 workload 进入失败态
3. 必须可追踪到对应 execution 请求

### 7.3 `d2h_copy` 失败

处理规则：

1. execution 可以被视为已完成
2. 但整个 workload 仍判定失败
3. 必须明确标记为结果回传失败，而不是 execution 失败

### 7.4 timeout

处理规则：

1. 必须指明 timeout 发生在 copy 还是 execution
2. timeout 不能统一折叠为 generic failure
3. timeout 后的资源清理路径必须可定义

### 7.5 late completion

处理规则：

1. 若请求已超时或取消，晚到 completion 不能再次触发成功收尾
2. 晚到 completion 必须有独立日志或 trace 标记

---

## 8. trace 观测点固定规则

M3 至少要有以下观测点。

1. `h2d_copy` issue
2. `h2d_copy` complete
3. `dispatch` issue
4. `dispatch` complete
5. `d2h_copy` issue
6. `d2h_copy` complete
7. `poll_completion` observe
8. workload final success/failure

每个观测点至少应能关联：

1. task key
2. operation id
3. 操作类型
4. 状态

---

## 9. 明确不接受的 M3 完成态

以下情况都不能算 M3 完成。

1. 仍然说不清 `dispatch` 完成是否等于 workload 完成。
2. 仍然说不清 copy 失败应归类为什么失败。
3. `h2d_copy` 和 `dispatch` 顺序靠约定，不靠规则。
4. 看日志仍无法区分 execution completion 和 copy completion。
5. timeout 和 late completion 没有单独处理方式。

---

## 10. M3 结束后的准入结论

M3 结束后，必须能明确回答以下问题：

1. 输入如何从 host 域进入 device 域。
2. execution 何时允许开始。
3. completion 到底表示哪一层完成。
4. 输出何时才算真正对 host 可见。
5. copy/execution/timeout/late completion 各自如何归类和处理。

若上述任一问题仍无法明确回答，则 M3 不应进入 M4。

