# `simpler` 接入计划配套说明：M2 `ChipBackend` 对照与职责定义

## 1. 文档目的

本说明用于固定 M2 阶段的接口对照规则，避免后续在 `ChipBackend` 与 `simpler` 的职责划分上反复争论。

本说明解决的问题只有一个：

**simulator 的 `ChipBackend` 边界，如何映射到 `simpler` 的 runtime 边界。**

---

## 2. M2 的唯一目标

M2 的唯一目标是：

**把 `ChipBackend` 的每个动作，映射到一个明确、可实现、可验收的 `simpler` 落点。**

M2 完成后，实现人员必须能够不争论地回答：

1. `dispatch` 到底由谁发起。
2. `copy` 到底由谁负责。
3. `completion` 到底从哪一层返回。
4. `device context` 的生命周期由谁控制。
5. 失败时错误码由谁翻译。

---

## 3. 职责边界总原则

### 3.1 simulator `ChipBackend` 负责什么

1. 暴露统一的执行后端接口。
2. 把 simulator 侧请求转换为 backend 可接受的调用。
3. 管理 simulator 侧任务标识、句柄和状态。
4. 汇总并向上返回 completion/error。
5. 提供统一 trace 观测点。

### 3.2 `simpler` runtime 负责什么

1. 提供设备上下文和 runtime 初始化能力。
2. 提供 execution/copy 的实际执行能力。
3. 提供任务完成状态或可轮询的完成信息。
4. 提供底层错误返回。

### 3.3 当前阶段明确不由 M2 解决的事情

1. guest 集成
2. 双节点任务分发
3. UB/QEMU 平台级 queue/doorbell 对齐
4. CPU 通用一致性问题

---

## 4. M2 固定方法对照表

以下表是 M2 的唯一基线。若后续实现需要偏离，必须显式说明原因。

| `ChipBackend` 动作 | `simpler` 侧落点 | 当前阶段职责说明 | 当前阶段验收点 |
| --- | --- | --- | --- |
| `dispatch` | 映射到 `simpler` 的最小 execution 调用路径 | 负责把 simulator 的任务请求转换为一次真实 execution | 能发起一次真实 execution，并能等待或轮询到完成 |
| `h2d_copy` | 映射到 `simpler` 的 host-to-device 数据传输路径 | 负责把 host 输入搬到 `simpler` 可执行的设备侧数据域 | 数据可被 execution 正常消费 |
| `d2h_copy` | 映射到 `simpler` 的 device-to-host 数据传输路径 | 负责把结果搬回 simulator 可见的数据域 | 输出结果可被程序化校验 |
| `poll_completion` | 映射到 `simpler` 的完成状态获取路径 | 负责提供真实 completion，而不是 stub 延迟事件 | 能返回 success/fail/timeout 等可判定状态 |

---

## 5. 句柄与标识模型

M2 必须固定以下四类标识的关系。

### 5.1 simulator 侧标识

1. `TaskKey`
2. `OpId`
3. `DispatchHandle`
4. copy 操作标识

### 5.2 `simpler` 侧标识

1. device context handle
2. runtime handle
3. execution handle
4. copy/completion 相关句柄

### 5.3 固定要求

1. 一次 simulator 侧动作，必须能映射到一个可追踪的 `simpler` 侧动作。
2. 一次 `simpler` completion，必须能反查到 simulator 侧请求。
3. 句柄映射必须支持日志和 trace 中的关联定位。
4. 不允许在 M2 阶段使用“隐式全局状态”代替句柄映射。

---

## 6. 生命周期对照

M2 必须固定以下生命周期顺序。

### 6.1 backend 生命周期

1. backend init
2. device context create
3. device bind / set device
4. runtime start
5. execution / copy
6. completion poll
7. runtime finalize
8. backend teardown

### 6.2 固定规则

1. `dispatch` 不负责偷偷初始化 runtime。
2. copy 不负责偷偷创建 device context。
3. teardown 必须可被显式执行。
4. 生命周期失败必须能报告到 simulator 层。

---

## 7. 错误与 completion 规则

### 7.1 completion 来源

M2 固定要求：

**completion 必须来自 `simpler` 的真实执行结果，不允许继续使用 stub 注入的伪完成。**

### 7.2 completion 至少要区分的状态

1. success
2. failed
3. timeout
4. cancelled
5. unknown internal error

### 7.3 错误传播规则

1. `simpler` 的底层错误，必须被翻译为 simulator 层可识别状态。
2. 翻译规则必须文档化。
3. 不允许把所有失败都折叠成单一 `generic failure`。

---

## 8. M2 实施输出模板

M2 至少要输出以下文档性结果。

### 8.1 方法对照表

必须逐项列出：

1. `ChipBackend` 方法名
2. `simpler` 落点
3. 输入参数来源
4. 输出参数去向
5. completion 语义
6. 错误码映射

### 8.2 句柄模型说明

必须列出：

1. simulator 侧句柄集合
2. `simpler` 侧句柄集合
3. 一一映射还是一对多映射
4. trace 中如何关联

### 8.3 生命周期状态机

必须列出：

1. 初始化前置条件
2. 正常路径
3. 失败路径
4. 资源释放点

---

## 9. 明确不接受的 M2 完成态

以下情况都不能算 M2 完成。

1. 只知道“大概可以接”，但没有逐方法对照。
2. completion 仍然靠 stub 或固定延迟伪造。
3. 句柄映射不清，只能靠全局状态猜测归属。
4. runtime 生命周期嵌在 `dispatch` 内部，无法独立控制。
5. 错误码映射没有文档，失败时只能看底层日志猜原因。

---

## 10. M2 结束后的准入结论

M2 结束后，必须能明确回答以下问题：

1. `ChipBackend` 的四个动作分别对应 `simpler` 哪条调用路径。
2. 一次请求如何从 simulator 追踪到 `simpler`。
3. 一次 completion 如何从 `simpler` 反查回 simulator 请求。
4. runtime 生命周期由谁管理。
5. 错误状态如何统一翻译。

若上述任一问题仍无法明确回答，则 M2 不应进入 M3/M4。

