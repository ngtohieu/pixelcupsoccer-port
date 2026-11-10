# Sourced by "Pixel Cup Soccer.sh" right before the loader starts.

# Unity stays on its loading screen until the activity reports focus; the
# Skate3 loader skips nativeFocusChanged(true) unless forced.
export SK3_FOCUS=1

# Skate3-only age-gate scanner: the classes do not exist in this game.
export SK3_AGE_AUTO=0

# Diagnostics: Unity/Android log output goes to log.txt.
export BC_LOGCAT=1

# A frame stuck this long faults the render thread: the stack lands in log.txt
# and the process exits back to the menu (Select+Start is polled by that same
# thread, so a hang otherwise needs a power cycle).
export BC_WATCHDOG=120

# Memory guard. The match scene needs more than the ~900 MB these handhelds
# have; muOS ships swap/zram so it survives, dArkOS ships none and the kernel
# OOM killer SIGKILLs the game (status 137) on entering a match. When the host
# has (almost) no swap, add zram, else a swapfile on an ext4 root, for this run.
nxmem_root() {
  if [ "$(id -u)" = 0 ]; then "$@"
  elif command -v sudo >/dev/null 2>&1; then sudo -n "$@"
  else return 1
  fi
}
nxmem_kb() { awk -v k="$1:" '$1 == k { print $2; exit }' /proc/meminfo 2>/dev/null; }
NXMEM_TOTAL=$(nxmem_kb MemTotal); NXMEM_SWAP=$(nxmem_kb SwapTotal)
NXMEM_ZRAM=""; NXMEM_SWAPFILE=""
echo "[mem] MemTotal=${NXMEM_TOTAL:-?}kB MemAvailable=$(nxmem_kb MemAvailable)kB SwapTotal=${NXMEM_SWAP:-?}kB"
if [ "${NXMEM_TOTAL:-0}" -gt 0 ] && [ "$NXMEM_TOTAL" -lt 1572864 ] && [ "${NXMEM_SWAP:-0}" -lt 262144 ]; then
  # zram first: no SD writes, and Unity's heap compresses well.
  if [ -e /sys/class/zram-control/hot_add ]; then
    NXMEM_ZDEV=$(nxmem_root cat /sys/class/zram-control/hot_add 2>/dev/null)
  elif [ ! -e /sys/block/zram0 ]; then
    nxmem_root modprobe zram num_devices=1 >/dev/null 2>&1 && NXMEM_ZDEV=0
  fi
  if [ -n "$NXMEM_ZDEV" ] && [ -e "/sys/block/zram$NXMEM_ZDEV/disksize" ] && \
     ! grep -q "^/dev/zram$NXMEM_ZDEV " /proc/swaps 2>/dev/null; then
    for a in lz4 lzo; do
      echo "$a" | nxmem_root tee "/sys/block/zram$NXMEM_ZDEV/comp_algorithm" >/dev/null 2>&1 && break
    done
    if echo $((NXMEM_TOTAL / 2))K | nxmem_root tee "/sys/block/zram$NXMEM_ZDEV/disksize" >/dev/null 2>&1 && \
       nxmem_root mkswap "/dev/zram$NXMEM_ZDEV" >/dev/null 2>&1 && \
       nxmem_root swapon -p 100 "/dev/zram$NXMEM_ZDEV" 2>/dev/null; then
      NXMEM_ZRAM=/dev/zram$NXMEM_ZDEV
      echo "[mem] zram swap on $NXMEM_ZRAM ($((NXMEM_TOTAL / 2048)) MB)"
    fi
  fi
  # Swapfile fallback (or addition when zram is small): kept between runs so
  # it is written once. Must live on ext4 (the ROM partition is often exFAT).
  NXMEM_FILE=/swapfile.pixelcupsoccer
  if [ -z "$NXMEM_ZRAM" ] || [ "$NXMEM_TOTAL" -lt 1048576 ]; then
    if [ ! -f "$NXMEM_FILE" ]; then
      NXMEM_FREE=$(df -Pk / 2>/dev/null | awk 'NR == 2 { print $4 }')
      if [ "${NXMEM_FREE:-0}" -gt 1572864 ]; then
        echo "[mem] creating $NXMEM_FILE (512 MB, one time)"
        nxmem_root dd if=/dev/zero of="$NXMEM_FILE" bs=1M count=512 2>/dev/null && \
          nxmem_root chmod 600 "$NXMEM_FILE" && nxmem_root mkswap "$NXMEM_FILE" >/dev/null 2>&1 || \
          nxmem_root rm -f "$NXMEM_FILE"
      else
        echo "[mem] not enough free space on / for a swapfile (${NXMEM_FREE:-?} kB)"
      fi
    fi
    if [ -f "$NXMEM_FILE" ] && nxmem_root swapon -p 10 "$NXMEM_FILE" 2>/dev/null; then
      NXMEM_SWAPFILE=$NXMEM_FILE
      echo "[mem] swapfile on $NXMEM_FILE"
    fi
  fi
  [ -n "$NXMEM_ZRAM$NXMEM_SWAPFILE" ] || echo "[mem] WARNING: could not add swap; the match may be OOM-killed"
  echo "[mem] SwapTotal now $(nxmem_kb SwapTotal)kB"
fi
sync; echo 3 | nxmem_root tee /proc/sys/vm/drop_caches >/dev/null 2>&1

# On the way out: record any OOM kill in log.txt and undo this run's swap,
# then chain to the firmware's own pm_finish.
if declare -F pm_finish >/dev/null 2>&1; then
  eval "nxmem_orig_pm_finish() $(declare -f pm_finish | tail -n +2)"
fi
pm_finish() {
  nxmem_root dmesg 2>/dev/null | grep -iE 'out of memory|oom-kill|killed process' | tail -n 5 | sed 's/^/[mem] dmesg: /'
  [ -n "$NXMEM_SWAPFILE" ] && nxmem_root swapoff "$NXMEM_SWAPFILE" 2>/dev/null
  if [ -n "$NXMEM_ZRAM" ]; then
    nxmem_root swapoff "$NXMEM_ZRAM" 2>/dev/null
    echo 1 | nxmem_root tee "/sys/block/${NXMEM_ZRAM#/dev/}/reset" >/dev/null 2>&1
  fi
  if declare -F nxmem_orig_pm_finish >/dev/null 2>&1; then nxmem_orig_pm_finish; fi
}
