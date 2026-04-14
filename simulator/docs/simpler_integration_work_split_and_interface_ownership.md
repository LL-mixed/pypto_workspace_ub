# `simpler` 接入计划配套说明：实施分工与接口边界

## 1. 文档目的

本说明用于降低多人并行实施时的责任不清、接口反复改、任务互相踩边界的问题。

本说明只解决两件事：

1. 哪类工作由谁负责。
2. 哪些接口边界由哪一侧 owner 决策和收口。

---

## 2. 分工总原则

1. 先按技术边界分工，不按“谁顺手”分工。
2. 一项接口只能有一个收口 owner。
3. 允许多人协作实现，但不允许多人同时拥有同一接口的最终解释权。
4. 若接口未冻结，先停在文档层收口，再进入实现。
5. guest 相关工作不得提前侵入当前 host-side 主线。

---

## 3. 角色定义

当前建议至少固定以下角色。

### 3.1 架构 Owner

负责：

1. 维护主线目标和阶段边界。
2. 冻结里程碑准入条件。
3. 收口跨模块接口争议。
4. 决定何时从 M5 进入 M6。

不负责：

1. 具体 adapter 编码细节
2. 具体 smoke 用例实现
3. guest 打包执行细节

### 3.2 Runtime Owner

负责：

1. `simpler` 独立运行基线
2. `simpler` 最小 workload 选择
3. `simpler` runtime 依赖 manifest
4. `simpler` 最小 runner 方案
5. `simpler` 侧 execution/copy/completion 语义说明

不负责：

1. simulator 侧上层调度逻辑
2. guest 镜像打包
3. UB/QEMU 平台桥接定义

### 3.3 Simulator Owner

负责：

1. `ChipBackend` 适配器实现
2. simulator 侧 handle / task / op 标识模型
3. simulator 侧 error/completion 状态收口
4. trace 点和日志观测点
5. backend 配置与启停路径

不负责：

1. 决定 `simpler` 内部 runtime 语义
2. 决定 guest 运行时打包模型
3. 决定双节点远期策略

### 3.4 Validation Owner

负责：

1. smoke 用例分层
2. 验证矩阵
3. 阶段准入门槛
4. 回归基线定义
5. 失败分类和结果记录口径

不负责：

1. 直接决定接口语义
2. 绕过里程碑提前要求覆盖非目标能力

### 3.5 Guest Owner

仅在进入 M6/M7 后介入。

负责：

1. guest 打包方案
2. guest 入口组织
3. guest 内 smoke 运行方式
4. guest 内依赖清单落地

不负责：

1. 当前 M1-M5 主线目标定义
2. `ChipBackend` 主体语义定义

---

## 4. 里程碑与角色责任矩阵

| 里程碑 | 架构 Owner | Runtime Owner | Simulator Owner | Validation Owner | Guest Owner |
| --- | --- | --- | --- | --- | --- |
| M0 | A | C | C | C | I |
| M1 | C | A | I | C | I |
| M2 | A | C | A | C | I |
| M3 | A | C | A | C | I |
| M4 | C | C | A | C | I |
| M5 | A | I | C | A | I |
| M6 | A | C | C | C | C |
| M7 | C | C | I | C | A |
| M8 | A | C | A | C | C |

说明：

1. `A` 表示最终收口责任人。
2. `C` 表示关键协作方。
3. `I` 表示知会方。

---

## 5. 接口边界归属

多人并行最容易反复扯皮的，不是代码归属，而是接口解释权。下面固定接口边界归属。

### 5.1 `simpler` 最小 workload 定义

收口 Owner：
Runtime Owner

协作方：
Validation Owner

固定规则：

1. Runtime Owner 负责提出唯一 workload 基线。
2. Validation Owner 负责把它转成 PASS/FAIL 准入标准。
3. Simulator Owner 不重新定义 workload。

### 5.2 `ChipBackend` 方法语义

收口 Owner：
Simulator Owner

协作方：
Runtime Owner、架构 Owner

固定规则：

1. `ChipBackend` 的输入输出语义由 Simulator Owner 收口。
2. Runtime Owner 提供 `simpler` 能力与限制。
3. 若 `ChipBackend` 语义需要改变，必须先在文档层冻结后再改实现。

