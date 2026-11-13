/* padswap — LD_PRELOAD shim: holding L2+R2 swaps the D-pad and the left stick.
 *
 * Same behaviour as the Soul Knight port's shim (sk_jni_alias.c): hold L2+R2
 * together for 200 ms (900 ms when they went down more than 350 ms apart),
 * the pad rumbles once and the swap flips. It stays until the combo is held
 * again and is remembered between launches in PADSWAP_FILE.
 *
 * The loader polls the pad once per frame: SDL_GameControllerUpdate, then
 * SDL_GameControllerGetButton / SDL_GameControllerGetAxis (SDL_Joystick* when
 * the pad has no controller mapping). The combo is advanced in Update and in
 * the trigger reads; the swap is applied in the getters.
 *
 * PADSWAP_MODE=off: the shim passes everything through.
 * PADSWAP_SWAP=1 / 0: start swapped / normal, ignoring the remembered state.
 *
 * Build: zig cc -target aarch64-linux-gnu.2.17 -shared -fPIC -O2 -s -o libpadswap.so padswap.c
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct SDL_GameController SDL_GameController;
typedef struct SDL_Joystick SDL_Joystick;

enum { AXIS_LEFTX = 0, AXIS_LEFTY = 1, AXIS_TRIGGERLEFT = 4, AXIS_TRIGGERRIGHT = 5 };
enum { BTN_DPAD_UP = 11, BTN_DPAD_DOWN = 12, BTN_DPAD_LEFT = 13, BTN_DPAD_RIGHT = 14 };
enum { HAT_UP = 1, HAT_RIGHT = 2, HAT_DOWN = 4, HAT_LEFT = 8 };

#define TRIGGER_ON 16000 /* triggers report 0..32767 */
#define STICK_ON 14000   /* stick-as-dpad threshold */

static void (*real_update)(void);
static uint8_t (*real_button)(SDL_GameController *, int);
static int16_t (*real_axis)(SDL_GameController *, int);
static SDL_GameController *(*real_open)(int);
static void (*real_close)(SDL_GameController *);
static int (*real_rumble)(SDL_GameController *, uint16_t, uint16_t, uint32_t);
static void (*real_jupdate)(void);
static SDL_Joystick *(*real_jopen)(int);
static void (*real_jclose)(SDL_Joystick *);
static int16_t (*real_jaxis)(SDL_Joystick *, int);
static uint8_t (*real_jhat)(SDL_Joystick *, int);
static int (*real_jnumaxes)(SDL_Joystick *);
static int (*real_jrumble)(SDL_Joystick *, uint16_t, uint16_t, uint32_t);

static int ready;
static int off;
static int swapped;
static const char *state_file;
static SDL_GameController *pad;
static SDL_Joystick *joy;

static int combo_holding, combo_fired;
static int prev_l2, prev_r2;
static int64_t l2_ms, r2_ms, both_ms;

static int64_t now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void init(void)
{
  if (ready) return;
  ready = 1;
  real_update = dlsym(RTLD_NEXT, "SDL_GameControllerUpdate");
  real_button = dlsym(RTLD_NEXT, "SDL_GameControllerGetButton");
  real_axis = dlsym(RTLD_NEXT, "SDL_GameControllerGetAxis");
  real_open = dlsym(RTLD_NEXT, "SDL_GameControllerOpen");
  real_close = dlsym(RTLD_NEXT, "SDL_GameControllerClose");
  real_rumble = dlsym(RTLD_NEXT, "SDL_GameControllerRumble");
  real_jupdate = dlsym(RTLD_NEXT, "SDL_JoystickUpdate");
  real_jopen = dlsym(RTLD_NEXT, "SDL_JoystickOpen");
  real_jclose = dlsym(RTLD_NEXT, "SDL_JoystickClose");
  real_jaxis = dlsym(RTLD_NEXT, "SDL_JoystickGetAxis");
  real_jhat = dlsym(RTLD_NEXT, "SDL_JoystickGetHat");
  real_jnumaxes = dlsym(RTLD_NEXT, "SDL_JoystickNumAxes");
  real_jrumble = dlsym(RTLD_NEXT, "SDL_JoystickRumble");

  const char *m = getenv("PADSWAP_MODE");
  off = m && !strcmp(m, "off");
  state_file = getenv("PADSWAP_FILE");
  if (!state_file || !*state_file) state_file = "padswap.state";
  const char *s = getenv("PADSWAP_SWAP");
  if (s && *s) {
    swapped = *s == '1';
  } else {
    FILE *f = fopen(state_file, "r");
    if (f) {
      swapped = fgetc(f) == '1';
      fclose(f);
    }
  }
  if (off) swapped = 0;
  fprintf(stderr, "[padswap] hold L2+R2 to swap dpad/left stick (%s, swap %s)\n",
          off ? "off" : "on", swapped ? "ON" : "OFF");
}

static void save_state(void)
{
  FILE *f = fopen(state_file, "w");
  if (!f) return;
  fputc(swapped ? '1' : '0', f);
  fclose(f);
}

/* One pulse on every flip; stronger when the swap turns on. */
static void rumble(void)
{
  uint16_t power = swapped ? 0xC000 : 0x6000;
  uint32_t ms = swapped ? 180 : 100;
  int r = -1;
  if (pad && real_rumble) r = real_rumble(pad, power, power, ms);
  else if (joy && real_jrumble) r = real_jrumble(joy, power, power, ms);
  if (r != 0) fprintf(stderr, "[padswap] rumble not available\n");
}

