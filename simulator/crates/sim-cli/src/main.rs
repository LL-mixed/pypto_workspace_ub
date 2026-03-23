use anyhow::Context;
use sim_core::{
    BlockHash, HierarchyCoord, IoOpcode, IoSubmitReq, LogicalSystemId, PlLevel, SimEvent, TaskKey,
};
use sim_config::ScenarioConfig;
use sim_report::{
    CliReport, DomainReport, EntityReport, HostReport, TopologyReport, UapiDemoReport, UbpuReport,
};
use sim_runtime::{
    EventSink, EvictionPlan, InMemoryBlockStore, PromotionPlan, RecursiveRoutePlanner,
    RoutePlanner, RouteRequest, SimBlockStore, VecEventSink,
};
use sim_topology::SimTopology;
use sim_uapi::{GuestUapiSurface, LocalGuestUapiSurface};
use sim_workloads::run_minimal_workload;
use std::env;
use std::path::{Path, PathBuf};

fn main() -> anyhow::Result<()> {
    let scenario_path = scenario_path_from_args();
    let config = ScenarioConfig::from_yaml_file(&scenario_path).with_context(|| {
        format!(
            "failed to load scenario config from {}",
            scenario_path.display()
        )
    })?;
    let topology = SimTopology::from_config(&config).context("failed to build topology")?;
    let runtime_events =
        run_demo(&config, &topology).context("failed to run route/store/event demo")?;
    let workload_report =
        run_minimal_workload(&config, &topology).context("failed to run minimal workload")?;
    let uapi_report = run_uapi_demo(&topology).context("failed to run local uapi demo")?;

    let report = CliReport {
        scenario_name: config.scenario.name,
        group: config.scenario.group,
        variant: config.scenario.variant,
        logical_system: config.scenario.logical_system,
        scenario_file: scenario_path.display().to_string(),
        topology: topology_report(&topology),
        runtime_events,
        workload_report,
        uapi_report,
    };

    print_report(&report);

    Ok(())
}

fn scenario_path_from_args() -> PathBuf {
    env::args_os()
        .nth(1)
        .map(PathBuf::from)
        .unwrap_or_else(default_scenario_path)
}

fn default_scenario_path() -> PathBuf {
    Path::new("scenarios").join("mvp_2host_single_domain.yaml")
}

fn run_demo(config: &ScenarioConfig, topology: &SimTopology) -> anyhow::Result<Vec<SimEvent>> {
    let task = TaskKey {
        logical_system: LogicalSystemId(1),
        coord: HierarchyCoord { levels: [0; 8] },
        scope_depth: 0,
        task_id: 1,
    };
    let block = BlockHash("demo-block-0".to_string());

    let planner = RecursiveRoutePlanner::from_config(config);
    let mut store = InMemoryBlockStore::from_config(config);
    let mut sink = VecEventSink::default();

    sink.emit(SimEvent::TaskCreated {
        at: 0,
        task: task.clone(),
    });

    let decision = planner
        .plan(
            RouteRequest {
                task: task.clone(),
                current_level: PlLevel::L4,
                block: block.clone(),
            },
            topology,
        )
        .context("route planning failed")?;

    sink.emit(SimEvent::RoutePlanned {
        at: 1,
        task: task.clone(),
        decision: decision.clone(),
    });

    store
        .stage_insert(PromotionPlan {
            block: block.clone(),
        })
        .context("block insert failed")?;

    let placement = store
        .lookup(&block)
        .placement
        .context("block placement missing after insert")?;

    sink.emit(SimEvent::BlockPromoted {
        at: 2,
        block: block.clone(),
        placement,
    });

    let evicted = store
        .evict(EvictionPlan { max_blocks: 1 })
        .context("block eviction failed")?;

    if let Some(evicted_block) = evicted.into_iter().next() {
        sink.emit(SimEvent::BlockEvicted {
            at: 3,
            block: evicted_block,
            from: sim_core::BlockPlacement {
                block: block.clone(),
                level: PlLevel::L2,
                node: 0,
            },
        });
    }

    Ok(sink.into_events())
}

fn run_uapi_demo(topology: &SimTopology) -> anyhow::Result<UapiDemoReport> {
    let mut surface = LocalGuestUapiSurface::new(topology.clone());
    let topo = surface.query_topology();
    let mut events = Vec::new();

    let cq = surface.register_cq().context("register cq failed")?;

    let segment = surface
        .create_segment(4096)
        .context("create segment failed")?;

    let write_op = surface
        .submit_io(IoSubmitReq {
            op_id: 100,
            task: None,
            entity: 0,
            opcode: IoOpcode::WriteBlock,
            segment: Some(segment),
            block: Some(BlockHash("uapi-block-0".to_string())),
        })
        .context("submit write failed")?;

    let health = surface.get_health(0).context("get health failed")?;

    for completion in surface.drain_cq(cq) {
        events.push(SimEvent::CompletionObserved {
            at: completion.finished_at,
            completion,
        });
    }

    let read_op = surface
        .submit_io(IoSubmitReq {
            op_id: 101,
            task: None,
            entity: 0,
            opcode: IoOpcode::ReadBlock,
            segment: Some(segment),
            block: Some(BlockHash("uapi-block-0".to_string())),
        })
        .context("submit read failed")?;

    for completion in surface.drain_cq(cq) {
        events.push(SimEvent::CompletionObserved {
            at: completion.finished_at,
            completion,
        });
    }

    let _ = write_op;
    let _ = read_op;

    Ok(UapiDemoReport {
        hosts_count: topo.hosts,
        ubpus_count: topo.ubpus,
        entities_count: topo.entities,
        domains_count: topo.domains,
        cq,
        segment,
        health,
        events,
    })
}

