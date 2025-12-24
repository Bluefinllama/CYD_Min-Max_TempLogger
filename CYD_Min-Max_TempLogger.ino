#include <lvgl.h> //Version 9.4.0
#include <TFT_eSPI.h> //Version 2.5.43
#include <OneWire.h> //Version 2.3.5
#include <DallasTemperature.h> //Version 4.0.5
#include <SD.h>
#include <XPT2046_Bitbang.h> //XPT2046_Bitbang_Slim Version 2.0.1
#include <time.h>
#include <sys/time.h>
#include <algorithm>
#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <ESPmDNS.h>

/* Hardware definitions */
#define SCREEN_WIDTH   320
#define SCREEN_HEIGHT  240

#define ONE_WIRE_BUS   27
#define SD_CS          5

#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_SCK  25
#define TOUCH_CS   33

#define LED_RED   4
#define LED_GREEN 16
#define LED_BLUE  17

#define LDR_PIN 34
#define BACKLIGHT_PIN 21

/* Globals */
TFT_eSPI tft;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

XPT2046_Bitbang ts(TOUCH_MOSI, TOUCH_MISO, TOUCH_SCK, TOUCH_CS);

static lv_color_t buf1[SCREEN_WIDTH * 20];
static lv_color_t buf2[SCREEN_WIDTH * 20];
static lv_disp_draw_buf_t draw_buf;

/* UI elements */
lv_obj_t *main_scr;
lv_obj_t *label_temp;
lv_obj_t *label_min;
lv_obj_t *label_max;
lv_obj_t *label_status;
lv_obj_t *label_time;
lv_obj_t *label_date;

lv_obj_t *history_scr;
lv_obj_t *history_container;

lv_obj_t *settings_scr;
lv_obj_t *slider_brightness;
lv_obj_t *switch_auto_brightness;
lv_obj_t *btn_reset;

/* State */
float session_min = 100.0;
float session_max = -100.0;
float today_min = 100.0;
float today_max = -100.0;

struct tm last_tm = {0};
bool time_ok = false;
bool sd_ok = false;
bool auto_brightness = true;
int brightness = 255;

/* WiFi and WebServer */
WebServer server(80);
const char* ssid = "TempLoggerAP";
const char* password = "tempLoggerPassword2025";

struct DayMinMax { float min = 100.0; float max = -100.0; };

/* Display flush */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;

  tft.startWrite();
  tft.pushImage(area->x1, area->y1, w, h, (uint16_t *)color_p);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

