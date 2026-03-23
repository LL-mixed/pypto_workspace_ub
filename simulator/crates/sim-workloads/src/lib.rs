//! Workload harness entry points.

use sim_config::{ScenarioConfig, WorkloadConfig};
use sim_core::{
    BlockHash, HierarchyCoord, IoOpcode, IoSubmitReq, LogicalSystemId, PlLevel, SimEvent, TaskKey,
};
use sim_report::WorkloadRunReport;
use sim_runtime::{
    InMemoryBlockStore, PromotionPlan, RecursiveRoutePlanner, RoutePlanner, RouteRequest,
    SimBlockStore,
};
use sim_topology::SimTopology;
use sim_uapi::{GuestUapiSurface, LocalGuestUapiSurface};

pub fn run_minimal_workload(
    config: &ScenarioConfig,
    topology: &SimTopology,
) -> Result<WorkloadRunReport, sim_core::SimError> {
    let mut store = InMemoryBlockStore::from_config(config);
    let planner = RecursiveRoutePlanner::from_config(config);
    let mut surface = LocalGuestUapiSurface::new(topology.clone());
    let cq = surface.register_cq()?;

    let (workload_kind, requests_total, blocks_per_request, unique_prefixes) = match &config.workload
    {
        WorkloadConfig::HotsetLoop(cfg) => (
            "hotset_loop".to_string(),
            cfg.qps.min(4),
            cfg.blocks_per_request,
            cfg.unique_prefixes.max(1),
        ),
        WorkloadConfig::TraceReplay(_) => ("trace_replay".to_string(), 2, 1, 2),
        WorkloadConfig::RustLlmMvp(cfg) => (
            "rust_llm_server_mvp".to_string(),
            cfg.qps.min(4),
            cfg.blocks_per_request,
            cfg.unique_prefixes.max(1),
        ),
    };

    let mut report = WorkloadRunReport {
        workload_kind,
        requests_total,
        blocks_total: 0,
        hits: 0,
        misses: 0,
        promotions: 0,
        evictions: 0,
        completions: 0,
        events: Vec::new(),
    };

    for request_idx in 0..requests_total {
        let task = TaskKey {
            logical_system: LogicalSystemId(1),
            coord: HierarchyCoord { levels: [0; 8] },
            scope_depth: 0,
            task_id: request_idx + 1,
        };
        report.events.push(SimEvent::TaskCreated {
            at: request_idx,
            task: task.clone(),
        });

        for block_idx in 0..u64::from(blocks_per_request) {
            report.blocks_total += 1;
            let block = BlockHash(format!(
                "workload-block-{}",
                (request_idx + block_idx) % unique_prefixes.min(2)
            ));

            let lookup = store.lookup(&block);
            if lookup.found {
                report.hits += 1;
                continue;
            }

            report.misses += 1;
            let decision = planner.plan(
                RouteRequest {
                    task: task.clone(),
                    current_level: PlLevel::L4,
                    block: block.clone(),
                },
                topology,
            )?;
            report.events.push(SimEvent::RoutePlanned {
                at: request_idx + block_idx,
                task: task.clone(),
                decision,
            });

            store.stage_insert(PromotionPlan {
                block: block.clone(),
            })?;
            report.promotions += 1;

            if let Some(placement) = store.lookup(&block).placement {
                report.events.push(SimEvent::BlockPromoted {
                    at: request_idx + block_idx + 1,
                    block: block.clone(),
                    placement,
                });
            }

            surface.submit_io(IoSubmitReq {
                op_id: 1000 + request_idx * 10 + block_idx,
                task: Some(task.clone()),
                entity: 0,
                opcode: IoOpcode::WriteBlock,
                segment: None,
                block: Some(block),
            })?;

            let completions = surface.drain_cq(cq);
            report.completions += completions.len() as u64;
            for completion in completions {
                report.events.push(SimEvent::CompletionObserved {
                    at: completion.finished_at,
                    completion,
                });
            }
        }
    }

    let evicted = store.evict(sim_runtime::EvictionPlan { max_blocks: 1 })?;
    report.evictions += evicted.len() as u64;
    for block in evicted {
        report.events.push(SimEvent::BlockEvicted {
            at: requests_total + report.blocks_total,
            from: sim_core::BlockPlacement {
                block: block.clone(),
                level: PlLevel::L2,
                node: 0,
            },
            block,
        });
    }

    Ok(report)
}

#[cfg(test)]
mod tests {
    use super::run_minimal_workload;
    use sim_config::ScenarioConfig;
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
  qps: 3
  unique_prefixes: 2
  blocks_per_request: 2
  function_label_mode: host_orchestration
faults: []
outputs:
  trace: true
  metrics_csv: true
  summary_json: true
  emit_task_coord_trace: true
  emit_data_service_trace: true
  emit_qemu_platform_trace: true
"#;

    #[test]
    fn minimal_workload_runs_and_emits_events() {
        let config = ScenarioConfig::from_yaml_str(VALID_YAML).expect("config");
        let topology = SimTopology::from_config(&config).expect("topology");
        let report = run_minimal_workload(&config, &topology).expect("workload");

        assert_eq!(report.requests_total, 3);
        assert_eq!(report.blocks_total, 6);
        assert!(report.promotions > 0);
        assert!(report.completions > 0);
        assert!(!report.events.is_empty());
    }
}
