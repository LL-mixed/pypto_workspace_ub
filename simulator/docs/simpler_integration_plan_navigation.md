# `simpler` 接入计划单引用入口与文档导航页

## 1. 文档目的

本页用于作为 `simpler` 接入当前模拟节点计划的**单引用入口文档**。

这意味着：

1. 无论当前处于哪个阶段，外部只需要引用本页即可。
2. 本页负责说明当前这组文档的结构、阅读顺序、使用顺序、依赖关系和阶段性最小引用集合。
3. 本页本身不是全部细节的承载体，但它定义了“在某个阶段应以哪些文档为准”。

执行时的约定是：

**只要引用本页，就等价于引用了本页所指定的该阶段适用文档集合。**

因此，本页承担两层职责：

1. 导航页
2. 单引用入口页

---

## 2. 使用约定

若在讨论、任务下发、协作实施、阶段收口时只引用一个文档，应统一引用：

[simpler_integration_plan_navigation.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_plan_navigation.md)

引用本页时，默认附带以下含义：

1. 本页是当前 `simpler` 接入计划的统一入口。
2. 当前阶段的执行依据，不是由引用者口头补充，而是由本页“阶段执行入口”一节规定。
3. 若本页与下游配套文档存在冲突，以更具体、更低层的阶段文档为准。
4. 若需要改变阶段规则，应先更新对应配套文档，再更新本页中的阶段入口说明。

---

## 3. 当前主线与统一结论

当前实施主线固定为：

1. 先做 `M0`
2. 再做 `M1`
3. 再做 `M2`
4. 再做 `M3`
5. 再做 `M4`
6. 再做 `M5`
7. 然后才讨论 `M6-M8`

当前统一结论：

1. 第一阶段主线是 `simpler -> ChipBackend -> simulator` 的 host-side 接入闭环。
2. 当前主线不以 guest 集成为关键路径。
3. 当前 `M1` 的唯一最小 workload 已冻结为 `host_build_graph/vector_example`。
4. 当前 `M2` 和 `M3` 已有规则性文档，可用于接口与时序收口。
5. 当前文档体系已经可用于多人并行实施。
6. 双节点 workload 推荐按 `W1 -> W2 -> W3 -> W4` 路线推进。

---

## 4. 文档清单

当前计划文档包括：

1. 总体里程碑计划
   - [simpler_integration_milestone_execution_plan.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_milestone_execution_plan.md)
2. M1 最小 workload 定义
   - [simpler_integration_m1_min_workload_definition.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_m1_min_workload_definition.md)
3. M2 `ChipBackend` 对照与职责定义
   - [simpler_integration_m2_chipbackend_mapping.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_m2_chipbackend_mapping.md)
4. M3 copy / dispatch / completion 时序规则
   - [simpler_integration_m3_copy_completion_rules.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_m3_copy_completion_rules.md)
5. 实施分工与接口边界
   - [simpler_integration_work_split_and_interface_ownership.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_work_split_and_interface_ownership.md)
6. `M5` 验证矩阵模板
   - [simpler_integration_m5_validation_matrix_template.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_m5_validation_matrix_template.md)
7. 阶段结果记录模板
   - [simpler_integration_phase_result_record_template.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_phase_result_record_template.md)
8. 双节点 MVP workload 路线图
   - [simpler_dual_node_mvp_workload_roadmap.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_dual_node_mvp_workload_roadmap.md)

---

## 5. 阶段执行入口

这是本页最关键的一节。

当外部只引用本页时，必须按下表理解“当前阶段到底以哪些文档为准”。

### 5.1 `M0` 阶段

执行依据：

1. [simpler_integration_milestone_execution_plan.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_milestone_execution_plan.md)
2. [simpler_integration_work_split_and_interface_ownership.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_work_split_and_interface_ownership.md)

本阶段关注点：

1. 主线冻结
2. 非目标冻结
3. 角色分工冻结

### 5.2 `M1` 阶段

执行依据：

