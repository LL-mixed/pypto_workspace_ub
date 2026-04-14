# `simpler` 双节点 MVP Workload 路线图

## 1. 文档目的

本路线图用于定义在以下约束下的双节点 MVP workload 递进顺序：

1. `L0-L2` 由 `simpler` 承载
2. `L3+` 由当前 simulator 承载
3. 先做小 workload，逐步逼近目标 workload
4. 每一步都必须有明确的前提、验收条件和进入下一步的门槛

本路线图不替代 `simpler` 接入主计划，而是建立在接入主计划之上的 workload 演进路径。

---

## 2. 适用前提

本路线图默认以下前提成立：

1. `simpler -> ChipBackend -> simulator` 的主接入路线仍是关键路径
2. 不直接从完整 `rust_llm_server_mvp` 开始
3. 双节点 workload 的目标是逐层验证：
   - 双节点数据可见性
   - 远端数据访问
   - 最小 compute 闭环
   - host-tier cache / routing 语义
   - 最终 workload 承载能力

---

## 3. 总体顺序

双节点 workload 固定按以下顺序推进：

1. `W1`: `shmem` ping-pong / mailbox
2. `W2`: `block read -> tiny compute`
3. `W3`: `remote block fetch + host-tier cache fill`
4. `W4`: `rust_llm_server_mvp` 最小 profile

固定原则：

1. 不跳步
2. 当前一步未通过，不进入下一步
3. 每一步只引入一类主要新增复杂度

---

## 4. 为什么采用这个顺序

### 4.1 `W1 -> W2`

从“共享内存可见 + 最小 compute”升级为“远端块访问 + 最小 compute”。

新增复杂度仅包括：

1. 远端 block 寻址
2. block completion
3. I/O completion 与 compute completion 的区分

### 4.2 `W2 -> W3`

从“远端块读取成功”升级为“远端块读取后进入本地 host-tier cache”。

新增复杂度仅包括：

1. host-tier cache fill
2. miss / fetch / fill / 命中
3. 最小 route / tier 语义

### 4.3 `W3 -> W4`

从“小型 cache workload”升级为“真正的 LLM serving MVP”。

新增复杂度包括：

1. request flow
2. 多 block per request
3. 更完整的 host orchestration
4. fault / degraded profile
5. 更真实的 metrics / trace

---

## 5. Workload 路线定义

### W1: `shmem` ping-pong / mailbox

#### 目标

验证双节点最小跨节点数据面与最小 `simpler` compute 闭环是否成立。

#### 形态

1. Node A 在 `lingqu_shmem` 共享区写入一个小 payload
2. Node A 写入 ready flag
3. Node B 观察到 ready flag 后读取 payload
4. Node B 调用一个极小 `simpler` L2 compute 对 payload 做确定性变换
5. Node B 将结果写回共享区并设置 ACK
6. Node A 读取结果并校验

#### 涉及层级

1. `L2`: `simpler` 最小 compute
2. `L3`: host-side orchestration 与 shared region 建立
3. `L4`: 双节点 shared visibility 所依赖的最小 domain 语义

#### 主要验证点

1. 双节点共享区是否可建立
2. 共享区数据是否跨节点可见
3. `simpler` 是否可消费共享区中的最小输入
4. completion / ACK 是否形成闭环
5. 结果是否可程序化校验

#### 不验证的内容

1. block 设备访问
2. cache fill / promotion
3. 完整 request routing
4. 高层 DFS / DB 语义

#### 进入前前提

1. `ChipBackend` 最小接入已打通
2. 最小 `simpler` workload 已独立验证通过

#### 验收条件

1. Node A 与 Node B 可稳定完成一次共享区 ping-pong
2. Node B 的最小 compute 真实执行，而非 stub
3. 输出结果可程序化判定正确
4. 至少能区分 shared visibility 失败与 compute 失败

#### 进入下一步门槛

1. `W1` 稳定通过
2. 对失败能明确分层归因：
   - 共享区建立
   - 跨节点可见性
   - `simpler` compute
   - ACK / completion

---

### W2: `block read -> tiny compute`

#### 目标

验证双节点远端块数据读取与最小 `simpler` compute 的组合闭环。

#### 形态

1. Node A 发起对 Node B 暴露的 `lingqu_block` 设备的远端 read
2. 读取结果进入本地 tensor / buffer
3. Node A 调用最小 `simpler` compute 对 block 数据做确定性变换
4. 输出结果在 host 侧校验
5. 可选：把结果回写到另一块 block

#### 涉及层级

1. `L2`: `simpler` 最小 compute
2. `L3`: host-side I/O orchestration
3. `L4`: 跨节点 block 数据路径

#### 主要验证点

1. 远端 block read 是否成立
2. block 数据是否能进入本地 compute 输入域
3. block completion 与 compute completion 是否可区分
4. 输出结果是否可校验

#### 不验证的内容

1. 层级 cache 命中/填充
2. promotion / eviction
3. 多请求路由
4. 完整 request profile

#### 相比 `W1` 的新增复杂度

1. 引入远端 block addressing
2. 引入异步 block completion
3. 引入 I/O 和 compute 的先后约束

#### 进入前前提

1. `W1` 已通过
2. `lingqu_block` 的最小远端访问语义已可表达

#### 验收条件

1. 远端 block read 成功
2. 读取结果被最小 compute 正确消费
3. block completion 与 compute completion 可分别观测
4. 整体结果可程序化校验