### 5.3 `simpler` runtime 侧能力语义

收口 Owner：
Runtime Owner

协作方：
Simulator Owner

固定规则：

1. `simpler` 的真实 execution/copy/completion 能力边界由 Runtime Owner 解释。
2. Simulator Owner 不得用 simulator 侧假设覆盖 `simpler` 的实际语义。

### 5.4 handle / task / op 映射模型

收口 Owner：
Simulator Owner

协作方：
Runtime Owner

固定规则：

1. simulator 侧如何组织 task/op/handle 由 Simulator Owner 收口。
2. Runtime Owner 负责给出 `simpler` 侧可暴露的句柄信息。
3. 不允许出现“两个团队各自维护一套映射”的情况。

### 5.5 error / completion 分类

收口 Owner：
Simulator Owner

协作方：
Runtime Owner、Validation Owner

固定规则：

1. Runtime Owner 给出底层错误来源。
2. Simulator Owner 负责统一翻译为 simulator 可消费状态。
3. Validation Owner 负责固定哪些状态必须出现在测试结果中。

### 5.6 smoke 与准入标准

收口 Owner：
Validation Owner

协作方：
Runtime Owner、Simulator Owner、架构 Owner

固定规则：

1. smoke 分层与准入标准由 Validation Owner 收口。
2. Runtime Owner 和 Simulator Owner 提供可测入口。
3. 架构 Owner 决定某阶段是否可以带着已知限制进入下一阶段。

### 5.7 guest 打包与 guest 入口

收口 Owner：
Guest Owner

协作方：
Runtime Owner、架构 Owner

固定规则：

1. 只有进入 M6/M7 后才正式展开。
2. guest 方案不得反向改变 M1-M5 的主线定义。

---

## 6. 固定协作边界

### 6.1 Runtime Owner 向 Simulator Owner 提供什么

必须提供：

1. 最小 workload 基线
2. 最小调用入口
3. execution/copy/completion 语义说明
4. 运行时依赖 manifest
5. 返回状态与错误源说明

### 6.2 Simulator Owner 向 Runtime Owner 提供什么

必须提供：

1. `ChipBackend` 方法语义
2. task/op/handle 映射规则
3. 需要的 completion 粒度
4. trace 关联要求
5. 上层调用时序约束

### 6.3 Validation Owner 向双方提供什么

必须提供：

1. smoke 分层
2. 每层 PASS/FAIL 口径
3. 阶段准入门槛
4. 结果记录模板

---

## 7. 变更控制规则

以下变更必须先改文档，再改实现。

1. 最小 workload 改变
2. `ChipBackend` 方法语义改变
3. completion 定义改变
4. copy / dispatch 时序规则改变
5. M1-M5 的通过门槛改变
6. host-side / guest-side 主线切换

以下变更可以直接在实现中推进，但必须同步记录。

1. 内部函数拆分
2. trace 字段补充
3. 非语义性的代码组织调整

---

## 8. 决策升级规则

出现以下情况时，必须升级到架构 Owner 收口：

1. Runtime 语义与 `ChipBackend` 语义发生冲突
2. M1-M5 主线被 guest 需求打断
3. 验证门槛与当前实现复杂度明显不匹配
4. `a2a3sim` 本地闭环与 UB/QEMU 平台对齐目标发生冲突

---

## 9. 不接受的协作状态

以下状态都视为分工失败。

1. 两边都能改接口，但没人对接口最终语义负责。
2. Runtime 团队和 Simulator 团队各自定义 completion。
3. Validation 在接口未冻结前就推动大规模回归。
4. Guest 工作提前侵入 M1-M5 主线。
5. 通过口径仍依赖口头同步，而不是文档规则。

---

## 10. 实施建议

在进入真正编码前，建议最少完成以下动作：

1. 指定各角色 owner
2. 确认 M1-M3 文档已经被对应 owner 接受
3. 确认 M4 前不展开 guest 实施
4. 确认 Validation Owner 已接受 M5 作为准入门槛

满足以上条件后，再进入并行实施。

