#!/bin/sh
set -eu

python_bin="${SIMPLER_PYTHON:-python3}"
repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
simpler_root="${SIMPLER_PROJECT_ROOT:-$repo_root/modules/simpler}"

profile=""
platform=""
runtime_variant=""
callable_hint=""
kernels=""
golden=""
manifest=""

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
        --kernels)
            kernels="$2"
            shift 2
            ;;
        --golden)
            golden="$2"
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
            PROFILE) profile="$value" ;;
            PLATFORM) platform="$value" ;;
            RUNTIME_VARIANT) runtime_variant="$value" ;;
            CALLABLE_HINT) callable_hint="$value" ;;
            KERNELS) kernels="$value" ;;
            GOLDEN) golden="$value" ;;
            "") ;;
            *)
                echo "unknown manifest key: $key" >&2
                exit 2
                ;;
        esac
    done < "$manifest"
fi

if [ -z "$platform" ] || [ -z "$kernels" ] || [ -z "$golden" ]; then
    echo "missing required simpler dispatch arguments" >&2
    exit 2
fi

cd "$simpler_root"
SIMPLER_DISPATCH_PROFILE="$profile" \
SIMPLER_RUNTIME_VARIANT="$runtime_variant" \
SIMPLER_CALLABLE_HINT="$callable_hint" \
exec "$python_bin" examples/scripts/run_example.py \
    -k "$kernels" \
    -g "$golden" \
    -p "$platform"