1. [simpler_integration_milestone_execution_plan.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_milestone_execution_plan.md)
2. [simpler_integration_m1_min_workload_definition.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_m1_min_workload_definition.md)
3. [simpler_integration_work_split_and_interface_ownership.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_work_split_and_interface_ownership.md)

本阶段关注点：

1. 跑通唯一最小 workload
2. 固定依赖 manifest
3. 收敛最小调用入口

### 5.3 `M2` 阶段

执行依据：

1. [simpler_integration_milestone_execution_plan.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_milestone_execution_plan.md)
2. [simpler_integration_m2_chipbackend_mapping.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_m2_chipbackend_mapping.md)
3. [simpler_integration_work_split_and_interface_ownership.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_work_split_and_interface_ownership.md)

本阶段关注点：

1. `ChipBackend` 与 `simpler` 方法对照
2. 句柄与任务标识模型
3. 生命周期、错误和 completion 语义

### 5.4 `M3` 阶段

执行依据：

1. [simpler_integration_milestone_execution_plan.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_milestone_execution_plan.md)
2. [simpler_integration_m2_chipbackend_mapping.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_m2_chipbackend_mapping.md)
3. [simpler_integration_m3_copy_completion_rules.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_m3_copy_completion_rules.md)
4. [simpler_integration_work_split_and_interface_ownership.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_work_split_and_interface_ownership.md)

本阶段关注点：

1. `h2d_copy -> dispatch -> d2h_copy -> completion` 的时序固定
2. execution completion 与 workload completion 的区分
3. 异常与 trace 规则

### 5.5 `M4` 阶段

执行依据：

1. [simpler_integration_milestone_execution_plan.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_milestone_execution_plan.md)
2. [simpler_integration_m2_chipbackend_mapping.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_m2_chipbackend_mapping.md)
3. [simpler_integration_m3_copy_completion_rules.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_m3_copy_completion_rules.md)
4. [simpler_integration_work_split_and_interface_ownership.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_work_split_and_interface_ownership.md)

本阶段关注点：

1. host-side `ChipBackend` 真接入
2. 不得 fallback 回 `stub`
3. 最小 workload 从 simulator 真实发起并完成

### 5.6 `M5` 阶段

执行依据：

1. [simpler_integration_milestone_execution_plan.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_milestone_execution_plan.md)
2. [simpler_integration_m5_validation_matrix_template.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_m5_validation_matrix_template.md)
3. [simpler_integration_phase_result_record_template.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_phase_result_record_template.md)
4. [simpler_integration_work_split_and_interface_ownership.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_work_split_and_interface_ownership.md)

本阶段关注点：

1. 验证矩阵
2. 阶段准入门槛
3. 阶段结果记录

### 5.7 `M6-M8` 阶段

执行依据：

1. [simpler_integration_milestone_execution_plan.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_milestone_execution_plan.md)
2. [simpler_dual_node_mvp_workload_roadmap.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_dual_node_mvp_workload_roadmap.md)
3. 视所选主线补充引用相关结果记录和后续决策文档

本阶段关注点：

1. 节点集成形态决策
2. guest 预案或落地
3. 与当前 UB/QEMU 平台的深度对齐
4. 双节点 workload 按 `W1 -> W2 -> W3 -> W4` 顺序推进

---

## 6. 冲突裁决规则

当本页被作为单引用入口使用时，冲突裁决顺序固定如下：

1. 最具体的阶段文档优先于总体计划
2. 总体计划优先于导航性描述
3. 分工文档负责“谁说了算”，不负责重写技术语义
4. 验证模板负责“怎么验、怎么记”，不负责改变阶段目标

举例：

1. 若 `M1` 具体 workload 与总体计划中的泛化描述不同，以 `M1` 文档为准。
2. 若 `M3` 的 completion 规则与某人临时口头说法不同，以 `M3` 文档为准。
3. 若某项接口归属有争议，以分工文档为准。

---

## 7. 快速执行指引

