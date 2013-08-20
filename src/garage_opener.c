#include "pebble_os.h"
#include "pebble_app.h"
#include "pebble_fonts.h"

#include "http.h"

#include "config_private.h" // This file defines the URL for the garage server
#include "garage_opener.h"

static void handle_init(AppContextRef ctx);
static void handle_deinit(AppContextRef ctx);
static void handle_timer_events(AppContextRef ctx, AppTimerHandle handle, uint32_t cookie);

static void failure(int32_t cookie, int status, void* ctx);
static void success(int32_t cookie, int status, DictionaryIterator* recv, void* ctx);
static bool get_garage_status(void);

static void click_provider(ClickConfig** config, void* ctx);
static void toggle_garage_door(ClickRecognizerRef rec, void* ctx);
static bool set_garage_status(int value);
static char* make_message(char* str);


PBL_APP_INFO(HTTP_UUID,
             "Garage Opener", "Tim",
             1, 0, /* App version */
             RESOURCE_ID_GARAGE_ICON,
             APP_INFO_STANDARD_APP);

Window window;

static TextLayer garage_status;
static AppTimerHandle garage_poller;
static bool get_garage_status_running = false;
static bool set_garage_status_running = false;
static int keepalive = 0;


void pbl_main(void *params)
{
  PebbleAppHandlers handlers =
  {
    .init_handler = &handle_init,
    .deinit_handler = &handle_deinit,
    .timer_handler = &handle_timer_events,
    .messaging_info =
    {
      .buffer_sizes =
      {
        .inbound = 124,
        .outbound = 256
      }
    }
  };

  app_event_loop(params, &handlers);
}

static void handle_init(AppContextRef ctx)
{
  window_init(&window, "Garage Opener");
  window_stack_push(&window, true /* Animated */);

  // Setup HTTP
  http_register_callbacks((HTTPCallbacks)
  {
    .failure = failure,
    .success = success
  }, ctx);

  // Setup message
  text_layer_init(&garage_status, GRect(0, 65, 144, 30));
  text_layer_set_text_alignment(&garage_status, GTextAlignmentCenter);
  text_layer_set_text(&garage_status, "Updating...");
  text_layer_set_font(&garage_status, fonts_get_system_font(FONT_KEY_ROBOTO_CONDENSED_21));
  layer_add_child(&window.layer, &garage_status.layer);

  // Setup buttons
  window_set_click_config_provider(&window, &click_provider);

  // Start timers
  garage_poller = app_timer_send_event(ctx, GARAGE_POLLING_INTERVAL, GARAGE_POLLING_TIMER_COOKIE);
  keepalive = GARAGE_KEEPALIVE_INTERVAL / GARAGE_POLLING_INTERVAL;

  // Get the current status
  get_garage_status();
}

static void handle_deinit(AppContextRef ctx)
{
  app_timer_cancel_event(ctx, garage_poller);
}

static void handle_timer_events(AppContextRef ctx, AppTimerHandle handle, uint32_t cookie)
{
  if (handle == garage_poller)
  {
    get_garage_status();
    garage_poller = app_timer_send_event(ctx, GARAGE_POLLING_INTERVAL, GARAGE_POLLING_TIMER_COOKIE);
    if (--keepalive <= 0)
    {
      window_stack_pop_all(true);
    }
  }
}

// BUTTONS

static void click_provider(ClickConfig** config, void* ctx)
{
  config[BUTTON_ID_SELECT]->click.handler = toggle_garage_door;
}

static void toggle_garage_door(ClickRecognizerRef rec, void* ctx)
{
  keepalive = GARAGE_KEEPALIVE_INTERVAL / GARAGE_POLLING_INTERVAL;
  set_garage_status(1);
}

// HTTP

static void failure(int32_t cookie, int status, void* ctx)
{
  get_garage_status_running = false;
  set_garage_status_running = false;
}

static void success(int32_t cookie, int status, DictionaryIterator* recv, void* ctx)
{
  switch (cookie)
  {
    case GARAGE_COOKIE_1:
      get_garage_status_running = false;
      Tuple* data = dict_find(recv, GARAGE_STATUS_ID);
      if (data && data->type == TUPLE_CSTRING)
      {
        text_layer_set_text(&garage_status, make_message(data->value->cstring));
      }
      break;

    case GARAGE_COOKIE_2:
      set_garage_status_running = false;
      break;

    default:
      break;
  }
}

static void add_cache_busting(DictionaryIterator* body)
{
  static int cacheBust = 0;

  if (cacheBust == 0)
  {
    cacheBust = time(NULL);
  }
  dict_write_int32(body, GARAGE_CACHE_BUST_ID, cacheBust++);
}

static bool get_garage_status()
{
  if (get_garage_status_running)
  {
    return false;
  }

  DictionaryIterator* body;
  HTTPResult result = http_out_get(GARAGE_STATUS_URL, GARAGE_COOKIE_1, &body);
  if (result != HTTP_OK)
  {
    return false;
  }

  dict_write_cstring(body, GARAGE_TARGET_ID, GARAGE_TARGET_NAME);
  add_cache_busting(body);

  if (http_out_send() != HTTP_OK)
  {
    return false;
  }

  get_garage_status_running = true;

  return true;
}

static bool set_garage_status(int value)
{
  if (set_garage_status_running)
  {
    return false;
  }

  DictionaryIterator* body;
  HTTPResult result = http_out_get(GARAGE_STATUS_URL, GARAGE_COOKIE_2, &body);
  if (result != HTTP_OK)
  {
    return false;
  }

  dict_write_cstring(body, GARAGE_TARGET_ID, GARAGE_TARGET_NAME);
  dict_write_int32(body, GARAGE_OVERRIDE_ID, value);
  add_cache_busting(body);

  if (http_out_send() != HTTP_OK)
  {
    return false;
  }

  text_layer_set_text(&garage_status, make_message("..."));
  set_garage_status_running = true;

  return true;
}

static char* make_message(char* str)
{
#define BUFSZ 200
  static char text[BUFSZ];
  strncpy(text, GARAGE_TARGET_NAME, BUFSZ);
  strncat(text, ": ", BUFSZ);
  strncat(text, str, BUFSZ);
  return text;
}
