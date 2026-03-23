//! Host-side service simulation entry points.

pub mod block {
    use std::collections::{HashMap, VecDeque};

    use sim_core::{
        BlockHash, CompletionEvent, CompletionSource, CompletionStatus, ServiceOpHandle,
        SimTimestamp,
    };
    use sim_runtime::{BlockReadReq, BlockService, BlockWriteReq};

    #[derive(Debug, Default)]
    pub struct BlockServiceStub {
        blocks: HashMap<BlockHash, Vec<u8>>,
        completions: VecDeque<CompletionEvent>,
        next_op_id: u64,
    }

    impl BlockServiceStub {
        pub fn new() -> Self {
            Self::default()
        }

        fn next_handle(&mut self) -> ServiceOpHandle {
            self.next_op_id += 1;
            ServiceOpHandle(self.next_op_id)
        }

        pub fn submit_read(
            &mut self,
            req: BlockReadReq,
            now: SimTimestamp,
        ) -> Result<ServiceOpHandle, sim_core::SimError> {
            let handle = self.next_handle();
            let status = if self.blocks.contains_key(&req.block) {
                CompletionStatus::Success
            } else {
                CompletionStatus::RetryableFailure {
                    code: "block_miss".to_string(),
                }
            };

            self.completions.push_back(CompletionEvent {
                op_id: handle.0,
                task: req.task,
                source: CompletionSource::BlockService,
                status,
                finished_at: now,
            });

            Ok(handle)
        }

        pub fn submit_write(
            &mut self,
            req: BlockWriteReq,
            now: SimTimestamp,
        ) -> Result<ServiceOpHandle, sim_core::SimError> {
            let handle = self.next_handle();
            self.blocks.insert(req.block, Vec::new());
            self.completions.push_back(CompletionEvent {
                op_id: handle.0,
                task: req.task,
                source: CompletionSource::BlockService,
                status: CompletionStatus::Success,
                finished_at: now,
            });
            Ok(handle)
        }

        pub fn poll_ready(&mut self, _now: SimTimestamp) -> Vec<CompletionEvent> {
            self.completions.drain(..).collect()
        }
    }

    impl BlockService for BlockServiceStub {
        fn read(&self, _req: BlockReadReq) -> Result<ServiceOpHandle, sim_core::SimError> {
            Err(sim_core::SimError::NotImplemented)
        }

        fn write(&self, _req: BlockWriteReq) -> Result<ServiceOpHandle, sim_core::SimError> {
            Err(sim_core::SimError::NotImplemented)
        }

        fn poll_completion(&self, _now: SimTimestamp) -> Vec<CompletionEvent> {
            Vec::new()
        }
    }

    #[cfg(test)]
    mod tests {
        use super::BlockServiceStub;
        use sim_core::{BlockHash, CompletionStatus};
        use sim_runtime::{BlockReadReq, BlockWriteReq};

        #[test]
        fn block_service_stub_write_then_read_completes() {
            let mut svc = BlockServiceStub::new();
            let block = BlockHash("block-0".into());

            svc.submit_write(
                BlockWriteReq {
                    task: None,
                    block: block.clone(),
                },
                10,
            )
            .expect("write");
            let write_events = svc.poll_ready(10);
            assert_eq!(write_events.len(), 1);
            assert_eq!(write_events[0].status, CompletionStatus::Success);

            svc.submit_read(
                BlockReadReq {
                    task: None,
                    block,
                },
                20,
            )
            .expect("read");
            let read_events = svc.poll_ready(20);
            assert_eq!(read_events.len(), 1);
            assert_eq!(read_events[0].status, CompletionStatus::Success);
        }
    }
}
