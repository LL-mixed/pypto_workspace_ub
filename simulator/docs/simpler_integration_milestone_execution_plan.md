# `simpler` 接入当前模拟节点实施计划

## 1. 文档目的

本计划用于指导后续按顺序实施 `simpler` 接入当前模拟系统的工作。

目标不是一次性覆盖所有远期能力，而是形成一条可执行、可验收、可逐步扩展的主线：

1. 先证明 `simpler` 自己可独立运行。
2. 再把 `simpler` 作为 simulator 的真实 `ChipBackend` 接入。
3. 然后再决定是否需要进入 ARM64 guest。
4. 最后才讨论与当前 UB/QEMU 平台的深度对齐和双节点扩展。

---

## 2. 总体原则

1. 主线先做单节点、host-side、`ChipBackend` 真接入。
2. `simpler` 内部不改，先做外部 adapter。
3. `a2a3sim` 闭环和“接入当前模拟节点”是两个阶段，不合并。
4. “能启动”不等于“已接入完成”。
5. 每个里程碑必须有硬验收，不靠口头判断。

---

## 3. 当前阶段目标与非目标

### 3.1 当前阶段目标

当前第一阶段的目标是：

`simpler -> ChipBackend -> simulator` 的 host-side 接入闭环。

这意味着：

1. `simpler` 可以在当前环境中独立稳定运行最小 workload。
2. simulator 的 `ChipBackend` 不再停留在 `stub`，而是能够真实驱动 `simpler`。
3. `dispatch`、`h2d_copy`、`d2h_copy`、`poll_completion` 四类动作可以形成真实闭环。

### 3.2 当前阶段非目标

当前阶段明确不做：

1. ARM64 guest 内运行 `simpler`
2. 双节点执行语义
3. 跨节点共享内存
4. CPU 通用 `load/store` 一致性
5. ATOMIC / 更高级一致性能力
6. 对 `simpler` 内部 runtime 逻辑做侵入式改造

---

## 4. 关键基线

当前计划建立在以下基线上：

1. simulator 已存在 `ChipBackend` 抽象
   - 见 [lib.rs](/Volumes/repos/pypto_workspace/simulator/crates/sim-runtime/src/lib.rs)
2. simulator 配置中已存在 `simpler_boundary`，但 `chip_backend_mode` 仍为 `stub`
   - 见 [lib.rs](/Volumes/repos/pypto_workspace/simulator/crates/sim-config/src/lib.rs)
3. `simpler` 已支持 `a2a3sim` / `a5sim`
   - 见 [README.md](/Volumes/repos/pypto_workspace/modules/simpler/README.md)
4. `simpler` 已有核心 C API 边界
   - 见 [pto_runtime_c_api.h](/Volumes/repos/pypto_workspace/modules/simpler/src/common/worker/pto_runtime_c_api.h)
5. 当前 ARM64 guest initramfs 只打包 `linqu_*` demo，不包含 `simpler` 动态依赖栈
   - 见 [build_initramfs.sh](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/build_initramfs.sh)
   - 见 [init.c](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/init.c)
6. 顶层 runtime 设计已经倾向于通过 `ChipBackend` 适配 `simpler`
   - 见 [linqu_runtime_design.md](/Volumes/repos/pypto_workspace/docs/pypto_top_level_design_documents/linqu_runtime_design.md)

---

## 5. 里程碑执行计划

### M0：边界冻结

#### 目标

把项目目标、阶段范围、非目标一次性说清楚，避免后续返工。

#### 实施内容

1. 定义第一阶段目标为：`simpler -> ChipBackend -> simulator` 的 host-side 接入闭环。
2. 明确 ARM64 guest 内运行 `simpler` 不进入当前关键路径。
3. 冻结最小支持能力：`dispatch`、`h2d_copy`、`d2h_copy`、`poll_completion`。
4. 冻结当前不做的能力：双节点执行、跨节点共享内存、CPU 通用 `load/store` 一致性、ATOMIC、guest 动态运行时完整打包。
5. 明确定义当前“模拟节点”优先指 host-side simulator node，而不是 ARM64 guest process。

#### 关键输入

1. [linqu_runtime_design.md](/Volumes/repos/pypto_workspace/docs/pypto_top_level_design_documents/linqu_runtime_design.md)
2. [lib.rs](/Volumes/repos/pypto_workspace/simulator/crates/sim-runtime/src/lib.rs)
3. [lib.rs](/Volumes/repos/pypto_workspace/simulator/crates/sim-config/src/lib.rs)