如果接手者不想先通读全部文档，可直接按下面方式进入。

### 情况 A：我要启动实施

先看：

1. 本页
2. [simpler_integration_milestone_execution_plan.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_milestone_execution_plan.md)
3. [simpler_integration_work_split_and_interface_ownership.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_work_split_and_interface_ownership.md)

### 情况 B：我要做 `M1`

先看：

1. 本页
2. [simpler_integration_m1_min_workload_definition.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_m1_min_workload_definition.md)

### 情况 C：我要做 `M2`

先看：

1. 本页
2. [simpler_integration_m2_chipbackend_mapping.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_m2_chipbackend_mapping.md)

### 情况 D：我要做 `M3/M4`

先看：

1. 本页
2. [simpler_integration_m2_chipbackend_mapping.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_m2_chipbackend_mapping.md)
3. [simpler_integration_m3_copy_completion_rules.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_m3_copy_completion_rules.md)

### 情况 E：我要做阶段验收

先看：

1. 本页
2. [simpler_integration_m5_validation_matrix_template.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_m5_validation_matrix_template.md)
3. [simpler_integration_phase_result_record_template.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_phase_result_record_template.md)

### 情况 F：我要做双节点 workload 路线推进

先看：

1. 本页
2. [simpler_dual_node_mvp_workload_roadmap.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_dual_node_mvp_workload_roadmap.md)
3. 当前阶段对应的实施文档

---

## 8. 阅读顺序

第一次进入该主题时，建议严格按以下顺序阅读。

### 第一步：看总体主线

先看：

[simpler_integration_milestone_execution_plan.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_milestone_execution_plan.md)

目的：

1. 理解项目主线是什么
2. 理解当前阶段不做什么
3. 理解里程碑顺序和进入条件

如果这一步没看清，就不要直接跳 M1-M3 细节。

### 第二步：看 M1 最小工作负载

再看：

[simpler_integration_m1_min_workload_definition.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_m1_min_workload_definition.md)

目的：

1. 固定“什么叫跑通 `simpler`”
2. 固定最小 workload 和依赖清单要求
3. 避免 M1 口径漂移

### 第三步：看 M2 接口对照

然后看：

[simpler_integration_m2_chipbackend_mapping.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_m2_chipbackend_mapping.md)

目的：

1. 固定 `ChipBackend` 与 `simpler` 的职责边界
2. 固定方法对照表
3. 固定句柄、生命周期、completion、错误翻译规则

### 第四步：看 M3 数据路径规则

然后看：

[simpler_integration_m3_copy_completion_rules.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_m3_copy_completion_rules.md)

目的：

1. 固定 copy / dispatch / completion 时序
2. 固定 execution completion 与 workload completion 的区别
3. 固定异常与 trace 口径

### 第五步：看实施分工

最后看：

[simpler_integration_work_split_and_interface_ownership.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_work_split_and_interface_ownership.md)

目的：

1. 固定谁对什么负责
2. 固定哪个接口由谁收口
3. 降低多人并行时反复扯皮的概率

---

## 9. 使用顺序

阅读顺序解决“先理解什么”，使用顺序解决“实施时先用哪份文档”。

实施时建议按以下顺序使用。

### 4.1 启动阶段

使用：

1. 总体里程碑计划
2. 实施分工与接口边界

目的：

1. 先固定目标、阶段和人员责任
2. 避免一开始就跨边界开工

### 4.2 M1 阶段

使用：

1. 总体里程碑计划
2. M1 最小 workload 定义

目的：

1. 固定唯一 workload 基线
2. 固定 M1 的 PASS/FAIL 口径

### 4.3 M2 阶段

使用：

1. 总体里程碑计划
2. M2 `ChipBackend` 对照与职责定义
3. 实施分工与接口边界

目的：

1. 固定接口对照
2. 固定方法语义归属
3. 固定谁有最终解释权

### 4.4 M3 阶段

使用：

