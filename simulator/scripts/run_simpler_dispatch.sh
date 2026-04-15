#!/bin/sh
set -eu

python_bin="${SIMPLER_PYTHON:-python3}"
repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
simpler_root="${SIMPLER_PROJECT_ROOT:-$repo_root/modules/simpler}"
executor_script="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/simpler_dispatch_exec.sh"

manifest_version=""
op_id=""
task_id=""
function_name=""
target_level=""
target_node=""
input_segment_count=""
profile=""
platform=""
runtime_variant=""
callable_hint=""
manifest=""

resolve_runner_spec() {
    case "${profile}:${runtime_variant}" in
        "tmrb_vector:tensormap_and_ringbuffer")
            dispatch_mode="example"
            runner_id="tmrb_vector_example"
            entrypoint="examples/scripts/run_example.py"
            kernels="examples/a2a3/tensormap_and_ringbuffer/vector_example/kernels"
            golden="examples/a2a3/tensormap_and_ringbuffer/vector_example/golden.py"
            ;;
        "host_matmul:host_build_graph")
            dispatch_mode="example"
            runner_id="host_matmul_example"
            entrypoint="examples/scripts/run_example.py"
            kernels="examples/a2a3/host_build_graph/matmul/kernels"
            golden="examples/a2a3/host_build_graph/matmul/golden.py"
            ;;
        "host_vector:host_build_graph"|":")
            dispatch_mode="example"
            runner_id="host_vector_example"
            entrypoint="examples/scripts/run_example.py"
            kernels="examples/a2a3/host_build_graph/vector_example/kernels"
            golden="examples/a2a3/host_build_graph/vector_example/golden.py"
            ;;
        *)
            echo "unsupported simpler runner spec: profile=${profile} runtime_variant=${runtime_variant}" >&2
            exit 2
            ;;
    esac
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --manifest)
            manifest="$2"
            shift 2
            ;;
        --profile)
            profile="$2"
            shift 2
            ;;
        --platform)
            platform="$2"
            shift 2
            ;;
        --runtime-variant)
            runtime_variant="$2"
            shift 2
            ;;
        --callable-hint)
            callable_hint="$2"
            shift 2
            ;;
        *)
            echo "unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

if [ -n "$manifest" ]; then
    while IFS='=' read -r key value; do
        case "$key" in
            MANIFEST_VERSION) manifest_version="$value" ;;
            PROFILE) profile="$value" ;;
            OP_ID) op_id="$value" ;;
            TASK_ID) task_id="$value" ;;
            FUNCTION_NAME) function_name="$value" ;;
            TARGET_LEVEL) target_level="$value" ;;
            TARGET_NODE) target_node="$value" ;;
            INPUT_SEGMENT_COUNT) input_segment_count="$value" ;;
            PLATFORM) platform="$value" ;;
            RUNTIME_VARIANT) runtime_variant="$value" ;;
            CALLABLE_HINT) callable_hint="$value" ;;
            "") ;;
            *)
                echo "unknown manifest key: $key" >&2
                exit 2
                ;;
        esac
    done < "$manifest"
fi

if [ -n "$manifest_version" ] && [ "$manifest_version" != "1" ]; then
    echo "unsupported manifest version: $manifest_version" >&2
    exit 2
fi

resolve_runner_spec

if [ -z "$platform" ] || [ -z "$kernels" ] || [ -z "$golden" ]; then
    echo "missing required simpler dispatch arguments" >&2
    exit 2
fi

SIMPLER_DISPATCH_OP_ID="$op_id" \
SIMPLER_DISPATCH_TASK_ID="$task_id" \
SIMPLER_DISPATCH_FUNCTION_NAME="$function_name" \
SIMPLER_DISPATCH_TARGET_LEVEL="$target_level" \
SIMPLER_DISPATCH_TARGET_NODE="$target_node" \
SIMPLER_DISPATCH_INPUT_SEGMENT_COUNT="$input_segment_count" \
SIMPLER_DISPATCH_PROFILE="$profile" \
SIMPLER_RUNTIME_VARIANT="$runtime_variant" \
SIMPLER_CALLABLE_HINT="$callable_hint" \
SIMPLER_DISPATCH_MODE="$dispatch_mode" \
SIMPLER_DISPATCH_RUNNER_ID="$runner_id" \
SIMPLER_DISPATCH_ENTRYPOINT="$entrypoint" \
SIMPLER_DISPATCH_PLATFORM="$platform" \
SIMPLER_DISPATCH_KERNELS="$kernels" \
SIMPLER_DISPATCH_GOLDEN="$golden" \
SIMPLER_PYTHON="$python_bin" \
SIMPLER_PROJECT_ROOT="$simpler_root" \
exec "$executor_script"