#### 产出物

1. 《项目范围说明》
2. 《阶段完成定义》
3. 《非目标清单》
4. 《边界术语表》

#### 验收标准

1. 团队对“先 host-side，后 guest-side”没有歧义。
2. 团队对“先接边界，后做平台深度对齐”没有歧义。
3. 后续任务不再把 guest 打包当成当前主线。

#### 失败信号

1. 仍在讨论是否应先把 `simpler` 塞进 guest。
2. 仍把双节点、跨节点内存、runtime 接入混成同一个任务。

---

### M1：跑通 `simpler` 独立基线

#### 目标

证明 `simpler` 在当前仓库和环境中可独立运行，消除黑盒风险。

#### 实施内容

1. 构建 `modules/simpler`。
2. 使用 `a2a3sim` 跑通最小 simulation example。
3. 固化运行命令、环境变量、依赖路径。
4. 梳理所有运行时依赖：
   - host runtime `.so`
   - `sim_context` 相关库
   - AICPU 产物
   - AICore 产物
   - 配置文件
5. 输出一个最小调用入口方案，建议最终选择最小 C/C++ runner，不依赖 Python harness。

#### 关键输入

1. [README.md](/Volumes/repos/pypto_workspace/modules/simpler/README.md)
2. [pto_runtime_c_api.h](/Volumes/repos/pypto_workspace/modules/simpler/src/common/worker/pto_runtime_c_api.h)
3. [chip_worker.cpp](/Volumes/repos/pypto_workspace/modules/simpler/src/common/worker/chip_worker.cpp)
4. [cpu_sim_context.cpp](/Volumes/repos/pypto_workspace/modules/simpler/src/common/sim_context/cpu_sim_context.cpp)

#### 产出物

1. 《simpler 构建说明》
2. 《simpler 最小运行说明》
3. 《运行时依赖 manifest》
4. 《最小 runner 方案》

#### 验收标准

1. 最小 workload 可在 host 上稳定跑通。
2. 所有 `.so` 和 binary 依赖列全。
3. 运行入口被收敛到可被 simulator 调用的最小边界。

#### 退出条件

`simpler` 不再是“理论可接”，而是“已知可跑、依赖已清”。

#### 失败信号

1. 依赖不完整，运行仍依赖人工补环境。
2. 仍然只能通过复杂脚本或 Python 才能启动。

---

### M2：定义接入边界

#### 目标

把 simulator 和 `simpler` 之间的接口语义设计清楚，避免接一半返工。

#### 实施内容

1. 以 [lib.rs](/Volumes/repos/pypto_workspace/simulator/crates/sim-runtime/src/lib.rs) 的 `ChipBackend` trait 为基准，逐项映射到 `simpler`。
2. 明确四个动作的接口语义：`dispatch`、`h2d_copy`、`d2h_copy`、`poll_completion`。
3. 设计句柄模型：simulator handle、task key、op id、`simpler` runtime handle 的关系。
4. 设计生命周期：backend init、device context create、set device、runtime start、runtime finalize。
5. 设计错误传播：`simpler` 返回码如何映射为 simulator completion/error。
6. 明确 completion 是真实 completion，不允许继续依赖 stub 延迟事件。

#### 关键输入

1. [lib.rs](/Volumes/repos/pypto_workspace/simulator/crates/sim-runtime/src/lib.rs)
2. [pto_runtime_c_api.h](/Volumes/repos/pypto_workspace/modules/simpler/src/common/worker/pto_runtime_c_api.h)
3. [linqu_runtime_design.md](/Volumes/repos/pypto_workspace/docs/pypto_top_level_design_documents/linqu_runtime_design.md)

#### 产出物

1. 《ChipBackend-Simpler 接口映射说明》
2. 《句柄与任务标识模型》
3. 《生命周期状态机》
4. 《错误与 completion 映射规则》

#### 验收标准

1. 每个 `ChipBackend` 方法都能找到明确落点。
2. completion 语义不再依赖“先假设后补实现”。
3. device context 和 runtime 生命周期有完整闭环。

#### 退出条件

实现人员不再争论“这个动作到底由哪一层负责”。

#### 失败信号

1. `dispatch`、copy、completion 责任不清。
2. 句柄和 task identity 没统一，后面无法扩到多 device。

---

### M3：内存与数据路径建模

#### 目标

把 copy 和 dispatch 的数据边界定义成可以真正实现和调试的模型。

#### 实施内容

