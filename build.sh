#!/usr/bin/env bash
#
# xaga (Redmi Note 11T Pro / POCO X4 GT / Redmi K50i) 6.12 kernel modules build.
#
# Builds ONLY kernel modules from a clean tree:
#   1. kernel config (gki_defconfig + mgk_64_k612_defconfig + xaga.config)
#   2. in-tree modules (refreshes Module.symvers)
#   3. out-of-tree modules tree -> 133 x .ko (make M=)
#
# The OPPO 6.12 kernel source (K) is located automatically:
#   - $K env var if set and exists
#   - local workspace paths
#   - a sibling dir named android_kernel_oddo_mt6895 (GitHub remote name)
#   - otherwise prompts for input
#
# Overridable via env: K M OUT PEM LLVM_PREFIX JOBS
# Usage: ./build.sh [--no-clean]
#
set -euo pipefail

# ---------------------------------------------------------------------------
# Paths (all overridable; defaults relative to this script's directory)
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
M="${M:-$SCRIPT_DIR/kernel_device_modules-6.12}"
OUT="${OUT:-$SCRIPT_DIR/out}"
PEM="${PEM:-$M/certs/mtk_signing_key.pem}"
LLVM_PREFIX="${LLVM_PREFIX:-/usr/lib/llvm-18}"
JOBS="${JOBS:-$(nproc)}"
SKIP_CLEAN=0

for arg in "$@"; do
    case "$arg" in
        --no-clean) SKIP_CLEAN=1 ;;
        *) echo "unknown arg: $arg" >&2; exit 2 ;;
    esac
done

WORK="$(mktemp -d /tmp/xaga_build.XXXXXX)"
log() { echo -e "\n\033[1;32m=== $* ===\033[0m"; }

# ---------------------------------------------------------------------------
# Locate the OPPO 6.12 kernel source tree (K)
# ---------------------------------------------------------------------------
locate_kernel() {
    local candidates=()
    # explicit env
    [ -n "${K:-}" ] && candidates+=("$K")
    # sibling / parent dirs of this script (workspace + GitHub clone layouts)
    candidates+=(
        "$SCRIPT_DIR/android_kernel_oppo_mt6896"
        "$SCRIPT_DIR/../android_kernel_oppo_mt6896"
        "$SCRIPT_DIR/../oddo6_12/android_kernel_oppo_mt6896"
        "$SCRIPT_DIR/../../oddo6_12/android_kernel_oppo_mt6896"
        "$SCRIPT_DIR/android_kernel_oddo_mt6895"          # GitHub remote name
        "$SCRIPT_DIR/../android_kernel_oddo_mt6895"
        "$SCRIPT_DIR/../../android_kernel_oddo_mt6895"
        "$SCRIPT_DIR/xaga/android_kernel_oddo_mt6895"
        "$SCRIPT_DIR/../xaga/android_kernel_oddo_mt6895"
        "$SCRIPT_DIR/../../xaga/android_kernel_oddo_mt6895"
    )
    local c
    for c in "${candidates[@]}"; do
        if [ -f "$c/arch/arm64/configs/gki_defconfig" ]; then
            echo "$c"
            return 0
        fi
    done
    return 1
}

if K="$(locate_kernel)"; then
    log "kernel source: $K"
else
    echo "OPPO 6.12 kernel source not found automatically." >&2
    echo "Set K=<path> or enter it now (expects arch/arm64/configs/gki_defconfig):" >&2
    read -r -p "K=" K
    if [ -z "${K:-}" ] || [ ! -f "$K/arch/arm64/configs/gki_defconfig" ]; then
        echo "ERROR: invalid kernel path: $K" >&2
        exit 1
    fi
fi

# 27 include dirs for out-of-tree module builds (verbatim from 2026-08-08)
INC="-I$M/include -I$M/include/soc/mediatek -I$M/drivers/clk/mediatek \
-I$M/drivers/gpu/mediatek/gpufreq -I$M/drivers/misc/mediatek/typec/tcpc/inc \
-I$M/drivers/misc/mediatek/mtk-interconnect -I$M/drivers/misc/mediatek/include \
-I$M/drivers/misc/mediatek/include/mt-plat -I$M/drivers/misc/mediatek/mminfra \
-I$M/drivers/misc/mediatek/power_throttling -I$M/drivers/misc/mediatek/dcm \
-I$M/drivers/misc/mediatek/dcm/include -I$M/drivers/misc/mediatek/mdpm \
-I$M/drivers/misc/mediatek/aee/mrdump -I$M/drivers/iommu/arm/arm-smmu-v3 \
-I$M/drivers/ufs -I$M/drivers/iommu -I$M/drivers/memory -I$M/drivers/usb/mtu3 \
-I$K/drivers/devfreq -I$K/drivers/mmc/host -I$K/drivers/tty/serial/8250 \
-I$K/drivers/dma -I$K/drivers/ufs/host -I$K/drivers/ufs/core \
-I$K/drivers/usb/typec -I$K/kernel"

