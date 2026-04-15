#!/bin/sh
set -eu

python_bin="${SIMPLER_PYTHON:-python3}"
repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
simpler_root="${SIMPLER_PROJECT_ROOT:-$repo_root/modules/simpler}"

dispatch_mode="${SIMPLER_DISPATCH_MODE:-}"
callable_name="${SIMPLER_DISPATCH_CALLABLE_NAME:-}"
argset_kind="${SIMPLER_DISPATCH_ARGSET_KIND:-}"
entrypoint="${SIMPLER_DISPATCH_ENTRYPOINT:-}"
platform="${SIMPLER_DISPATCH_PLATFORM:-}"
kernels="${SIMPLER_DISPATCH_KERNELS:-}"
golden="${SIMPLER_DISPATCH_GOLDEN:-}"

if [ -z "$dispatch_mode" ] || [ -z "$entrypoint" ]; then
    echo "missing simpler dispatch executor configuration" >&2
    exit 2
fi

cd "$simpler_root"

case "$dispatch_mode" in
    example)
        if [ -z "$platform" ] || [ -z "$kernels" ] || [ -z "$golden" ]; then
            echo "missing simpler example execution arguments" >&2
            exit 2
        fi
        if [ -z "$callable_name" ] || [ -z "$argset_kind" ]; then
            echo "missing simpler callable resolution for example execution" >&2
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