/* Touch input */
void touch_read(lv_indev_drv_t *, lv_indev_data_t *data)
{
  TouchPoint p = ts.getTouch();

  if (p.zRaw > 300) {
    uint16_t touch_x = map(p.xRaw, 200, 3800, 0, SCREEN_WIDTH - 1);
    uint16_t touch_y = map(p.yRaw, 200, 3800, 0, SCREEN_HEIGHT - 1);

    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = touch_x;
    data->point.y = touch_y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

/* Dark theme */
void apply_dark_theme(lv_obj_t *scr)
{
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
}

/* Gesture handler */
static void gesture_cb(lv_event_t * e)
{
  lv_obj_t * scr = lv_event_get_target(e);
  lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

  if (dir == LV_DIR_RIGHT) {
    if (scr == main_scr) lv_scr_load(history_scr);
    else if (scr == history_scr) lv_scr_load(settings_scr);
    else if (scr == settings_scr) lv_scr_load(main_scr);
  } else if (dir == LV_DIR_LEFT) {
    if (scr == main_scr) lv_scr_load(settings_scr);
    else if (scr == settings_scr) lv_scr_load(history_scr);
    else if (scr == history_scr) lv_scr_load(main_scr);
  }
}

/* Event callbacks */
static void reset_session_cb(lv_event_t * e)
{
  session_min = 100.0;
  session_max = -100.0;
  save_session();
  update_main_min_max_labels();
}

static void slider_brightness_cb(lv_event_t * e)
{
  brightness = lv_slider_get_value(slider_brightness);
  ledcWrite(0, brightness);
}

static void switch_auto_brightness_cb(lv_event_t * e)
{
  auto_brightness = lv_obj_has_state(switch_auto_brightness, LV_STATE_CHECKED);
}

static void history_loaded_cb(lv_event_t * e)
{
  update_history();
}

/* Update Min/Max labels */
void update_main_min_max_labels()
{
  if (session_min <= session_max) {
    char buf_min[32], buf_max[32];
    sprintf(buf_min, "Min\n%.1f °C", session_min);
    sprintf(buf_max, "Max\n%.1f °C", session_max);
    lv_label_set_text(label_min, buf_min);
    lv_label_set_text(label_max, buf_max);
  } else {
    lv_label_set_text(label_min, "Min\nNo data");
    lv_label_set_text(label_max, "Max\nNo data");
  }
}

/* Session persist */
void save_session()
{
  if (sd_ok) {
    File f = SD.open("/session.txt", FILE_WRITE);
    if (f) {
      f.printf("%.2f %.2f\n", session_min, session_max);
      f.close();
    }
  }
}

void load_session()
{
  if (sd_ok) {
    File f = SD.open("/session.txt");
    if (f) {
      char buf[32];
      size_t len = f.readBytesUntil('\n', buf, sizeof(buf) - 1);
      buf[len] = '\0';
      sscanf(buf, "%f %f", &session_min, &session_max);
      f.close();
    }
  }
}

/* Monthly CSV */
String get_monthly_csv(struct tm *tmn)
{
  char month_str[8];
  strftime(month_str, sizeof(month_str), "%Y-%m", tmn);
  return "/temp-" + String(month_str) + ".csv";
}

/* List CSVs */
void list_monthly_csvs(std::vector<String> &files)
{
  files.clear();
  File root = SD.open("/");
  if (root) {
    File entry = root.openNextFile();
    while (entry) {
      if (!entry.isDirectory()) {
        String name = entry.name();
        if (name.startsWith("temp-") && name.endsWith(".csv")) {
          files.push_back("/" + name);
        }
      }
      entry = root.openNextFile();
    }
    root.close();
  }
  std::sort(files.begin(), files.end());
}

/* SD full handler */
bool handle_sd_full()
{
  uint64_t total = SD.cardSize();
  uint64_t used = SD.usedBytes();
  uint64_t free = total - used;
  if (free > 1024 * 1024) return true;

  std::vector<String> files;
  list_monthly_csvs(files);
  if (files.empty()) return false;

  String oldest = files[0];
  if (SD.remove(oldest)) {
    Serial.printf("Deleted oldest CSV: %s\n", oldest.c_str());
    return true;
  }
  return false;
}

/* History update - clean container, create labels dynamically */
void update_history()
{
  lv_obj_clean(history_container);  // Clear old labels to free memory

  if (!sd_ok) {
    lv_obj_t *label = lv_label_create(history_container);
    lv_label_set_text(label, "No SD card");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_scrollbar_mode(history_container, LV_SCROLLBAR_MODE_OFF);  // No scroll to fix stretch
    return;
  }
  if (!time_ok) {
    lv_obj_t *label = lv_label_create(history_container);
    lv_label_set_text(label, "No time set");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_scrollbar_mode(history_container, LV_SCROLLBAR_MODE_OFF);  // No scroll to fix stretch
    return;
  }

  lv_obj_set_scrollbar_mode(history_container, LV_SCROLLBAR_MODE_AUTO);  // Enable scroll for data

  time_t now = time(nullptr);
  struct tm curr = *localtime(&now);
  curr.tm_hour = 0;
  curr.tm_min = 0;
  curr.tm_sec = 0;
  time_t curr_midnight = mktime(&curr);

  DayMinMax days[8];
  for (int i = 0; i < 8; i++) { days[i].min = 100.0; days[i].max = -100.0; }

  std::vector<String> files;
  list_monthly_csvs(files);

  for (const String& fname : files) {
    File f = SD.open(fname.c_str());
    if (!f) continue;
    char line[64];
    while (f.available()) {
      size_t len = f.readBytesUntil('\n', line, sizeof(line) - 1);
      line[len] = '\0';
      int y, mo, d, h, mi, s; float t;
      if (sscanf(line, "%d-%d-%d %d:%d:%d,%f", &y, &mo, &d, &h, &mi, &s, &t) == 7) {
        struct tm log_tm = {s, mi, h, d, mo - 1, y - 1900};
        log_tm.tm_isdst = -1;  // Let mktime handle DST
        time_t log_midnight = mktime(&log_tm) - (log_tm.tm_hour * 3600 + log_tm.tm_min * 60 + log_tm.tm_sec);  // Midnight of log day
        double diff = difftime(curr_midnight, log_midnight) / 86400.0;
        if (diff >= 0 && diff < 8) {
          int idx = (int)diff;
          days[idx].min = std::min(days[idx].min, t);
          days[idx].max = std::max(days[idx].max, t);
        }
      }
    }
    f.close();
  }

  // Apply in-memory today
  if (today_min <= today_max) {
    days[0].min = std::min(days[0].min, today_min);
    days[0].max = std::max(days[0].max, today_max);
  }

  for (int i = 0; i < 8; i++) {
    struct tm day_tm = curr;
    day_tm.tm_mday -= i;
    day_tm.tm_isdst = -1;
    mktime(&day_tm);
    char date_str[30];
    if (i == 0) {
      strcpy(date_str, "Today");
    } else {
      strftime(date_str, sizeof(date_str), "%A, %Y-%m-%d", &day_tm);
    }

    lv_obj_t *label_date = lv_label_create(history_container);
    lv_label_set_text(label_date, date_str);
    lv_obj_set_style_text_color(label_date, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_date, &lv_font_montserrat_20, 0);

    if (days[i].min <= days[i].max) {
      char buf[64];
      sprintf(buf, "Min: %.1f °C", days[i].min);
      lv_obj_t *label_min_day = lv_label_create(history_container);
      lv_label_set_text(label_min_day, buf);
      lv_obj_set_style_text_color(label_min_day, lv_color_hex(0x00BFFF), 0);
      lv_obj_set_style_text_font(label_min_day, &lv_font_montserrat_18, 0);

      sprintf(buf, "Max: %.1f °C", days[i].max);
      lv_obj_t *label_max_day = lv_label_create(history_container);
      lv_label_set_text(label_max_day, buf);
      lv_obj_set_style_text_color(label_max_day, lv_color_hex(0xFF4444), 0);
      lv_obj_set_style_text_font(label_max_day, &lv_font_montserrat_18, 0);
    } else {
      lv_obj_t *label_no_data = lv_label_create(history_container);
      lv_label_set_text(label_no_data, "No data");
      lv_obj_set_style_text_color(label_no_data, lv_color_white(), 0);
      lv_obj_set_style_text_font(label_no_data, &lv_font_montserrat_18, 0);
    }

    lv_obj_t *spacer = lv_label_create(history_container);
    lv_label_set_text(spacer, "");
    lv_obj_set_height(spacer, 10);  // Empty line
  }
}

/* UI Creation */
void create_main_ui()
{
  main_scr = lv_obj_create(NULL);
  apply_dark_theme(main_scr);
  lv_obj_add_event_cb(main_scr, gesture_cb, LV_EVENT_GESTURE, NULL);

  label_temp = lv_label_create(main_scr);
  lv_obj_align(label_temp, LV_ALIGN_TOP_MID, 0, 20);
  lv_obj_set_style_text_font(label_temp, &lv_font_montserrat_48, 0);
  lv_label_set_text(label_temp, "--.- °C");
  lv_obj_set_style_text_color(label_temp, lv_color_hex(0xFFA500), 0);

  label_min = lv_label_create(main_scr);
  lv_obj_align(label_min, LV_ALIGN_LEFT_MID, 30, 10);
  lv_obj_set_style_text_font(label_min, &lv_font_montserrat_24, 0);
  lv_label_set_text(label_min, "Min\nNo data");
  lv_obj_set_style_text_color(label_min, lv_color_hex(0x00BFFF), 0);

  label_max = lv_label_create(main_scr);
  lv_obj_align(label_max, LV_ALIGN_RIGHT_MID, -30, 10);
  lv_obj_set_style_text_font(label_max, &lv_font_montserrat_24, 0);
  lv_label_set_text(label_max, "Max\nNo data");
  lv_obj_set_style_text_color(label_max, lv_color_hex(0xFF4444), 0);

  label_status = lv_label_create(main_scr);
  lv_obj_align(label_status, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_obj_set_style_text_font(label_status, &lv_font_montserrat_16, 0);
  lv_label_set_text(label_status, "");
  lv_obj_set_style_text_color(label_status, lv_color_hex(0xFF6347), 0);

  label_time = lv_label_create(main_scr);
  lv_obj_align(label_time, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_obj_set_style_text_font(label_time, &lv_font_montserrat_16, 0);
  lv_label_set_text(label_time, "Time: N/A");
  lv_obj_set_style_text_color(label_time, lv_color_white(), 0);

  label_date = lv_label_create(main_scr);
  lv_obj_align(label_date, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  lv_obj_set_style_text_font(label_date, &lv_font_montserrat_16, 0);
  lv_label_set_text(label_date, "Date: N/A");
  lv_obj_set_style_text_color(label_date, lv_color_white(), 0);
}

void create_history_ui()
{
  history_scr = lv_obj_create(NULL);
  apply_dark_theme(history_scr);
  lv_obj_add_event_cb(history_scr, gesture_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(history_scr, history_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);

  history_container = lv_obj_create(history_scr);
  lv_obj_set_size(history_container, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_align(history_container, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_flex_flow(history_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(history_container, 2, 0);
  lv_obj_set_scrollbar_mode(history_container, LV_SCROLLBAR_MODE_AUTO);
}

void create_settings_ui()
{
  settings_scr = lv_obj_create(NULL);
  apply_dark_theme(settings_scr);
  lv_obj_add_event_cb(settings_scr, gesture_cb, LV_EVENT_GESTURE, NULL);

  lv_obj_t *lbl_bright = lv_label_create(settings_scr);
  lv_obj_align(lbl_bright, LV_ALIGN_TOP_MID, 0, 20);
  lv_obj_set_style_text_font(lbl_bright, &lv_font_montserrat_22, 0);
  lv_label_set_text(lbl_bright, "Brightness");
  lv_obj_set_style_text_color(lbl_bright, lv_color_white(), 0);

  slider_brightness = lv_slider_create(settings_scr);
  lv_obj_align(slider_brightness, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_set_width(slider_brightness, 280);
  lv_slider_set_range(slider_brightness, 0, 255);
  lv_slider_set_value(slider_brightness, brightness, LV_ANIM_OFF);
  lv_obj_add_event_cb(slider_brightness, slider_brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);

  lv_obj_t *lbl_auto = lv_label_create(settings_scr);
  lv_obj_align(lbl_auto, LV_ALIGN_CENTER, -60, 0);
  lv_obj_set_style_text_font(lbl_auto, &lv_font_montserrat_22, 0);
  lv_label_set_text(lbl_auto, "Auto Brightness");
  lv_obj_set_style_text_color(lbl_auto, lv_color_white(), 0);

  switch_auto_brightness = lv_switch_create(settings_scr);
  lv_obj_align(switch_auto_brightness, LV_ALIGN_CENTER, 100, 0);
  lv_obj_add_state(switch_auto_brightness, LV_STATE_CHECKED);
  lv_obj_add_event_cb(switch_auto_brightness, switch_auto_brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);

  btn_reset = lv_btn_create(settings_scr);
  lv_obj_align(btn_reset, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_size(btn_reset, 240, 60);
  lv_obj_add_event_cb(btn_reset, reset_session_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_set_style_bg_color(btn_reset, lv_color_hex(0xDC143C), 0);
  lv_obj_t *lbl_reset = lv_label_create(btn_reset);
  lv_label_set_text(lbl_reset, "Reset Min/Max");
  lv_obj_center(lbl_reset);
  lv_obj_set_style_text_font(lbl_reset, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(lbl_reset, lv_color_white(), 0);
}

/* Time update task - every second */
void time_update_task(lv_timer_t *)
{
  if (time_ok) {
    time_t now = time(nullptr);
    struct tm *tmn = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tmn);
    lv_label_set_text(label_time, time_buf);
  }
}

/* Temperature task - every 10 seconds */
void temp_task(lv_timer_t *)
{
  sensors.requestTemperatures();
  float t = sensors.getTempCByIndex(0);
  bool valid_temp = (t != DEVICE_DISCONNECTED_C && t != -127.0 && t < 85.0);

  if (valid_temp) {
    char buf[32];
    sprintf(buf, "%.1f °C", t);
    lv_label_set_text(label_temp, buf);
  } else {
    lv_label_set_text(label_temp, "No sensor");
  }

  bool session_updated = false;
  if (valid_temp) {
    if (session_min > session_max) {
      session_min = session_max = t;
      session_updated = true;
    } else {
      if (t < session_min) { session_min = t; session_updated = true; }
      if (t > session_max) { session_max = t; session_updated = true; }
    }
  }
  if (session_updated) save_session();
  update_main_min_max_labels();

  time_t now = time(nullptr);
  struct tm *tmn = localtime(&now);
  time_ok = (tmn->tm_year + 1900 >= 2024);

  if (time_ok) {
    char date_buf[32];
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", tmn);
    lv_label_set_text(label_date, date_buf);

    bool new_day = (tmn->tm_mday != last_tm.tm_mday || tmn->tm_mon != last_tm.tm_mon || tmn->tm_year != last_tm.tm_year);
    if (new_day && last_tm.tm_year > 0) {
      today_min = 100.0;
      today_max = -100.0;
    }

    if (valid_temp) {
      if (today_min > today_max) {
        today_min = today_max = t;
      } else {
        today_min = std::min(today_min, t);
        today_max = std::max(today_max, t);
      }
    }

    bool logged = false;
    if (sd_ok && valid_temp) {
      if (!handle_sd_full()) {
        sd_ok = false;
      } else {
        String fname = get_monthly_csv(tmn);
        File f = SD.open(fname.c_str(), FILE_APPEND);
        if (f) {
          char buf[32];
          strftime(buf, sizeof(buf), "%F %T", tmn);
          f.printf("%s,%.2f\n", buf, t);
          f.close();
          logged = true;
        }
      }
    }

    if (logged) {
      digitalWrite(LED_GREEN, LOW);
      delay(100);
      digitalWrite(LED_GREEN, HIGH);
    }

    last_tm = *tmn;
  }

  if (auto_brightness) {
    int light_raw = analogRead(LDR_PIN);
    int backlight = map(light_raw, 0, 4095, 30, 255);
    backlight = constrain(backlight, 30, 255);
    ledcWrite(0, backlight);
  } else {
    ledcWrite(0, brightness);
  }

  String status = "";
  if (!time_ok) status += "No time";
  if (!sd_ok) { if (!status.isEmpty()) status += " • "; status += "No SD"; }
  if (!valid_temp) { if (!status.isEmpty()) status += " • "; status += "No sensor"; }
  lv_label_set_text(label_status, status.c_str());

  // RGB LED
  static uint32_t last_flash = 0;
  static bool flash_state = HIGH;
  if (!time_ok) {
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, LOW);
  } else if (!sd_ok) {
    if (millis() - last_flash > 500) {
      flash_state = !flash_state;
      digitalWrite(LED_RED, flash_state ? LOW : HIGH);
      last_flash = millis();
    }
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, HIGH);
  } else if (!valid_temp) {
    if (millis() - last_flash > 150) {
      flash_state = !flash_state;
      digitalWrite(LED_RED, flash_state ? LOW : HIGH);
      last_flash = millis();
    }
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, HIGH);
  } else {
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, HIGH);
  }
}

/* Web Handlers */
void handle_root() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html><head><title>Temp Logger</title><meta charset="UTF-8"><style>
  body { background:#121212; color:#fff; font-family:Arial; margin:0 auto; max-width:800px; padding:20px; text-align:center; }
  h1,h2 { color:#bb86fc; } .card { background:#1f1f1f; padding:15px; margin:20px auto; border-radius:8px; max-width:600px; }
  table { width:100%; border-collapse:collapse; } th,td { padding:8px; border-bottom:1px solid #333; text-align:left; }
  th { background:#333; } a { color:#03dac6; }
</style></head><body><h1>Temp Logger</h1>
<script>
async function syncTime(){ const p=new URLSearchParams({epoch:Math.floor(Date.now()/1000),tz_offset:new Date().getTimezoneOffset()});
await fetch('/settime',{method:'POST',body:p}); loadData(); }
async function loadData(){ const r=await fetch('/data'); const d=await r.json();
document.getElementById('current').innerHTML=`
<div class="card"><h3>Current Data</h3><p>Temperature: ${d.current_temp} °C</p><p>Time: ${d.time}</p>
<p>Session Min/Max: ${d.session_min} / ${d.session_max} °C</p><p>Today Min/Max: ${d.today_min} / ${d.today_max} °C</p></div>`;
let h=`<div class="card"><h3>Last 7 Days</h3><table><tr><th>Date</th><th>Min (°C)</th><th>Max (°C)</th></tr>`;
d.history.forEach(day=>{ h+=`<tr><td>${day.date}</td><td>${typeof day.min==='number'?day.min.toFixed(1):day.min}</td><td>${typeof day.max==='number'?day.max.toFixed(1):day.max}</td></tr>`; });
h+='</table></div>'; document.getElementById('history').innerHTML=h; }
window.onload=syncTime;
</script>
<div id="current"></div><div id="history"></div>
<h2><a href="/files">Manage CSVs</a></h2>
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handle_files() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><title>Manage CSVs</title><meta charset="UTF-8"><style>
body{background:#121212;color:#fff;font-family:Arial;margin:0 auto;max-width:800px;padding:20px;text-align:center;}
h1{color:#bb86fc;}ul{list-style:none;padding:0;}li{margin:10px;display:flex;justify-content:space-between;}
a,button{color:#03dac6;background:none;border:none;cursor:pointer;}
</style></head><body><h1>Manage CSVs</h1><ul>
)rawliteral";

  std::vector<String> files;
  list_monthly_csvs(files);
  if (files.empty()) {
    html += "<li>No CSV files found yet.</li>";
  } else {
    for (const String& fname : files) {
      String month = fname.substring(6, 13);
      html += "<li>" + month + " <a href='/download?month=" + month + "'>Download</a> ";
      html += "<form method='POST' action='/delete'><input type='hidden' name='month' value='" + month + "'><button type='submit'>Delete</button></form></li>";
    }
  }
  html += R"rawliteral(</ul><p><a href="/">← Back</a></p></body></html>)rawliteral";
  server.send(200, "text/html", html);
}

void handle_settime() {
  String epoch_str = server.arg("epoch");
  String tz_offset_str = server.arg("tz_offset");
  time_t epoch = epoch_str.toInt();
  timeval tv = {epoch, 0};
  settimeofday(&tv, nullptr);
  if (!tz_offset_str.isEmpty()) {
    int tz_offset_min = tz_offset_str.toInt();
    int hours = tz_offset_min / 60;
    int mins = abs(tz_offset_min % 60);
    char tz_buf[20];
    sprintf(tz_buf, mins == 0 ? "LOC%d" : "LOC%d:%02d", hours, mins);
    setenv("TZ", tz_buf, 1);
    tzset();
  }
  time_ok = true;
  Serial.println("Time and TZ synced via WiFi!");
  server.send(200, "text/plain", "Time set");
}

void handle_delete() {
  String month = server.arg("month");
  String fname = "/temp-" + month + ".csv";
  if (SD.exists(fname.c_str())) {
    SD.remove(fname.c_str());
    server.sendHeader("Location", "/files");
    server.send(303);
  } else {
    server.send(404, "text/plain", "File not found");
  }
}

void handle_download() {
  String month = server.arg("month");
  String fname = "/temp-" + month + ".csv";
  if (SD.exists(fname.c_str())) {
    File file = SD.open(fname.c_str());
    server.streamFile(file, "text/csv");
    file.close();
  } else {
    server.send(404, "text/plain", "File not found");
  }
}

void handle_data() {
  sensors.requestTemperatures();
  float current_temp = sensors.getTempCByIndex(0);
  if (current_temp == DEVICE_DISCONNECTED_C || current_temp == -127.0) current_temp = -999;
  time_t now = time(nullptr);
  struct tm *tmn = localtime(&now);
  char curr_time[64];
  strftime(curr_time, sizeof(curr_time), "%Y-%m-%d %H:%M:%S", tmn);

  String json = "{";
  json += "\"current_temp\":" + (current_temp > -999 ? String(current_temp, 1) : "\"No sensor\"") + ",";
  json += "\"time\":\"" + String(curr_time) + "\",";
  json += "\"session_min\":" + String(session_min, 1) + ",";
  json += "\"session_max\":" + String(session_max, 1) + ",";
  json += "\"today_min\":" + String(today_min, 1) + ",";
  json += "\"today_max\":" + String(today_max, 1) + ",";
  json += "\"history\":[";

  if (time_ok) {
    struct tm curr = *tmn;
    curr.tm_hour = curr.tm_min = curr.tm_sec = 0;
    time_t curr_midnight = mktime(&curr);
    DayMinMax days[7];
    for (int i = 0; i < 7; i++) { days[i].min = 100.0; days[i].max = -100.0; }

    std::vector<String> files;
    list_monthly_csvs(files);
    for (const String& fname : files) {
      File f = SD.open(fname.c_str());
      if (!f) continue;
      char line[64];
      while (f.available()) {
        size_t len = f.readBytesUntil('\n', line, sizeof(line)-1);
        line[len] = '\0';
        int y,mo,d,h,mi,s; float t;
        if (sscanf(line, "%d-%d-%d %d:%d:%d,%f", &y,&mo,&d,&h,&mi,&s,&t)==7) {
          struct tm log_tm = {s,mi,h,d,mo-1,y-1900};
          time_t log_midnight = mktime(&log_tm);
          double diff = difftime(curr_midnight, log_midnight)/86400.0;
          if (diff >= 0 && diff < 7) {
            int idx = (int)diff;
            days[idx].min = std::min(days[idx].min, t);
            days[idx].max = std::max(days[idx].max, t);
          }
        }
      }
      f.close();
    }

    // Apply in-memory today
    if (today_min <= today_max) {
      days[0].min = std::min(days[0].min, today_min);
      days[0].max = std::max(days[0].max, today_max);
    }

    for (int i = 0; i < 7; i++) {
      struct tm day_tm = curr;
      day_tm.tm_mday -= i;
      mktime(&day_tm);
      char date_str[30];
      strftime(date_str, sizeof(date_str), "%A, %Y-%m-%d", &day_tm);
      json += "{\"date\":\"" + String(date_str) + "\",";
      json += "\"min\":" + (days[i].min <= days[i].max ? String(days[i].min,1) : "\"No data\"") + ",";
      json += "\"max\":" + (days[i].min <= days[i].max ? String(days[i].max,1) : "\"No data\"") + "}";
      if (i < 6) json += ",";
    }
  }
  json += "]}";
  server.send(200, "application/json", json);
}

/* Setup */
void setup()
{
  Serial.begin(115200);

  sensors.begin();
  ts.begin();

  tft.begin();
  tft.setRotation(1);
  tft.invertDisplay(false);

  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);

  ledcSetup(0, 5000, 8);
  ledcAttachPin(BACKLIGHT_PIN, 0);
  ledcWrite(0, brightness);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  delay(500);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  if (!MDNS.begin("templogger")) {
    Serial.println("mDNS failed");
  } else {
    Serial.println("Access via http://templogger.local");
    MDNS.addService("http", "tcp", 80);
  }

  server.on("/", handle_root);
  server.on("/files", handle_files);
  server.on("/settime", HTTP_POST, handle_settime);
  server.on("/delete", HTTP_POST, handle_delete);
  server.on("/data", handle_data);
  server.on("/download", handle_download);
  server.begin();

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, SCREEN_WIDTH * 20);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_WIDTH;
  disp_drv.ver_res = SCREEN_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_read;
  lv_indev_drv_register(&indev_drv);

  create_main_ui();
  create_history_ui();
  create_settings_ui();
  lv_scr_load(main_scr);

  sd_ok = SD.begin(SD_CS);
  Serial.printf("SD OK: %s\n", sd_ok ? "yes" : "no");

  load_session();
  update_main_min_max_labels();

  lv_timer_create(temp_task, 10000, nullptr);
  lv_timer_create(time_update_task, 1000, nullptr);

  Serial.println("Setup done");
}

/* Loop */
void loop()
{
  uint32_t now = millis();
  static uint32_t last_tick = 0;
  lv_tick_inc(now - last_tick);
  last_tick = now;

  lv_timer_handler();
  server.handleClient();
  delay(5);
}