# ---------------------------------------------------------------------------
# Preflight
# ---------------------------------------------------------------------------
for p in "$K" "$M" "$PEM"; do
    [ -e "$p" ] || { echo "missing required path: $p" >&2; exit 1; }
done
export PATH="$LLVM_PREFIX/bin:$PATH"
export KCONFIG_EXT_PREFIX="$M/"   # REQUIRED for every make (module symbols)

# ---------------------------------------------------------------------------
# 1. Clean
# ---------------------------------------------------------------------------
clean() {
    log "1/3 clean $OUT + module tree artifacts"
    rm -rf "$OUT" && mkdir -p "$OUT"
    find "$M" \( -name '*.o' -o -name '*.ko' -o -name '*.cmd' -o -name '*.mod*' \
        -o -name 'modules.order' -o -name 'Module.symvers' -o -name '.tmp_versions' \) \
        -exec rm -rf {} + 2>/dev/null || true
}

# ---------------------------------------------------------------------------
# 2. Kernel config (gki + mgk + xaga fragment)
# ---------------------------------------------------------------------------
config() {
    log "2/3 kernel config (gki_defconfig + mgk_64_k612 + xaga.config)"
    ( cd "$K" && make O="$OUT" ARCH=arm64 LLVM=1 gki_defconfig > "$WORK/config1.log" 2>&1 )
    ( cd "$K" && KCONFIG_CONFIG="$OUT/.config" ./scripts/kconfig/merge_config.sh -m \
        "$OUT/.config" \
        "$M/arch/arm64/configs/mgk_64_k612_defconfig" \
        "$M/arch/arm64/configs/vendor/xaga.config" > "$WORK/merge.log" 2>&1 )
    ( cd "$K" && make O="$OUT" ARCH=arm64 LLVM=1 olddefconfig > "$WORK/config2.log" 2>&1 )
    sed -i "s|^CONFIG_MODULE_SIG_KEY=.*|CONFIG_MODULE_SIG_KEY=\"$PEM\"|" "$OUT/.config"
    grep -q '^CONFIG_XAGA_OOPS_LOG=m' "$OUT/.config" \
        || { echo "ERROR: CONFIG_XAGA_OOPS_LOG missing after merge" >&2; exit 1; }
}

# ---------------------------------------------------------------------------
# 3. Modules: in-tree (Module.symvers) + out-of-tree 133 x .ko
# ---------------------------------------------------------------------------
modules() {
    log "3/3 in-tree modules (Module.symvers)"
    ( cd "$K" && make O="$OUT" ARCH=arm64 LLVM=1 -j"$JOBS" modules > "$WORK/inmod.log" 2>&1 )

    log "3b/3 out-of-tree modules -> 133 x .ko (KBUILD_MODPOST_WARN=1)"
    make -C "$K" O="$OUT" ARCH=arm64 LLVM=1 KCONFIG_EXT_PREFIX="$M/" M="$M" \
        DEVICE_MODULES_PATH="$M" DEVCIE_MODULES_INCLUDE="$INC" \
        KBUILD_MODPOST_WARN=1 -j"$JOBS" modules > "$WORK/ootmod.log" 2>&1
    NKO="$(find "$M" -name '*.ko' | wc -l)"
    [ "$NKO" -eq 133 ] || { echo "ERROR: expected 133 .ko, got $NKO" >&2; exit 1; }
    echo "$NKO .ko built"
}

# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------
[ "$SKIP_CLEAN" -eq 1 ] || clean
config
modules

log "DONE. artifacts:"
echo "out-of-tree modules: $(find "$M" -name '*.ko' | wc -l) x .ko in $M"
echo "Module.symvers: $(ls -la "$OUT/Module.symvers" 2>/dev/null | awk '{print $5, $9}')"
echo "build log: $WORK (kept for inspection)"