1. 明确 host DRAM 和 device GM 是两个独立内存域。
2. 明确 buffer ownership：谁申请、谁持有、谁释放。
3. 明确 host buffer 到 `simpler` device 地址或句柄的映射关系。
4. 明确 `h2d_copy` / `d2h_copy` 的同步语义和异步语义。
5. 明确 copy、dispatch、completion 的先后约束。
6. 明确异常场景：copy failure、timeout、missing completion、late completion。
7. 明确 trace 观测点：copy issue、copy done、dispatch issue、dispatch done、poll、completion emit。

#### 关键输入

1. M2 的接口映射结果
2. `simpler` 的 runtime/copy 语义
3. simulator 当前 completion 模型

#### 产出物

1. 《Tier-2 内存边界说明》
2. 《copy-dispatch-completion 状态机》
3. 《异常行为定义》
4. 《trace 点清单》

#### 验收标准

1. 任意一次 execution 都能解释清楚数据从哪来、到哪去、何时完成。
2. copy 和 dispatch 的依赖关系明确。
3. 故障场景有统一处理规则。

#### 退出条件

数据路径不再依赖隐式假设。

#### 失败信号

1. 仍把 device memory 当成“黑盒内部状态”。
2. 出错时无法判断是 copy、dispatch 还是 completion 问题。

---

### M4：完成 host-side `ChipBackend` 真接入

#### 目标

在 host-side simulator node 中完成 `simpler` 真接入，替换 `stub`。

#### 实施内容

1. 将 simulator 配置中的 `simpler_boundary` 从概念开关推进为真实 backend 路径。
2. 落地 `ChipBackend -> simpler` adapter。
3. 接通初始化、device 绑定、runtime 启停。
4. 接通 `dispatch`、`h2d_copy`、`d2h_copy`、`poll_completion`。
5. 接通错误回传和 completion 回传。
6. 接通 trace 和日志关键点。

#### 关键输入

1. M1 的最小 runner / runtime 基线
2. M2 的边界设计
3. M3 的内存与状态机定义

#### 产出物

1. host-side `simpler` backend 实现
2. backend 配置说明
3. 运行与观测说明

#### 验收标准

1. `ChipBackend` 不再依赖 `stub`。
2. simulator 能收到真实 completion。
3. 一个最小 workload 可从 simulator 真实发起并完成。

#### 退出条件

`simpler` 已成为 simulator 的真实执行 backend，而不是旁路 demo。

#### 失败信号

1. completion 仍是假造的。
2. runtime 初始化成功，但 dispatch/copy 没有真实落到 `simpler`。

---

### M5：建立验证基线

#### 目标

建立“是否可进入下一阶段”的硬准入，不让项目停在半接入状态。

#### 实施内容

1. 定义 standalone smoke。
2. 定义 backend smoke。
3. 定义 copy + dispatch + completion 联合 smoke。
4. 定义稳定性回归轮数。
5. 定义现有 demo 非回归要求。

#### 建议准入门槛

1. standalone workload 连续通过 20 轮。
2. `ChipBackend` smoke 连续通过 20 轮。
3. copy-dispatch-completion 链路连续通过 20 轮。
4. 不破坏现有 `linqu_*` demo 主链路。

#### 关键输入

1. M1 到 M4 的产物
2. 当前 simulator 已有 demo 基线

#### 产出物

1. 《验证矩阵》
2. 《阶段准入标准》
3. 《回归基线说明》

#### 验收标准

1. 每个阶段都能回答“凭什么说已经完成”。
2. 任何失败都有可归类的测试层次。

#### 退出条件

接入完成不再是主观判断。

#### 失败信号

1. 只有一次性手工跑通，没有稳定性判据。
2. 新链路跑通后把旧链路跑坏了。

---

### M6：节点集成形态决策

#### 目标

明确后续“当前模拟节点”到底是 host-side 集成，还是把 `simpler` 真放进 ARM64 guest。

#### 实施内容

1. 评估方案 A：`simpler` 保持 host-side simulator node 组件。
2. 评估方案 B：`simpler` 真进入 ARM64 guest。
3. 从工程复杂度、调试成本、依赖打包压力、与当前 QEMU/UB 模型贴合度、后续双节点扩展成本等维度比较。
4. 做唯一决策，不双线推进。

#### 关键输入

1. [build_initramfs.sh](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/build_initramfs.sh)
2. [init.c](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/init.c)
3. M1 到 M5 的接入现状

#### 产出物

1. 《节点集成决策说明》
2. 《A/B 方案对比》
3. 《进入 guest 的前置条件》

#### 验收标准