1. 总体里程碑计划
2. M3 copy / dispatch / completion 时序规则
3. M2 `ChipBackend` 对照与职责定义

目的：

1. 固定数据路径
2. 固定 completion 含义
3. 固定异常归类

### 4.5 M4-M5 阶段

使用：

1. 总体里程碑计划
2. M2 文档
3. M3 文档
4. 实施分工文档

目的：

1. 按已冻结接口实现
2. 按已冻结时序实现
3. 按已冻结准入推进验证

### 4.6 M6 之后

M6 之后继续以总体里程碑计划为主，但只有在 M1-M5 收敛后，才进入 guest 与平台对齐问题。

---

## 10. 文档依赖关系

这几份文档之间有明确依赖关系。

### 5.1 总体计划是根文档

所有其他文档都依赖：

[simpler_integration_milestone_execution_plan.md](/Volumes/repos/pypto_workspace/simulator/docs/simpler_integration_milestone_execution_plan.md)

作用：

1. 决定阶段顺序
2. 决定哪些问题属于当前主线
3. 决定哪些问题属于后续阶段

### 5.2 M1 文档依赖总体计划

M1 文档用于细化“独立跑通”的定义，不重新定义总目标。

### 5.3 M2 文档依赖总体计划和 M1 结果

因为：

1. `ChipBackend` 接口映射必须建立在已知可运行的最小 workload 之上
2. 若 M1 未收敛，M2 会失去稳定落点

### 5.4 M3 文档依赖 M2 文档

因为：

1. 时序规则必须建立在方法边界已经冻结的前提下
2. 若 M2 方法语义未定，M3 的 completion 规则也无法固定

### 5.5 分工文档依赖总体计划，但对 M1-M3 都生效

因为：

1. 它不重新定义技术语义
2. 它只规定由谁对哪些技术语义收口

---

## 11. 进入实施前的最小检查表

在真正展开并行实施前，建议先检查以下事项。

1. 是否已经阅读总体计划
2. 是否已经指定各角色 owner
3. Runtime Owner 是否接受 M1 workload 基线
4. Simulator Owner 是否接受 M2 接口对照
5. Runtime Owner 与 Simulator Owner 是否共同接受 M3 时序规则
6. Validation Owner 是否接受按 M5 准入推进
7. 是否确认 guest 工作不进入当前关键路径

若上述任一项未完成，不建议进入多人并行编码。

---

## 12. 建议的最小会议顺序

若需要线下或线上快速对齐，建议按以下顺序开会，而不是一锅端讨论。

### 会议一：主线与分工冻结

材料：

1. 总体里程碑计划
2. 实施分工与接口边界

目标：

1. 固定当前主线
2. 固定 owner
3. 固定 M1-M5 不被 guest 打断

### 会议二：M1 workload 冻结

材料：

1. M1 最小 workload 定义

目标：

1. 只决定最小 workload
2. 只决定 M1 PASS/FAIL

### 会议三：M2-M3 接口与时序冻结

材料：

1. M2 `ChipBackend` 对照与职责定义
2. M3 copy / dispatch / completion 时序规则

目标：

1. 固定接口对照
2. 固定 completion 含义
3. 固定 copy / dispatch 先后关系

---

## 13. 不推荐的使用方式

以下方式都不推荐。

1. 不看总体计划，直接跳到 M2 或 M3。
2. 不看分工文档，直接让多人平行改接口。
3. M1 还没固定 workload，就开始设计 `ChipBackend` 全量适配。
4. M4 还没完成，就提前投入 guest 打包。
5. 把 `a2a3sim` 本地闭环误当成“已经接上当前模拟节点平台”。

---

## 14. 当前建议

如果现在准备进入实施，建议最先做的不是编码，而是：

1. 用本导航页组织一次文档对齐
2. 用分工文档固定 owner
3. 用 M1 文档冻结最小 workload
4. 然后再进入 M2/M3 的接口冻结

完成这一步后，再进入编码，返工概率会明显降低。
