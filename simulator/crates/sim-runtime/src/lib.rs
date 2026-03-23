//! Runtime traits and orchestration glue.

use std::collections::{HashSet, VecDeque};

use sim_config::ScenarioConfig;
use sim_core::{
    BlockHash, BlockPlacement, CompletionEvent, CopyRequest, DispatchHandle, DispatchRequest,
    PlLevel, RouteDecision, RouteReason, ServiceOpHandle, SimEvent, SimTimestamp, TaskKey,
    TransferHandle,
};
use sim_topology::SimTopology;

pub trait EventSink {
    fn emit(&mut self, event: SimEvent);
}

#[derive(Debug, Default)]
pub struct VecEventSink {
    events: Vec<SimEvent>,
}

impl VecEventSink {
    pub fn into_events(self) -> Vec<SimEvent> {
        self.events
    }
}

impl EventSink for VecEventSink {
    fn emit(&mut self, event: SimEvent) {
        self.events.push(event);
    }
}

#[derive(Debug, Clone)]
pub struct BlockReadReq {
    pub task: Option<TaskKey>,
    pub block: BlockHash,
}

#[derive(Debug, Clone)]
pub struct BlockWriteReq {
    pub task: Option<TaskKey>,
    pub block: BlockHash,
}

#[derive(Debug, Clone)]
pub struct RouteRequest {
    pub task: TaskKey,
    pub current_level: sim_core::PlLevel,
    pub block: BlockHash,
}

#[derive(Debug, Clone)]
pub struct LookupResult {
    pub found: bool,
    pub placement: Option<BlockPlacement>,
}

#[derive(Debug, Clone)]
pub struct PromotionPlan {
    pub block: BlockHash,
}

#[derive(Debug, Clone)]
pub struct EvictionPlan {
    pub max_blocks: usize,
}

pub trait ChipBackend {
    fn dispatch(&self, req: DispatchRequest) -> Result<DispatchHandle, sim_core::SimError>;
    fn h2d_copy(&self, req: CopyRequest) -> Result<TransferHandle, sim_core::SimError>;
    fn d2h_copy(&self, req: CopyRequest) -> Result<TransferHandle, sim_core::SimError>;
    fn poll_completion(&self, now: SimTimestamp) -> Vec<CompletionEvent>;
}

pub trait BlockService {
    fn read(&self, req: BlockReadReq) -> Result<ServiceOpHandle, sim_core::SimError>;
    fn write(&self, req: BlockWriteReq) -> Result<ServiceOpHandle, sim_core::SimError>;
    fn poll_completion(&self, now: SimTimestamp) -> Vec<CompletionEvent>;
}

pub trait RoutePlanner {
    fn plan(
        &self,
        req: RouteRequest,
        topo: &SimTopology,
    ) -> Result<RouteDecision, sim_core::SimError>;
}

pub trait SimBlockStore {
    fn lookup(&self, block: &BlockHash) -> LookupResult;
    fn stage_insert(&mut self, plan: PromotionPlan) -> Result<(), sim_core::SimError>;
    fn evict(&mut self, plan: EvictionPlan) -> Result<Vec<BlockHash>, sim_core::SimError>;
}

pub trait RingRuntime {
    fn on_scope_enter(&mut self, task: &TaskKey);
    fn on_scope_exit(&mut self, task: &TaskKey);
    fn on_pl_free(&mut self, task: &TaskKey, block: &BlockHash);
}

#[derive(Debug)]
pub struct RecursiveRoutePlanner {
    pub hit_weight: f64,
    pub load_weight: f64,
    pub capacity_weight: f64,
}

impl RecursiveRoutePlanner {
    pub fn from_config(config: &ScenarioConfig) -> Self {
        Self {
            hit_weight: config.routing.hit_weight,
            load_weight: config.routing.load_weight,
            capacity_weight: config.routing.capacity_weight,
        }
    }

    fn choose_reason(&self, level: PlLevel) -> RouteReason {
        match level {
            PlLevel::L2 if self.hit_weight >= self.capacity_weight => RouteReason::LocalHit,
            PlLevel::L3 if self.capacity_weight >= self.load_weight => {
                RouteReason::CapacityPreferred
            }
            PlLevel::L4 if self.load_weight > self.capacity_weight => RouteReason::HealthPreferred,
            _ => RouteReason::RecursiveFallback,
        }
    }
}

impl Default for RecursiveRoutePlanner {
    fn default() -> Self {
        Self {
            hit_weight: 1.0,
            load_weight: 1.0,
            capacity_weight: 1.0,
        }
    }
}