1. 团队只保留一条后续主线。
2. 若继续 host-side，则 guest 工作不进入关键路径。
3. 若进入 guest，则必须有明确前置条件。

#### 退出条件

后续资源投入方向固定。

#### 失败信号

1. 同时推进 host-side 和 guest-side。
2. 还没把 backend 接稳，就开始做 guest 打包。

---

### M7：guest 集成预案或落地

#### 目标

只有在 M6 明确选择 guest-side 时才进入本里程碑。

#### 实施内容

1. 评估当前 tiny initramfs 是否继续可用。
2. 判断是否需要更完整 rootfs。
3. 梳理 guest 内最小运行所需内容：
   - 动态 loader
   - 共享库
   - runner
   - AICPU/AICore 产物
   - 配置
   - 环境变量
4. 设计 guest smoke：runtime init、`set_device`、一次 dispatch、一次 completion、清理退出。
5. 定义 guest 内 device id / entity / node 的映射。

#### 关键输入

1. [build_initramfs.sh](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/build_initramfs.sh)
2. [init.c](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/init.c)
3. M1 的依赖 manifest

#### 产出物

1. 《guest 集成差距清单》
2. 《guest 打包方案》
3. 《guest smoke 方案》

#### 验收标准

1. guest 内运行的依赖、入口、smoke 都被明确列出。
2. guest 方案不会反向污染 host-side 主线。

#### 退出条件

guest 集成具备明确工程路径。

#### 失败信号

1. 低估动态库和 rootfs 压力。
2. 用“把文件塞进 initramfs”替代完整运行时设计。

---

### M8：与当前模拟平台的深度对齐

#### 目标

回答“`simpler` 是借 simulator 外壳跑起来，还是已经真正对齐当前 UB/QEMU 平台”。

#### 实施内容

1. 明确 `simpler` 当前底层是继续使用 `a2a3sim`，还是要桥接到当前 UB/QEMU 设备模型。
2. 若要平台对齐，定义以下映射：
   - device id
   - entity
   - queue
   - doorbell
   - completion
   - memory region
   - fault/reset/timeout
3. 明确 `cpu_sim_context` 的定位：它可以作为早期 execution backend，但不自动等于“已接上当前模拟节点平台”。

#### 关键输入

1. 当前 simulator/QEMU/UB 平台模型
2. M4 的 host-side 接入结果
3. M6 的节点集成决策

#### 产出物

1. 《平台对齐差距说明》
2. 《平台桥接优先级清单》
3. 《第一批必须补齐的桥接项》

#### 验收标准

1. 能清楚区分“功能接入完成”和“平台对齐完成”。
2. 不再混淆 `a2a3sim` 闭环与 UB/QEMU 节点闭环。

#### 退出条件

后续平台演进有了明确入口。

#### 失败信号

1. 项目宣称“已接入当前模拟节点”，但实际上还只是 `a2a3sim` 本地闭环。
2. 平台映射没有明确边界，后续设计继续漂移。

---

## 6. 推荐执行顺序

严格按以下顺序推进：

1. M0
2. M1
3. M2
4. M3
5. M4
6. M5
7. M6
8. M7
9. M8

执行原则：

1. 先证明 `simpler` 自己可跑。
2. 再把 simulator 边界接通。
3. 再建立稳定性和准入门槛。
4. 然后才决定是否进入 guest。
5. 最后再做更深的 UB/QEMU 平台对齐。

---

## 7. 每个阶段的通过门槛

1. M0：范围、非目标、阶段目标冻结。
2. M1：`simpler` 独立最小 workload 稳定运行，依赖 manifest 完整。
3. M2：`ChipBackend` 到 `simpler` 的方法映射和生命周期定义完整。
4. M3：copy / dispatch / completion 的数据路径和异常语义定义完整。
5. M4：host-side `ChipBackend` 真接入完成，最小 workload 可从 simulator 发起并完成。
6. M5：验证矩阵与稳定性门槛建立，旧 demo 不回归。
7. M6：节点集成路线唯一化。
8. M7：若选 guest-side，guest 运行条件与 smoke 完整明确。
9. M8：功能接入与平台对齐两件事被正式拆开管理。

---

## 8. 当前实施主判断

如果现在就进入实施，关键路径只有这一条：

1. 先做 M0
2. 再做 M1
3. 再做 M2-M4
4. 用 M5 卡住质量门槛
5. 然后再讨论 M6-M8

换句话说：

**先把 `simpler` 变成 simulator 的真实 backend，再决定是否把它变成“模拟节点内部组件”。**

