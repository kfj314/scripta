// Display backend for the e-paper board. The app draws into a u8g2 canvas;
// flush() hands the frame to a background task, which diffs it against what
// is on the glass and sends only the changed rectangle as a partial refresh.
// Frames that arrive while the panel is busy are merged (latest one wins).
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "config.h"
#include "epd_ssd1677.h"
#include "gfx_backend.h"

namespace gfx {
namespace backend {

static U8G2 canvas;
static uint8_t *drawBuf = nullptr;  // what the app draws into (u8g2)
static uint8_t *pending = nullptr;  // latest flushed frame, not yet shown
static uint8_t *work = nullptr;     // frame being sent to the panel
static uint8_t *shown = nullptr;    // what is on the glass now
static SemaphoreHandle_t mtx;
static TaskHandle_t task;
static volatile bool hasPending = false, working = false, fullReq = true;
static int partials = 0;

// Ghost cleanup: after this many partial refreshes, do a full refresh once the
// user has paused for kCleanIdleMs. Force one after kPartialsMax regardless.
static const int kCleanAfter = 40;
static const int kPartialsMax = 200;
static const uint32_t kCleanIdleMs = 5000;

static u8x8_display_info_t info = {
    /* chip_enable_level = */ 0,
    /* chip_disable_level = */ 1,
    /* post_chip_enable_wait_ns = */ 0,
    /* pre_chip_disable_wait_ns = */ 0,
    /* reset_pulse_width_ms = */ 0,
    /* post_reset_wait_ms = */ 0,
    /* sda_setup_time_ns = */ 0,
    /* sck_pulse_width_ns = */ 0,
    /* sck_clock_hz = */ 0,
    /* spi_mode = */ 0,
    /* i2c_bus_clock_100kHz = */ 4,
    /* data_setup_time_ns = */ 0,
    /* write_pulse_width_ns = */ 0,
    /* tile_width = */ epd::W / 8,
    /* tile_height = */ epd::H / 8,
    /* default_x_offset = */ 0,
    /* flip_mode_x_offset = */ 0,
    /* pixel_width = */ epd::W,
    /* pixel_height = */ epd::H};

// u8g2 only needs this for its buffer geometry; the panel is driven separately.
static uint8_t canvasCb(u8x8_t *u8x8, uint8_t msg, uint8_t, void *) {
  if (msg == U8X8_MSG_DISPLAY_SETUP_MEMORY) u8x8_d_helper_display_setup_memory(u8x8, &info);
  return 1;
}

static uint8_t *alloc(bool internal) {
  uint8_t *p = (uint8_t *)heap_caps_malloc(epd::FRAME_BYTES, internal ? MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
                                                                        : MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = (uint8_t *)malloc(epd::FRAME_BYTES);
  if (p) memset(p, 0, epd::FRAME_BYTES);
  return p;
}

// Smallest rectangle (rows, byte columns) where work differs from shown.
static bool dirtyRect(int &xb0, int &xb1, int &y0, int &y1) {
  y0 = -1;
  y1 = -1;
  xb0 = epd::ROW_BYTES;
  xb1 = 0;
  for (int y = 0; y < epd::H; y++) {
    const uint8_t *a = work + y * epd::ROW_BYTES, *b = shown + y * epd::ROW_BYTES;
    if (memcmp(a, b, epd::ROW_BYTES) == 0) continue;
    if (y0 < 0) y0 = y;
    y1 = y + 1;
    int l = 0, r = epd::ROW_BYTES - 1;
    while (a[l] == b[l]) l++;
    while (a[r] == b[r]) r--;
    if (l < xb0) xb0 = l;
    if (r + 1 > xb1) xb1 = r + 1;
  }
  return y0 >= 0;
}

static void showFull(const uint8_t *frame) {
  epd::full(frame);
  if (frame != shown) memcpy(shown, frame, epd::FRAME_BYTES);
  partials = 0;
}

static void render() {
  if (fullReq || partials >= kPartialsMax) {
    fullReq = false;
    showFull(work);
    return;
  }
  int xb0, xb1, y0, y1;
  if (!dirtyRect(xb0, xb1, y0, y1)) return;
  epd::partial(work, xb0, xb1, y0, y1);
  for (int y = y0; y < y1; y++) memcpy(shown + y * epd::ROW_BYTES, work + y * epd::ROW_BYTES, epd::ROW_BYTES);
  partials++;
}

static void refreshTask(void *) {
  for (;;) {
    TickType_t wait = partials >= kCleanAfter ? pdMS_TO_TICKS(kCleanIdleMs) : portMAX_DELAY;
    if (ulTaskNotifyTake(pdTRUE, wait) == 0) {
      // Idle long enough after many partials: clean up the ghosting.
      xSemaphoreTake(mtx, portMAX_DELAY);
      bool idle = !hasPending;
      if (idle) working = true;
      xSemaphoreGive(mtx);
      if (idle) {
        showFull(shown);
        xSemaphoreTake(mtx, portMAX_DELAY);
        working = false;
        xSemaphoreGive(mtx);
      }
      continue;
    }
    for (;;) {
      xSemaphoreTake(mtx, portMAX_DELAY);
      if (!hasPending) {
        working = false;
        xSemaphoreGive(mtx);
        break;
      }
      memcpy(work, pending, epd::FRAME_BYTES);
      hasPending = false;
      working = true;
      xSemaphoreGive(mtx);
      render();
    }
  }
}

U8G2 *begin() {
  drawBuf = alloc(true);
  pending = alloc(false);
  work = alloc(false);
  shown = alloc(false);
  mtx = xSemaphoreCreateMutex();

  u8g2_t *u = canvas.getU8g2();
  u8x8_Setup(u8g2_GetU8x8(u), canvasCb, u8x8_dummy_cb, u8x8_dummy_cb, u8x8_dummy_cb);
  u8g2_SetupBuffer(u, drawBuf, epd::H / 8, u8g2_ll_hvline_horizontal_right_lsb, U8G2_R0);
  u8g2_InitDisplay(u);

  epd::begin();
  fullReq = true;  // first frame after boot or wake is a full refresh
  xTaskCreatePinnedToCore(refreshTask, "epd", 4096, nullptr, 1, &task, 0);
  return &canvas;
}

void flush() {
  xSemaphoreTake(mtx, portMAX_DELAY);
  memcpy(pending, drawBuf, epd::FRAME_BYTES);
  hasPending = true;
  xSemaphoreGive(mtx);
  xTaskNotifyGive(task);
}

void waitIdle(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    bool busy = hasPending || working;
    xSemaphoreGive(mtx);
    if (!busy) return;
    delay(10);
  }
}

void requestFullRefresh() { fullReq = true; }

void sleep() {
  waitIdle(15000);
  epd::sleep();
}

}  // namespace backend
}  // namespace gfx