impl RoutePlanner for RecursiveRoutePlanner {
    fn plan(
        &self,
        req: RouteRequest,
        topo: &SimTopology,
    ) -> Result<RouteDecision, sim_core::SimError> {
        let selected = match req.current_level {
            PlLevel::L2 => topo
                .ubpus
                .iter()
                .find(|ubpu| ubpu.health == sim_core::HealthStatus::Healthy)
                .map(|ubpu| (PlLevel::L2, ubpu.node_id, self.choose_reason(PlLevel::L2))),
            PlLevel::L3 => topo
                .hosts
                .iter()
                .find(|host| host.health == sim_core::HealthStatus::Healthy)
                .map(|host| (PlLevel::L3, host.node_id, self.choose_reason(PlLevel::L3))),
            _ => topo
                .domains
                .iter()
                .find(|domain| domain.health == sim_core::HealthStatus::Healthy)
                .map(|domain| (PlLevel::L4, domain.node_id, self.choose_reason(PlLevel::L4))),
        };

        match selected {
            Some((to_level, selected_node, reason)) => Ok(RouteDecision {
                from_level: req.current_level,
                to_level,
                selected_node,
                reason,
            }),
            None => Err(sim_core::SimError::NotImplemented),
        }
    }
}

#[derive(Debug, Clone)]
pub struct InMemoryBlockStore {
    placements: HashSet<BlockPlacement>,
    insertion_order: VecDeque<BlockHash>,
    capacity_blocks: usize,
    default_level: PlLevel,
    default_node: u64,
}

impl InMemoryBlockStore {
    pub fn new() -> Self {
        Self {
            placements: HashSet::new(),
            insertion_order: VecDeque::new(),
            capacity_blocks: usize::MAX,
            default_level: PlLevel::L2,
            default_node: 0,
        }
    }

    pub fn from_config(config: &ScenarioConfig) -> Self {
        Self {
            placements: HashSet::new(),
            insertion_order: VecDeque::new(),
            capacity_blocks: config.levels.l2_ubpu_tier.capacity_blocks as usize,
            default_level: PlLevel::L2,
            default_node: 0,
        }
    }

    pub fn capacity_blocks(&self) -> usize {
        self.capacity_blocks
    }
}

impl Default for InMemoryBlockStore {
    fn default() -> Self {
        Self::new()
    }
}

impl SimBlockStore for InMemoryBlockStore {
    fn lookup(&self, block: &BlockHash) -> LookupResult {
        let placement = self
            .placements
            .iter()
            .find(|placement| placement.block == *block)
            .cloned();

        LookupResult {
            found: placement.is_some(),
            placement,
        }
    }

    fn stage_insert(&mut self, plan: PromotionPlan) -> Result<(), sim_core::SimError> {
        let placement = BlockPlacement {
            block: plan.block.clone(),
            level: self.default_level,
            node: self.default_node,
        };

        if self.placements.insert(placement) {
            self.insertion_order.push_back(plan.block);
        }
        while self.placements.len() > self.capacity_blocks {
            let _ = self.evict(EvictionPlan { max_blocks: 1 })?;
        }
        Ok(())
    }

    fn evict(&mut self, plan: EvictionPlan) -> Result<Vec<BlockHash>, sim_core::SimError> {
        let mut evicted = Vec::new();

        for _ in 0..plan.max_blocks {
            let Some(block) = self.insertion_order.pop_front() else {
                break;
            };

            if let Some(placement) = self
                .placements
                .iter()
                .find(|placement| placement.block == block)
                .cloned()
            {
                self.placements.remove(&placement);
                evicted.push(block);
            }
        }

        Ok(evicted)
    }
}

#[cfg(test)]
mod tests {
    use super::{
        EvictionPlan, InMemoryBlockStore, PromotionPlan, RecursiveRoutePlanner, RoutePlanner,
        RouteRequest, SimBlockStore,
    };
    use sim_config::ScenarioConfig;
    use sim_core::{BlockHash, HierarchyCoord, LogicalSystemId, PlLevel, TaskKey};
    use sim_topology::SimTopology;