fn print_report(report: &CliReport) {
    println!("scenario: {}", report.scenario_name);
    println!("group: {}", report.group.as_deref().unwrap_or("-"));
    println!("variant: {}", report.variant.as_deref().unwrap_or("-"));
    println!("logical_system: {}", report.logical_system);
    println!("scenario_file: {}", report.scenario_file);
    println!(
        "topology: hosts={} ubpus={} entities={} domains={}",
        report.topology.hosts_count,
        report.topology.ubpus_count,
        report.topology.entities_count,
        report.topology.domains_count
    );
    println!();
    println!("hosts:");
    for host in &report.topology.hosts {
        println!(
            "  host id={} node_id={} health={:?}",
            host.id, host.node_id, host.health
        );
    }

    println!("ubpus:");
    for ubpu in &report.topology.ubpus {
        println!(
            "  ubpu id={} node_id={} host_id={} health={:?}",
            ubpu.id, ubpu.node_id, ubpu.host_id, ubpu.health
        );
    }

    println!("entities:");
    for entity in &report.topology.entities {
        println!(
            "  entity id={} eid={} ubpu_id={} health={:?}",
            entity.id, entity.eid, entity.ubpu_id, entity.health
        );
    }

    println!("domains:");
    for domain in &report.topology.domains {
        println!(
            "  domain id={} label={} node_id={} hosts={:?} health={:?}",
            domain.id, domain.label, domain.node_id, domain.hosts, domain.health
        );
    }

    println!();
    println!("runtime_demo_events:");
    for event in &report.runtime_events {
        println!("  {:?}", event);
    }

    println!();
    println!("workload_report:");
    println!(
        "  kind={} requests={} blocks={} hits={} misses={} promotions={} evictions={} completions={}",
        report.workload_report.workload_kind,
        report.workload_report.requests_total,
        report.workload_report.blocks_total,
        report.workload_report.hits,
        report.workload_report.misses,
        report.workload_report.promotions,
        report.workload_report.evictions,
        report.workload_report.completions
    );
    println!("  events:");
    for event in &report.workload_report.events {
        println!("    {:?}", event);
    }

    println!();
    println!("report_json:");
    println!(
        "{}",
        serde_json::to_string_pretty(report).expect("report json serialization")
    );

    println!();
    println!("uapi_demo:");
    println!(
        "  snapshot => hosts={} ubpus={} entities={} domains={}",
        report.uapi_report.hosts_count,
        report.uapi_report.ubpus_count,
        report.uapi_report.entities_count,
        report.uapi_report.domains_count
    );
    println!("  cq => {:?}", report.uapi_report.cq);
    println!("  segment => {:?}", report.uapi_report.segment);
    println!("  health(entity=0) => {:?}", report.uapi_report.health);
    println!("  events:");
    for event in &report.uapi_report.events {
        println!("    {:?}", event);
    }
}

fn topology_report(topology: &SimTopology) -> TopologyReport {
    let snapshot = topology.snapshot();

    TopologyReport {
        hosts_count: snapshot.hosts,
        ubpus_count: snapshot.ubpus,
        entities_count: snapshot.entities,
        domains_count: snapshot.domains,
        hosts: topology
            .hosts
            .iter()
            .map(|host| HostReport {
                id: host.id,
                node_id: host.node_id,
                health: host.health,
            })
            .collect(),
        ubpus: topology
            .ubpus
            .iter()
            .map(|ubpu| UbpuReport {
                id: ubpu.id,
                node_id: ubpu.node_id,
                host_id: ubpu.host_id,
                health: ubpu.health,
            })
            .collect(),
        entities: topology
            .entities
            .iter()
            .map(|entity| EntityReport {
                id: entity.id,
                eid: entity.eid,
                ubpu_id: entity.ubpu_id,
                health: entity.health,
            })
            .collect(),
        domains: topology
            .domains
            .iter()
            .map(|domain| DomainReport {
                id: domain.id,
                label: domain.label.clone(),
                node_id: domain.node_id,
                hosts: domain.hosts.clone(),
                health: domain.health,
            })
            .collect(),
    }
}
