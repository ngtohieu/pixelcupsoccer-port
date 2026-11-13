# Sourced by "Pixel Cup Soccer.sh" right before the loader starts.

# Unity stays on its loading screen until the activity reports focus; the
# Skate3 loader skips nativeFocusChanged(true) unless forced.
export SK3_FOCUS=1

# Skate3-only age-gate scanner: the classes do not exist in this game.
export SK3_AGE_AUTO=0

# Holding L2+R2 swaps the D-pad and the left stick (src/padswap.c, same as the
# Soul Knight port): the pad rumbles once when it flips; hold again to swap
# back. The state is remembered in padswap.state. PADSWAP_MODE=off disables it,
# PADSWAP_SWAP=1 always starts swapped.
if [ -f "$GAMEDIR/libpadswap.so" ]; then
  export PADSWAP_FILE="$GAMEDIR/padswap.state"
  BIN_PRELOAD="$GAMEDIR/libpadswap.so${BIN_PRELOAD:+:$BIN_PRELOAD}"
fi

# Diagnostics: Unity/Android log output goes to log.txt.
export BC_LOGCAT=1

# A frame stuck this long faults the render thread: the stack lands in log.txt
# and the process exits back to the menu (Select+Start is polled by that same
# thread, so a hang otherwise needs a power cycle).
export BC_WATCHDOG=120
