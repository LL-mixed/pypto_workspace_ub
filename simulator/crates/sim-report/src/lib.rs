//! Serializable report schema for simulator outputs.

use serde::{Deserialize, Serialize};
use sim_core::{CqHandle, HealthStatus, SegmentHandle, SimEvent};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HostReport {
    pub id: u32,
    pub node_id: u64,
    pub health: HealthStatus,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct UbpuReport {
    pub id: u32,
    pub node_id: u64,
    pub host_id: u32,
    pub health: HealthStatus,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EntityReport {
    pub id: u32,
    pub eid: u32,
    pub ubpu_id: u32,
    pub health: HealthStatus,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DomainReport {
    pub id: u32,
    pub label: String,
    pub node_id: u64,
    pub hosts: Vec<u32>,
    pub health: HealthStatus,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TopologyReport {
    pub hosts_count: usize,
    pub ubpus_count: usize,
    pub entities_count: usize,
    pub domains_count: usize,
    pub hosts: Vec<HostReport>,
    pub ubpus: Vec<UbpuReport>,
    pub entities: Vec<EntityReport>,
    pub domains: Vec<DomainReport>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct UapiDemoReport {
    pub hosts_count: usize,
    pub ubpus_count: usize,
    pub entities_count: usize,
    pub domains_count: usize,
    pub cq: CqHandle,
    pub segment: SegmentHandle,
    pub health: HealthStatus,
    pub events: Vec<SimEvent>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct WorkloadRunReport {
    pub workload_kind: String,
    pub requests_total: u64,
    pub blocks_total: u64,
    pub hits: u64,
    pub misses: u64,
    pub promotions: u64,
    pub evictions: u64,
    pub completions: u64,
    pub events: Vec<SimEvent>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CliReport {
    pub scenario_name: String,
    pub group: Option<String>,
    pub variant: Option<String>,
    pub logical_system: String,
    pub scenario_file: String,
    pub topology: TopologyReport,
    pub runtime_events: Vec<SimEvent>,
    pub workload_report: WorkloadRunReport,
    pub uapi_report: UapiDemoReport,
}