static void update_combo(int l2, int r2)
{
  int64_t now = now_ms();
  if (l2 && !prev_l2) l2_ms = now;
  if (r2 && !prev_r2) r2_ms = now;
  prev_l2 = l2;
  prev_r2 = r2;
  if (!l2 || !r2) {
    combo_holding = 0;
    combo_fired = 0;
    return;
  }
  if (!combo_holding) {
    combo_holding = 1;
    both_ms = now;
  }
  int64_t held = now - both_ms;
  int64_t apart = l2_ms > r2_ms ? l2_ms - r2_ms : r2_ms - l2_ms;
  if (!combo_fired && ((apart <= 350 && held >= 200) || held >= 900)) {
    combo_fired = 1;
    swapped = !swapped;
    save_state();
    rumble();
    fprintf(stderr, "[padswap] swap %s\n", swapped ? "ON" : "OFF");
  }
}

/* The triggers are hidden from the game while the combo is being held. */
static int combo_eats_triggers(void)
{
  if (!combo_holding) return 0;
  int64_t apart = l2_ms > r2_ms ? l2_ms - r2_ms : r2_ms - l2_ms;
  return combo_fired || (apart <= 350 && now_ms() - both_ms >= 50);
}

SDL_GameController *SDL_GameControllerOpen(int index)
{
  init();
  SDL_GameController *c = real_open(index);
  if (c) pad = c;
  return c;
}

void SDL_GameControllerClose(SDL_GameController *c)
{
  init();
  if (pad == c) pad = NULL;
  real_close(c);
}

void SDL_GameControllerUpdate(void)
{
  init();
  real_update();
  if (off || !pad) return;
  update_combo(real_axis(pad, AXIS_TRIGGERLEFT) > TRIGGER_ON,
               real_axis(pad, AXIS_TRIGGERRIGHT) > TRIGGER_ON);
}

uint8_t SDL_GameControllerGetButton(SDL_GameController *c, int button)
{
  init();
  if (c && !pad) pad = c;
  if (!swapped || button < BTN_DPAD_UP || button > BTN_DPAD_RIGHT)
    return real_button(c, button);
  /* D-pad reads the left stick. */
  switch (button) {
  case BTN_DPAD_UP: return real_axis(c, AXIS_LEFTY) < -STICK_ON;
  case BTN_DPAD_DOWN: return real_axis(c, AXIS_LEFTY) > STICK_ON;
  case BTN_DPAD_LEFT: return real_axis(c, AXIS_LEFTX) < -STICK_ON;
  default: return real_axis(c, AXIS_LEFTX) > STICK_ON;
  }
}

int16_t SDL_GameControllerGetAxis(SDL_GameController *c, int axis)
{
  init();
  if (c && !pad) pad = c;
  if (off) return real_axis(c, axis);
  if (axis == AXIS_TRIGGERLEFT || axis == AXIS_TRIGGERRIGHT) {
    update_combo(real_axis(c, AXIS_TRIGGERLEFT) > TRIGGER_ON,
                 real_axis(c, AXIS_TRIGGERRIGHT) > TRIGGER_ON);
    return combo_eats_triggers() ? 0 : real_axis(c, axis);
  }
  if (!swapped || (axis != AXIS_LEFTX && axis != AXIS_LEFTY))
    return real_axis(c, axis);
  /* Left stick reads the D-pad. */
  int neg = real_button(c, axis == AXIS_LEFTX ? BTN_DPAD_LEFT : BTN_DPAD_UP);
  int pos = real_button(c, axis == AXIS_LEFTX ? BTN_DPAD_RIGHT : BTN_DPAD_DOWN);
  return (int16_t)((pos ? 32767 : 0) - (neg ? 32767 : 0));
}

/* Raw joystick path, used by the loader when the pad has no controller
 * mapping: axes 0/1 are the left stick, 4/5 the triggers, hat 0 the D-pad. */

SDL_Joystick *SDL_JoystickOpen(int index)
{
  init();
  SDL_Joystick *j = real_jopen(index);
  if (j) joy = j;
  return j;
}

void SDL_JoystickClose(SDL_Joystick *j)
{
  init();
  if (joy == j) joy = NULL;
  real_jclose(j);
}

void SDL_JoystickUpdate(void)
{
  init();
  real_jupdate();
  if (off || pad || !joy) return;
  int na = real_jnumaxes(joy);
  update_combo(na > 4 && real_jaxis(joy, 4) > TRIGGER_ON,
               na > 5 && real_jaxis(joy, 5) > TRIGGER_ON);
}

int16_t SDL_JoystickGetAxis(SDL_Joystick *j, int axis)
{
  init();
  if (pad || !swapped || (axis != 0 && axis != 1))
    return real_jaxis(j, axis);
  uint8_t hat = real_jhat(j, 0);
  int neg = hat & (axis == 0 ? HAT_LEFT : HAT_UP);
  int pos = hat & (axis == 0 ? HAT_RIGHT : HAT_DOWN);
  return (int16_t)((pos ? 32767 : 0) - (neg ? 32767 : 0));
}

uint8_t SDL_JoystickGetHat(SDL_Joystick *j, int hat)
{
  init();
  if (pad || !swapped || hat != 0)
    return real_jhat(j, hat);
  int16_t lx = real_jaxis(j, 0), ly = real_jaxis(j, 1);
  uint8_t res = 0;
  if (ly < -STICK_ON) res |= HAT_UP;
  if (ly > STICK_ON) res |= HAT_DOWN;
  if (lx < -STICK_ON) res |= HAT_LEFT;
  if (lx > STICK_ON) res |= HAT_RIGHT;
  return res;
}

__attribute__((constructor)) static void padswap_init(void)
{
  init();
}
