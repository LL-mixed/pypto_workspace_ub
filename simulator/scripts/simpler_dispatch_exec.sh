#!/bin/sh
set -eu

python_bin="${SIMPLER_PYTHON:-python3}"
repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
simpler_root="${SIMPLER_PROJECT_ROOT:-$repo_root/modules/simpler}"

dispatch_mode="${SIMPLER_DISPATCH_MODE:-}"
runner_id="${SIMPLER_DISPATCH_RUNNER_ID:-}"
callable_name="${SIMPLER_DISPATCH_CALLABLE_NAME:-}"
argset_kind="${SIMPLER_DISPATCH_ARGSET_KIND:-}"
platform="${SIMPLER_DISPATCH_PLATFORM:-}"

resolve_execution_spec() {
    case "${runner_id}:${callable_name}:${argset_kind}" in
        "tmrb_vector_example:"*":tmrb_vector_example")
            entrypoint="examples/scripts/run_example.py"
            kernels="examples/a2a3/tensormap_and_ringbuffer/vector_example/kernels"
            golden="examples/a2a3/tensormap_and_ringbuffer/vector_example/golden.py"
            ;;
        "host_matmul_example:"*":host_matmul_example")
            entrypoint="examples/scripts/run_example.py"
            kernels="examples/a2a3/host_build_graph/matmul/kernels"
            golden="examples/a2a3/host_build_graph/matmul/golden.py"
            ;;
        "host_vector_example:"*":host_vector_example")
            entrypoint="examples/scripts/run_example.py"
            kernels="examples/a2a3/host_build_graph/vector_example/kernels"
            golden="examples/a2a3/host_build_graph/vector_example/golden.py"
            ;;
        *)
            echo "unsupported simpler execution spec: runner_id=${runner_id} callable_name=${callable_name} argset_kind=${argset_kind}" >&2
            exit 2
            ;;
    esac
}

if [ -z "$dispatch_mode" ] || [ -z "$runner_id" ]; then
    echo "missing simpler dispatch executor configuration" >&2
    exit 2
fi

cd "$simpler_root"

case "$dispatch_mode" in
    example)
        if [ -z "$callable_name" ] || [ -z "$argset_kind" ]; then
            echo "missing simpler callable resolution for example execution" >&2
            exit 2
        fi
        resolve_execution_spec
        if [ -z "$platform" ] || [ -z "$entrypoint" ] || [ -z "$kernels" ] || [ -z "$golden" ]; then
            echo "missing simpler example execution arguments" >&2
            exit 2
        fi
        exec "$python_bin" "$entrypoint" \
            -k "$kernels" \
            -g "$golden" \
            -p "$platform"
        ;;
    *)
        echo "unsupported simpler dispatch mode: $dispatch_mode" >&2
        exit 2
        ;;
esac
