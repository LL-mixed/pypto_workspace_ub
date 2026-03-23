//! Guest-visible UAPI surface placeholders.

use std::collections::{HashMap, VecDeque};

use sim_core::{
    CompletionEvent, CompletionSource, CompletionStatus, CqHandle, EntityId, HealthStatus,
    IoOpcode, IoSubmitReq, SegmentHandle, SimError,
};
use sim_services::block::BlockServiceStub;
use sim_topology::{SimTopology, TopologySnapshot};

pub trait GuestUapiSurface {
    fn query_topology(&self) -> TopologySnapshot;
    fn create_segment(&mut self, bytes: u64) -> Result<SegmentHandle, SimError>;
    fn register_cq(&mut self) -> Result<CqHandle, SimError>;
    fn submit_io(&mut self, req: IoSubmitReq) -> Result<u64, SimError>;
    fn poll_cq(&self, cq: CqHandle) -> Vec<CompletionEvent>;
    fn get_health(&self, entity: EntityId) -> Result<HealthStatus, SimError>;
}

#[derive(Debug)]
pub struct LocalGuestUapiSurface {
    topology: SimTopology,
    block_service: BlockServiceStub,
    next_segment_id: u64,
    next_cq_id: u32,
    cq_events: HashMap<CqHandle, VecDeque<CompletionEvent>>,
}

impl LocalGuestUapiSurface {
    pub fn new(topology: SimTopology) -> Self {
        Self {
            topology,
            block_service: BlockServiceStub::new(),
            next_segment_id: 0,
            next_cq_id: 0,
            cq_events: HashMap::new(),
        }
    }

    fn default_cq(&self) -> Result<CqHandle, SimError> {
        self.cq_events
            .keys()
            .next()
            .copied()
            .ok_or(SimError::NotFound("completion queue"))
    }

    fn enqueue_to_cq(&mut self, event: CompletionEvent) -> Result<(), SimError> {
        let cq = self.default_cq()?;
        let queue = self
            .cq_events
            .get_mut(&cq)
            .ok_or(SimError::NotFound("completion queue"))?;
        queue.push_back(event);
        Ok(())
    }

    pub fn drain_cq(&mut self, cq: CqHandle) -> Vec<CompletionEvent> {
        self.cq_events
            .get_mut(&cq)
            .map(|queue| queue.drain(..).collect())
            .unwrap_or_default()
    }
}

impl GuestUapiSurface for LocalGuestUapiSurface {
    fn query_topology(&self) -> TopologySnapshot {
        self.topology.snapshot()
    }

    fn create_segment(&mut self, bytes: u64) -> Result<SegmentHandle, SimError> {
        if bytes == 0 {
            return Err(SimError::InvalidInput("segment bytes must be positive"));
        }
        self.next_segment_id += 1;
        Ok(SegmentHandle(self.next_segment_id))
    }

    fn register_cq(&mut self) -> Result<CqHandle, SimError> {
        self.next_cq_id += 1;
        let cq = CqHandle(self.next_cq_id);
        self.cq_events.entry(cq).or_default();
        Ok(cq)
    }

    fn submit_io(&mut self, req: IoSubmitReq) -> Result<u64, SimError> {
        match req.opcode {
            IoOpcode::ReadBlock => {
                let block = req.block.ok_or(SimError::InvalidInput("missing block hash"))?;
                let handle = self.block_service.submit_read(
                    sim_runtime::BlockReadReq {
                        task: req.task,
                        block,
                    },
                    req.op_id,
                )?;
                for event in self.block_service.poll_ready(req.op_id) {
                    self.enqueue_to_cq(event)?;
                }
                Ok(handle.0)
            }
            IoOpcode::WriteBlock => {
                let block = req.block.ok_or(SimError::InvalidInput("missing block hash"))?;
                let handle = self.block_service.submit_write(
                    sim_runtime::BlockWriteReq {
                        task: req.task,
                        block,
                    },
                    req.op_id,
                )?;
                for event in self.block_service.poll_ready(req.op_id) {
                    self.enqueue_to_cq(event)?;
                }
                Ok(handle.0)
            }
            IoOpcode::Dispatch => {
                let event = CompletionEvent {
                    op_id: req.op_id,
                    task: req.task,
                    source: CompletionSource::GuestUapi,
                    status: CompletionStatus::Success,
                    finished_at: req.op_id,
                };
                self.enqueue_to_cq(event)?;
                Ok(req.op_id)
            }
        }
    }

    fn poll_cq(&self, cq: CqHandle) -> Vec<CompletionEvent> {
        self.cq_events
            .get(&cq)
            .map(|queue| queue.iter().cloned().collect())
            .unwrap_or_default()
    }

    fn get_health(&self, entity: EntityId) -> Result<HealthStatus, SimError> {
        self.topology
            .entities
            .iter()
            .find(|e| e.id == entity)
            .map(|e| e.health)
            .ok_or(SimError::NotFound("entity"))
    }
}

#[cfg(test)]
mod tests {
    use super::{GuestUapiSurface, LocalGuestUapiSurface};
    use sim_config::ScenarioConfig;
    use sim_core::{BlockHash, CompletionStatus, IoOpcode, IoSubmitReq};
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

    fn test_surface() -> LocalGuestUapiSurface {
        let config = ScenarioConfig::from_yaml_str(VALID_YAML).expect("valid config");
        let topology = SimTopology::from_config(&config).expect("topology");
        LocalGuestUapiSurface::new(topology)
    }

    #[test]
    fn local_guest_uapi_can_submit_write_and_drain_completion() {
        let mut surface = test_surface();
        let cq = surface.register_cq().expect("register cq");

        surface
            .submit_io(IoSubmitReq {
                op_id: 11,
                task: None,
                entity: 0,
                opcode: IoOpcode::WriteBlock,
                segment: None,
                block: Some(BlockHash("block-1".into())),
            })
            .expect("submit write");

        let events = surface.drain_cq(cq);
        assert_eq!(events.len(), 1);
        assert_eq!(events[0].status, CompletionStatus::Success);
    }
}
