# `simpler` 接入计划配套模板：M5 验证矩阵

## 1. 文档目的

本模板用于统一 `simpler` 接入计划在 M5 阶段的验证组织方式。

目标：

1. 固定验证分层
2. 固定每层的 PASS/FAIL 口径
3. 固定进入下一里程碑的准入门槛

本模板是执行模板，不是设计文档。

---

## 2. 使用方式

每进入一个实施阶段，基于本模板填写一版验证矩阵。

填写原则：

1. 先填验证层级，再填具体用例
2. 每个用例都必须有明确输入、步骤、预期结果
3. 每个用例都必须绑定所属里程碑
4. 每个用例都必须有明确 owner
5. 每个用例都必须能输出 `PASS / FAIL / BLOCKED`

---

## 3. 验证分层

当前建议固定以下验证层级。

### L1：`simpler` Standalone 基线验证

目的：

验证 `simpler` 自身在 host 环境中的最小闭环。

适用里程碑：

1. M1
2. M5

典型内容：

1. 最小 workload 单次运行
2. 重复运行稳定性
3. 依赖 manifest 完整性检查

---

### L2：`ChipBackend` 接口接通验证

目的：

验证 simulator 到 `simpler` 的后端接入是否真实成立。

适用里程碑：

1. M2
2. M4
3. M5

典型内容：

1. backend 初始化
2. device context 建立
3. `dispatch` 可真实发起
4. `poll_completion` 可返回真实状态

---

### L3：数据路径验证

目的：

验证 `h2d_copy -> dispatch -> d2h_copy -> completion` 的完整链路。

适用里程碑：

1. M3
2. M4
3. M5

典型内容：

1. `h2d_copy` 成功路径
2. execution 成功路径
3. `d2h_copy` 成功路径
4. workload 最终成功判定

---

### L4：异常与鲁棒性验证

目的：

验证失败路径、超时、晚到 completion 等异常场景的可判定性。

适用里程碑：

1. M3
2. M4
3. M5

典型内容：

1. copy failure
2. execution failure
3. timeout
4. late completion

---

### L5：非回归验证

目的：

验证新链路引入后，不破坏现有主链路。

适用里程碑：

1. M5
2. 后续所有阶段

典型内容：

1. 现有 `linqu_*` demo 不回归
2. 现有 simulator 主链路不回归

---

## 4. 验证矩阵模板

按下表填写。

| Case ID | 层级 | 所属里程碑 | 用例名称 | 目标 | 前置条件 | 输入 | 执行步骤 | 预期结果 | 判定口径 | Owner | 状态 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| L1-001 | L1 | M1 | 最小 workload 单次运行 | 验证 `simpler` 独立最小闭环 | 构建完成，依赖齐全 | 固定输入数据 | 按最小运行命令执行一次 | 返回成功，输出正确 | 结果可程序化校验 | `TBD` | `TBD` |
| L1-002 | L1 | M1/M5 | 最小 workload 20 轮稳定性 | 验证最小 workload 稳定性 | L1-001 通过 | 固定输入数据 | 连续执行 20 轮 | 20 轮全成功，输出一致 | 任一轮失败即 FAIL | `TBD` | `TBD` |
| L2-001 | L2 | M4/M5 | backend 初始化 | 验证 backend 可初始化 | 配置启用真实 backend | backend 配置 | 执行 backend init | 初始化成功 | 无 fallback 到 stub | `TBD` | `TBD` |
| L2-002 | L2 | M4/M5 | 单次 dispatch | 验证 `dispatch` 真实落到 `simpler` | backend init 成功 | 固定 workload 请求 | 发起一次 dispatch | execution 被真实执行 | completion 来自真实 backend | `TBD` | `TBD` |
| L3-001 | L3 | M4/M5 | 完整数据路径成功 | 验证 copy + dispatch + completion 闭环 | backend 可用 | 固定输入数据 | `h2d -> dispatch -> d2h` | workload 成功完成 | host 结果可读且正确 | `TBD` | `TBD` |
| L4-001 | L4 | M5 | `h2d_copy` 失败分类 | 验证 copy 失败分类 | 可触发 copy failure | 异常输入或故障注入 | 执行 `h2d_copy` | workload 失败 | 明确归类为 copy failure | `TBD` | `TBD` |
| L4-002 | L4 | M5 | execution timeout 分类 | 验证 timeout 分类 | 可触发 timeout | 固定 workload | 发起 dispatch 并等待超时 | 返回 timeout | 不折叠为 generic failure | `TBD` | `TBD` |
| L5-001 | L5 | M5 | 现有 demo 非回归 | 验证现有主链路不回归 | 当前基线可运行 | 现有 demo 命令集 | 跑既有 demo | 与原基线一致 | 任一主链路破坏即 FAIL | `TBD` | `TBD` |

---

## 5. 每层最小覆盖要求

在进入下一里程碑前，建议至少满足以下覆盖要求。

### M1 -> M2 前

必须通过：

1. L1-001
2. L1-002

### M2/M3 -> M4 前

必须通过：

1. 至少 1 个 L2 用例
2. 至少 1 个 L3 用例设计已冻结

### M4 -> M5 前

必须通过：

1. 全部核心 L2 用例
2. 至少 1 个核心 L3 用例

### M5 -> M6 前

必须通过：

1. 全部 L1 核心用例
2. 全部 L2 核心用例
3. 全部 L3 核心用例
4. 至少 2 个 L4 异常用例
5. 至少 1 个 L5 非回归用例

---

## 6. PASS / FAIL / BLOCKED 口径

### PASS

满足：

1. 用例执行完成
2. 结果符合预期
3. 输出证据完整

### FAIL

满足任一项：

1. 结果不符合预期
2. 状态分类不符合文档规定
3. 输出证据不足以证明通过

### BLOCKED

满足任一项：

1. 前置条件未满足
2. 依赖接口未冻结
3. 环境问题导致无法执行

固定规则：

1. `BLOCKED` 不能算通过
2. `BLOCKED` 必须记录阻塞原因和解除条件

---

## 7. 结果证据要求

每个用例至少要保留以下证据中的一种或多种：

1. 命令行输出摘要
2. 关键日志摘要
3. 结果文件
4. trace 摘要
5. 对应阶段结果记录文档链接

不接受：

1. 仅口头说明“本地跑过”
2. 仅口头说明“看起来没问题”

---

## 8. 使用建议

1. Validation Owner 负责维护本表
2. Runtime Owner 和 Simulator Owner 负责补齐各自层的前置条件和执行入口
3. 架构 Owner 负责用本表判断是否允许进入下一阶段