    const VALID_YAML: &str = r#"
scenario:
  name: mvp_2host_single_domain
  group: M
  variant: m_single_domain_mvp
  seed: 42
  duration_us: 1000000
  logical_system: llm-serving-mvp
platform:
  backend: qemu
  machine_profile: ub-host-minimal
  cpu_model: host
  memory_model: numa-sim
  device_model_mode: mixed
topology:
  hosts: 2
  ubpus_per_host: 2
  entities_per_ubpu: 2
  ub_domains:
    - id: domain0
      hosts: [0, 1]
  collapse:
    fabric: true
    global: true
ub_runtime:
  active_levels: [2, 3, 4]
  reserved_levels: [0, 1, 5, 6, 7]
  preserve_full_task_coord: true
pypto:
  enable_function_labels: true
  default_level: HOST
  allow_levels: [CHIP, HOST, CLUSTER_0]
  simpler_boundary:
    enabled: true
    chip_backend_mode: stub
    dispatch_latency_us: 15
  scope_runtime:
    enable_multi_layer_ring: true
    enable_pl_free: true
    max_scope_depth: 8
lingqu_data:
  shmem:
    enabled: true
    pe_count: 2
    default_latency_us: 3
  block:
    enabled: true
    devices:
      - uba: ssu0
        blocks: 1048576
        block_size: 4096
  dfs:
    enabled: true
    namespace_root: /
    metadata_latency_us: 20
    data_latency_us: 80
  db:
    enabled: true
    inline_value_limit: 64
    pipeline_batch_limit: 16
levels:
  l2_ubpu_tier:
    capacity_blocks: 1024
    high_watermark: 0.9
    low_watermark: 0.7
    hit_latency_us: 5
  l3_host_tier:
    capacity_blocks: 8192
    high_watermark: 0.9
    low_watermark: 0.7
    fetch_latency_us: 30
  l4_domain_tier:
    capacity_blocks: 65536
    high_watermark: 0.95
    low_watermark: 0.8
    fetch_latency_us: 80
routing:
  mode: recursive
  hit_weight: 10.0
  load_weight: 2.0
  capacity_weight: 1.0
workload:
  type: rust_llm_server_mvp
  profile: single_domain_basic
  qps: 2000
  unique_prefixes: 256
  blocks_per_request: 4
  function_label_mode: host_orchestration
faults:
  - type: host_degraded
    at_us: 300000
    host_id: 0
outputs:
  trace: true
  metrics_csv: true
  summary_json: true
  emit_task_coord_trace: true
  emit_data_service_trace: true
  emit_qemu_platform_trace: true
"#;

    fn test_task() -> TaskKey {
        TaskKey {
            logical_system: LogicalSystemId(1),
            coord: HierarchyCoord { levels: [0; 8] },
            scope_depth: 0,
            task_id: 7,
        }
    }

    fn test_topology() -> SimTopology {
        let config = ScenarioConfig::from_yaml_str(VALID_YAML).expect("valid config");
        SimTopology::from_config(&config).expect("topology build")
    }

    #[test]
    fn recursive_route_planner_picks_domain_for_l4_request() {
        let config = ScenarioConfig::from_yaml_str(VALID_YAML).expect("valid config");
        let planner = RecursiveRoutePlanner::from_config(&config);
        let topo = test_topology();

        let decision = planner
            .plan(
                RouteRequest {
                    task: test_task(),
                    current_level: PlLevel::L4,
                    block: BlockHash("block-a".into()),
                },
                &topo,
            )
            .expect("route decision");

        assert_eq!(decision.from_level, PlLevel::L4);
        assert_eq!(decision.to_level, PlLevel::L4);
        assert_eq!(decision.selected_node, topo.domains[0].node_id);
    }

    #[test]
    fn in_memory_block_store_supports_lookup_insert_and_evict() {
        let config = ScenarioConfig::from_yaml_str(VALID_YAML).expect("valid config");
        let mut store = InMemoryBlockStore::from_config(&config);
        let block = BlockHash("block-a".into());

        assert!(!store.lookup(&block).found);

        store
            .stage_insert(PromotionPlan {
                block: block.clone(),
            })
            .expect("insert");

        let lookup = store.lookup(&block);
        assert!(lookup.found);
        assert!(lookup.placement.is_some());

        let evicted = store.evict(EvictionPlan { max_blocks: 1 }).expect("evict");
        assert_eq!(evicted, vec![block.clone()]);
        assert!(!store.lookup(&block).found);
    }

    #[test]
    fn in_memory_block_store_uses_capacity_from_config() {
        let config = ScenarioConfig::from_yaml_str(VALID_YAML).expect("valid config");
        let store = InMemoryBlockStore::from_config(&config);
        assert_eq!(store.capacity_blocks(), 1024);
    }
}
