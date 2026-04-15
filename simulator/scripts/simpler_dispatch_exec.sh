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

build_execution_spec() {
    case "${runner_id}:${callable_name}:${argset_kind}" in
        "tmrb_vector_example:"*":tmrb_vector_example")
            execution_mode="example"
            execution_entrypoint="examples/scripts/run_example.py"
            execution_kernels="examples/a2a3/tensormap_and_ringbuffer/vector_example/kernels"
            execution_golden="examples/a2a3/tensormap_and_ringbuffer/vector_example/golden.py"
            ;;
        "host_matmul_example:"*":host_matmul_example")
            execution_mode="example"
            execution_entrypoint="examples/scripts/run_example.py"
            execution_kernels="examples/a2a3/host_build_graph/matmul/kernels"
            execution_golden="examples/a2a3/host_build_graph/matmul/golden.py"
            ;;
        "host_vector_example:"*":host_vector_example")
            execution_mode="example"
            execution_entrypoint="examples/scripts/run_example.py"
            execution_kernels="examples/a2a3/host_build_graph/vector_example/kernels"
            execution_golden="examples/a2a3/host_build_graph/vector_example/golden.py"
            ;;
        *)
            echo "unsupported simpler execution spec: runner_id=${runner_id} callable_name=${callable_name} argset_kind=${argset_kind}" >&2
            exit 2
            ;;
    esac
}

run_example_execution() {
    if [ -z "$platform" ] || [ -z "$execution_entrypoint" ] || [ -z "$execution_kernels" ] || [ -z "$execution_golden" ]; then
        echo "missing simpler example execution arguments" >&2
        exit 2
    fi
    SIMPLER_EXECUTION_MODE="$execution_mode" \
    SIMPLER_EXECUTION_ENTRYPOINT="$execution_entrypoint" \
    SIMPLER_EXECUTION_KERNELS="$execution_kernels" \
    SIMPLER_EXECUTION_GOLDEN="$execution_golden" \
    exec "$python_bin" "$execution_entrypoint" \
        -k "$execution_kernels" \
        -g "$execution_golden" \
        -p "$platform"
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
        build_execution_spec
        run_example_execution
        ;;
    *)
        echo "unsupported simpler dispatch mode: $dispatch_mode" >&2
        exit 2
        ;;
esac