#### 进入下一步门槛

1. `W2` 稳定通过
2. 能明确区分以下失败类别：
   - block read failure
   - block timeout
   - compute failure
   - result validation failure

---

### W3: `remote block fetch + host-tier cache fill`

#### 目标

验证双节点最小 host-tier cache / fetch / fill 语义，并开始贴近未来目标 workload。

#### 形态

1. Node A 收到一个最小请求
2. Node A 检查本地 host-tier cache
3. 若 miss，则从 Node B 的 block tier 拉取 1-4 个 block
4. 拉回后填入本地 host-tier cache
5. 调用最小 `simpler` compute 处理这些 block
6. 记录一次 hit / miss / fill 结果

#### 涉及层级

1. `L2`: `simpler` 最小 compute
2. `L3`: host-tier cache / orchestration
3. `L4`: 远端 block tier / domain fetch

#### 主要验证点

1. miss -> remote fetch -> local fill 是否成立
2. host-tier cache 是否可复用
3. 命中后是否可以绕过远端 fetch
4. 最小 hit / miss / fill trace 是否成立

#### 不验证的内容

1. 完整 LLM request 流
2. 大规模多请求并发
3. 全量 fault policy
4. DFS / DB 高层服务

#### 相比 `W2` 的新增复杂度

1. 引入 host-tier cache
2. 引入 fetch / fill / reuse
3. 引入最小 route / tier 语义

#### 进入前前提

1. `W2` 已通过
2. block read 到 local buffer 的路径已稳定

#### 验收条件

1. 首次 miss 会触发 remote fetch
2. fetch 结果可成功填入本地 cache
3. 后续相同请求可命中本地 cache
4. hit / miss / fill 三类结果可观测

#### 进入下一步门槛

1. `W3` 稳定通过
2. host-tier cache 语义已稳定
3. route / fetch / fill 的 trace 已足够解释行为

---

### W4: `rust_llm_server_mvp` 最小 profile

#### 目标

在前面三步成立后，验证当前 simulator + `simpler` 分层是否足以承载目标 workload 的最小 profile。

#### 形态

1. 使用最小请求流
2. 使用最小 block 数
3. 使用最小层级 cache / route / fetch 组合
4. 使用最小 metrics / trace 集

可参考的目标场景基线：

- [`mvp_2host_single_domain.yaml`](/Volumes/repos/pypto_workspace/ub_sim.git/scenarios/mvp_2host_single_domain.yaml)

#### 涉及层级

1. `L2`: `simpler` 承载最小 chip compute
2. `L3`: host orchestration / cache / request handling
3. `L4`: domain-tier remote data access

#### 主要验证点

1. 最小 request flow 是否跑通
2. block fetch 与 cache fill 是否协同成立
3. metrics / trace 是否足够解释行为
4. 最小 fault / degraded profile 是否可观察

#### 相比 `W3` 的新增复杂度

1. 引入 request-oriented workload 视角
2. 引入多 block per request
3. 引入更完整的 route / metrics / trace 要求

#### 进入前前提

1. `W3` 已通过
2. host-tier cache 语义已收敛
3. route / fill / compute 的组合行为已可解释

#### 验收条件

1. 最小 profile 请求可稳定跑通
2. metrics / trace / summary 可输出
3. 关键路径错误可分层归因
4. fault / degraded 至少有一条最小路径可演示

---

## 6. 为什么不直接从 `W4` 开始

不建议直接从 `rust_llm_server_mvp` 最小 profile 开始，原因如下：

1. 会同时引入 `simpler` 接入、双节点数据面、host-tier cache、route、metrics、fault 等多种复杂度
2. 一旦失败，很难判断问题落在：
   - `simpler` L2 接入
   - shared / block 数据面
   - host-tier cache
   - route 逻辑
   - workload 本身
3. 这样会显著放大调试成本，也会拖慢主线收敛

---

## 7. 与 `simpler` 接入主计划的关系

这条 workload 路线不替代 `simpler` 接入主计划，而是建立在主计划之上。

正确关系是：

1. 先完成 `simpler -> ChipBackend -> simulator` 的最小接入闭环
2. 再从 `W1` 开始做双节点最小 workload
3. 按 `W1 -> W2 -> W3 -> W4` 顺序推进

因此，本路线图属于：

**接入完成之后的 workload 演进路径**

而不是：

**接入主计划本身**

---

## 8. 建议的实施与验收节奏

建议按以下节奏推进：

1. 接入主线先收敛到 `M4/M5`
2. 然后开始 `W1`
3. `W1` 稳定后进入 `W2`
4. `W2` 稳定后进入 `W3`
5. `W3` 稳定后进入 `W4`

固定规则：

1. 每一步都需要独立验收记录
2. 每一步都需要独立 failure 分类
3. 不允许把下一步的复杂度提前带入当前步

---

## 9. 最终结论

双节点 MVP workload 的推荐推进顺序固定为：

1. `shmem` ping-pong / mailbox
2. `block read -> tiny compute`
3. `remote block fetch + host-tier cache fill`
4. `rust_llm_server_mvp` 最小 profile

这个顺序的价值在于：

1. 先验证最小跨节点可见性
2. 再验证远端数据访问
3. 再验证 host-tier cache 语义
4. 最后再承载目标 workload

这是当前约束下最小风险、最容易分层定位问题、也最适合做双节点 MVP 的路线。

