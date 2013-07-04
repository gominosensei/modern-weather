#include "pebble_os.h"
#include "pebble_app.h"
#include "pebble_fonts.h"
#include "build_config.h"
// Modern includes	
#include "lang.h"
// Weather includes	
#include "http.h"
#include "util.h"
#include "weather_layer.h"
#include "constants.h"

PBL_APP_INFO(HTTP_UUID, "Modern Weather", "michael donnelly",
             1, 7, RESOURCE_ID_IMAGE_MENU_ICON, APP_INFO_WATCH_FACE);

Window window;
BmpContainer background_image_container;

// Modern face
Layer minute_display_layer;
Layer hour_display_layer;
Layer center_display_layer;
Layer second_display_layer;
TextLayer date_layer;
GFont date_font;
static char date_text[] = "Wed 13";

// Weather face
static WeatherLayer weather_layer;
static int our_latitude, our_longitude;
static bool located;
static uint8_t precip_forecast[60];
static int8_t precip_forecast_index;
void request_weather();
void handle_timer(AppContextRef app_ctx, AppTimerHandle handle, uint32_t cookie);

void failed(int32_t cookie, int http_status, void* context) {
	if(cookie == 0 || cookie == WEATHER_HTTP_COOKIE) {
		// *msd 7/4/13 Don't show an error icon - just leave it blank
		//weather_layer_set_icon(&weather_layer, WEATHER_ICON_NO_WEATHER);
	}
}
void success(int32_t cookie, int http_status, DictionaryIterator* received, void* context) {
	if(cookie != WEATHER_HTTP_COOKIE) return;
	Tuple* data_tuple = dict_find(received, WEATHER_KEY_CURRENT);
	if(data_tuple) {
		// The below bitwise dance is so we can actually fit our precipitation forecast.
		uint16_t value = data_tuple->value->int16;
		uint8_t icon = value >> 11;
		if(icon < 10) {
			weather_layer_set_icon(&weather_layer, icon);
		} else {
			weather_layer_set_icon(&weather_layer, WEATHER_ICON_NO_WEATHER);
		}
		int16_t temp = value & 0x3ff;
		if(value & 0x400) temp = -temp;
		weather_layer_set_temperature(&weather_layer, temp);
	}
	Tuple* forecast_tuple = dict_find(received, WEATHER_KEY_PRECIPITATION);
	if(forecast_tuple) {
		// It's going to rain!
		memset(precip_forecast, 0, 60);
		memcpy(precip_forecast, forecast_tuple->value->data, forecast_tuple->length > 60 ? 60 : forecast_tuple->length);
		precip_forecast_index = 0;
		weather_layer_set_precipitation_forecast(&weather_layer, precip_forecast, 60);
	} else {
		weather_layer_clear_precipitation_forecast(&weather_layer);
	}
}

void reconnect(void* context) {
	request_weather();
}

const GPathInfo MINUTE_HAND_PATH_POINTS = {
  3,
  (GPoint []) {
    {-4, 15},
    {4, 15},
    {4, -70},
    {-4,  -70},
  }
};

const GPathInfo HOUR_HAND_PATH_POINTS = {
  3,
  (GPoint []) {
    {-4, 15},
    {4, 15},
    {4, -50},
    {-4,  -50},
  }
};


GPath hour_hand_path;
GPath minute_hand_path;

AppTimerHandle timer_handle;
#define ANIM_IDLE 0
#define ANIM_START 1
#define ANIM_HOURS 2
#define ANIM_MINUTES 3
#define ANIM_SECONDS 4
#define ANIM_DONE 5
int init_anim = ANIM_DONE;
int32_t second_angle_anim = 0;
unsigned int minute_angle_anim = 0;
unsigned int hour_angle_anim = 0;

void handle_timer(AppContextRef ctx, AppTimerHandle handle, uint32_t cookie) {
  (void)ctx;
  (void)handle;

  if (cookie == COOKIE_MY_TIMER) {
    if(init_anim == ANIM_START)
    {
        init_anim = ANIM_HOURS;
        timer_handle = app_timer_send_event(ctx, 50 /* milliseconds */, COOKIE_MY_TIMER);
    }
    else if(init_anim==ANIM_HOURS)
    {
        layer_mark_dirty(&hour_display_layer);
        timer_handle = app_timer_send_event(ctx, 50 /* milliseconds */, COOKIE_MY_TIMER);
    }
    else if(init_anim==ANIM_MINUTES)
    {
        layer_mark_dirty(&minute_display_layer);
        timer_handle = app_timer_send_event(ctx, 50 /* milliseconds */, COOKIE_MY_TIMER);
    }
#if DISPLAY_SECONDS
    else if(init_anim==ANIM_SECONDS)
    {
        layer_mark_dirty(&second_display_layer);
        timer_handle = app_timer_send_event(ctx, 50 /* milliseconds */, COOKIE_MY_TIMER);
    }
#endif
  }
  else if (cookie == COOKIE_WEATHER_TIMER) {
	request_weather();
	app_timer_send_event(ctx, REFRESH_WEATHER, COOKIE_WEATHER_TIMER);
  }
}

#if DISPLAY_SECONDS && INVERTED
void second_display_layer_update_callback(Layer *me, GContext* ctx) {
  (void)me;

  PblTm t;
  get_time(&t);

  int32_t second_angle = t.tm_sec * (0xffff/60);
  int second_hand_length = 70;
  GPoint center = grect_center_point(&me->frame);
  GPoint second = GPoint(center.x, center.y - second_hand_length);

  if(init_anim < ANIM_SECONDS)
  {
     second = GPoint(center.x, center.y - 70);
  }
  else if(init_anim==ANIM_SECONDS)
  {
     second_angle_anim += 0xffff/60;
     if(second_angle_anim >= second_angle)
     {
        init_anim = ANIM_DONE;
        second = GPoint(center.x + second_hand_length * sin_lookup(second_angle)/0xffff,
				center.y + (-second_hand_length) * cos_lookup(second_angle)/0xffff);
     }
     else
     {
        second = GPoint(center.x + second_hand_length * sin_lookup(second_angle_anim)/0xffff,
				center.y + (-second_hand_length) * cos_lookup(second_angle_anim)/0xffff);
     }
  }
  else 
  {
     second = GPoint(center.x + second_hand_length * sin_lookup(second_angle)/0xffff,
			center.y + (-second_hand_length) * cos_lookup(second_angle)/0xffff);
  }

  graphics_context_set_stroke_color(ctx, GColorBlack);

  graphics_draw_line(ctx, center, second);
}
#elif DISPLAY_SECONDS
void second_display_layer_update_callback(Layer *me, GContext* ctx) {
  (void)me;

  PblTm t;
  get_time(&t);

  int32_t second_angle = t.tm_sec * (0xffff/60);
  int second_hand_length = 70;
  GPoint center = grect_center_point(&me->frame);
  GPoint second = GPoint(center.x, center.y - second_hand_length);

  if(init_anim < ANIM_SECONDS)
  {
     second = GPoint(center.x, center.y - 70);
  }
  else if(init_anim==ANIM_SECONDS)
  {
     second_angle_anim += 0xffff/60;
     if(second_angle_anim >= second_angle)
     {
        init_anim = ANIM_DONE;
        second = GPoint(center.x + second_hand_length * sin_lookup(second_angle)/0xffff,
				center.y + (-second_hand_length) * cos_lookup(second_angle)/0xffff);
     }
     else
     {
        second = GPoint(center.x + second_hand_length * sin_lookup(second_angle_anim)/0xffff,
				center.y + (-second_hand_length) * cos_lookup(second_angle_anim)/0xffff);
     }
  }
  else 
  {
     second = GPoint(center.x + second_hand_length * sin_lookup(second_angle)/0xffff,
			center.y + (-second_hand_length) * cos_lookup(second_angle)/0xffff);
  }

  graphics_context_set_stroke_color(ctx, GColorWhite);

  graphics_draw_line(ctx, center, second);
}
#endif
void center_display_layer_update_callback(Layer *me, GContext* ctx) {
  (void)me;

  GPoint center = grect_center_point(&me->frame);
#if INVERTED
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, center, 4);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, center, 3);
#else
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, center, 4);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, center, 3);
#endif
}

void minute_display_layer_update_callback(Layer *me, GContext* ctx) {
  (void)me;

  PblTm t;

  get_time(&t);

  unsigned int angle = t.tm_min * 6 + t.tm_sec / 10;

  if(init_anim < ANIM_MINUTES)
  {
     angle = 0;
  }
  else if(init_anim==ANIM_MINUTES)
  {
     minute_angle_anim += 6;
     if(minute_angle_anim >= angle)
     {
#if DISPLAY_SECONDS
        init_anim = ANIM_SECONDS;
#else
        init_anim = ANIM_DONE;
#endif
     }
     else
     {
        angle = minute_angle_anim;
     }
  }

  gpath_rotate_to(&minute_hand_path, (TRIG_MAX_ANGLE / 360) * angle);


#if INVERTED
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_context_set_stroke_color(ctx, GColorWhite);
#else
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_context_set_stroke_color(ctx, GColorBlack);
#endif

  gpath_draw_filled(ctx, &minute_hand_path);
  gpath_draw_outline(ctx, &minute_hand_path);
}

void hour_display_layer_update_callback(Layer *me, GContext* ctx) {
  (void)me;

  PblTm t;

  get_time(&t);

  unsigned int angle = t.tm_hour * 30 + t.tm_min / 2;

  if(init_anim < ANIM_HOURS)
  {
     angle = 0;
  }
  else if(init_anim==ANIM_HOURS)
  {
     if(hour_angle_anim==0&&t.tm_hour>=12)
     {
        hour_angle_anim = 360;
     }
     hour_angle_anim += 6;
     if(hour_angle_anim >= angle)
     {
        init_anim = ANIM_MINUTES;
     }
     else
     {
        angle = hour_angle_anim;
     }
  }

  gpath_rotate_to(&hour_hand_path, (TRIG_MAX_ANGLE / 360) * angle);

#if INVERTED
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_context_set_stroke_color(ctx, GColorWhite);
#else
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_context_set_stroke_color(ctx, GColorBlack);
#endif

  gpath_draw_filled(ctx, &hour_hand_path);
  gpath_draw_outline(ctx, &hour_hand_path);
}
#if DISPLAY_DATE_ANALOG
void draw_date(){
  PblTm t;
  get_time(&t);
  
  string_format_time(date_text, sizeof(date_text), "%d", &t);
  text_layer_set_text(&date_layer, date_text);
}
#else
void draw_date(){
  PblTm t;
  get_time(&t);
  /*
  string_format_time(date_text, sizeof(date_text), "%a %d", &t);
  text_layer_set_text(&date_layer, date_text);
*/
  char day[] = "14";
  string_format_time(day, sizeof(day), "%d", &t);
  
  size_t size = sizeof(date_text);
  memset(date_text, 0, size);
  
  strncat(date_text, DAYS[t.tm_wday], size);
  size -= strlen(DAYS[t.tm_wday]);
  strncat(date_text, " ", size);
  size -= strlen(" ");
  strncat(date_text, day, size);
  text_layer_set_text(&date_layer, date_text);
}
#endif

	/*
void set_weather_timer(AppContextRef ctx) {
	app_timer_send_event(ctx, 1740000, COOKIE_WEATHER_TIMER);
}*/

void location(float latitude, float longitude, float altitude, float accuracy, void* context) {
	// Fix the floats
	our_latitude = latitude * 10000;
	our_longitude = longitude * 10000;
	located = true;
	request_weather();
//	set_weather_timer((AppContextRef)context);
	app_timer_send_event((AppContextRef)context, REFRESH_WEATHER, COOKIE_WEATHER_TIMER);
}

void handle_init(AppContextRef ctx) {
  (void)ctx;

  window_init(&window, "Modern Watch");
  window_stack_push(&window, true /* Animated */);
  resource_init_current_app(&APP_RESOURCES);
  window_set_fullscreen(&window, true);  // *msd 7/3/13

#if DISPLAY_DATE_ANALOG
  bmp_init_container(RESOURCE_ID_IMAGE_BACKGROUND_DATEBOX, &background_image_container);
#elif INVERTED
  bmp_init_container(RESOURCE_ID_IMAGE_BACKGROUND_INVERTED, &background_image_container);
#else
  bmp_init_container(RESOURCE_ID_IMAGE_BACKGROUND, &background_image_container);
#endif
  layer_add_child(&window.layer, &background_image_container.layer.layer);


	// Add weather layer
//	weather_layer_init(&weather_layer, GPoint(0, 100));
	weather_layer_init(&weather_layer, GPoint(0, 0));  // *msd 7/3/13
	layer_add_child(&window.layer, &weather_layer.layer);
	
	http_register_callbacks((HTTPCallbacks){
		.failure=failed,
		.success=success,
		.reconnect=reconnect,
		.location=location
	}, (void*)ctx);
	
	// Request weather
	precip_forecast_index = -1;
	located = false;
	request_weather();
	
	
	
	
#if DISPLAY_DATE_ANALOG
  date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ORBITRON_MEDIUM_12));
  text_layer_init(&date_layer, GRect(116, 77, 20, 20));
#else
  date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DIGITALDREAM_NARROW_12));
  text_layer_init(&date_layer, GRect(27, 110, 90, 30));
#endif
#if DISPLAY_DATE_ANALOG
  text_layer_set_text_color(&date_layer, GColorBlack);
#elif INVERTED
  text_layer_set_text_color(&date_layer, GColorBlack);
#else
  text_layer_set_text_color(&date_layer, GColorWhite);
#endif
  text_layer_set_text_alignment(&date_layer, GTextAlignmentCenter);
  text_layer_set_background_color(&date_layer, GColorClear);
  text_layer_set_font(&date_layer, date_font);
  layer_add_child(&window.layer, &date_layer.layer);

  draw_date();

  layer_init(&hour_display_layer, window.layer.frame);
  hour_display_layer.update_proc = &hour_display_layer_update_callback;
  layer_add_child(&window.layer, &hour_display_layer);

  gpath_init(&hour_hand_path, &HOUR_HAND_PATH_POINTS);
  gpath_move_to(&hour_hand_path, grect_center_point(&hour_display_layer.frame));
	
  layer_init(&minute_display_layer, window.layer.frame);
  minute_display_layer.update_proc = &minute_display_layer_update_callback;
  layer_add_child(&window.layer, &minute_display_layer);

  gpath_init(&minute_hand_path, &MINUTE_HAND_PATH_POINTS);
  gpath_move_to(&minute_hand_path, grect_center_point(&minute_display_layer.frame));

  layer_init(&center_display_layer, window.layer.frame);
  center_display_layer.update_proc = &center_display_layer_update_callback;
  layer_add_child(&window.layer, &center_display_layer);
#if DISPLAY_SECONDS
  layer_init(&second_display_layer, window.layer.frame);
  second_display_layer.update_proc = &second_display_layer_update_callback;
  layer_add_child(&window.layer, &second_display_layer);
#endif
	

}

void handle_deinit(AppContextRef ctx) {
  (void)ctx;

  bmp_deinit_container(&background_image_container);
  fonts_unload_custom_font(date_font);
  weather_layer_deinit(&weather_layer);
}

void handle_tick(AppContextRef ctx, PebbleTickEvent *t){
  (void)t;
  (void)ctx;

  if(init_anim == ANIM_IDLE)
  {
     init_anim = ANIM_START;
     timer_handle = app_timer_send_event(ctx, 50 /* milliseconds */, COOKIE_MY_TIMER);
  }
  else if(init_anim == ANIM_DONE)
  {
  if(t->tick_time->tm_sec%10==0)
  {
     layer_mark_dirty(&minute_display_layer);
     
     if(t->tick_time->tm_sec==0)
     {
        if(t->tick_time->tm_min%2==0)
        {
           layer_mark_dirty(&hour_display_layer);
           if(t->tick_time->tm_min==0&&t->tick_time->tm_hour==0)
           {
              draw_date();
           }
#if HOUR_VIBRATION
           if(t->tick_time->tm_min==0
                 &&t->tick_time->tm_hour>=HOUR_VIBRATION_START
                    &&t->tick_time->tm_hour<=HOUR_VIBRATION_END)
           {
              vibes_double_pulse();
           }
#endif
        }
     }
  }

#if DISPLAY_SECONDS
  layer_mark_dirty(&second_display_layer);
#endif
  }
	
  // Weather
  if(precip_forecast_index >= 0) {
		++precip_forecast_index;
		if(precip_forecast_index > 60) {
			weather_layer_clear_precipitation_forecast(&weather_layer);
			precip_forecast_index = -1;
		} else {
			weather_layer_set_precipitation_forecast(&weather_layer, &precip_forecast[precip_forecast_index], 60 - precip_forecast_index);
		}
  }	
}



void pbl_main(void *params) {
  PebbleAppHandlers handlers = {
    .init_handler = &handle_init,
    .deinit_handler = &handle_deinit,
    .timer_handler = &handle_timer,
    .tick_info = {
			.tick_handler = &handle_tick,
#if DISPLAY_SECONDS
			.tick_units = SECOND_UNIT
#else 
			.tick_units = MINUTE_UNIT
#endif
		},
	.messaging_info = {
		.buffer_sizes = {
			.inbound = 124,
			.outbound = 256,
		}
	}
  };
  app_event_loop(params, &handlers);
}

void request_weather() {
	if(!located) {
		http_location_request();
		return;
	}
	// Build the HTTP request
	DictionaryIterator *body;
	HTTPResult result = http_out_get(WEATHER_SERVICE_URL, WEATHER_HTTP_COOKIE, &body);  // *msd 7/3/13
//	HTTPResult result = http_out_get("http://pwdb.kathar.in/pebble/weather3.php", WEATHER_HTTP_COOKIE, &body);
	if(result != HTTP_OK) {
		weather_layer_set_icon(&weather_layer, WEATHER_ICON_NO_WEATHER);
		return;
	}
	dict_write_int32(body, WEATHER_KEY_LATITUDE, our_latitude);
	dict_write_int32(body, WEATHER_KEY_LONGITUDE, our_longitude);
	dict_write_cstring(body, WEATHER_KEY_UNIT_SYSTEM, UNIT_SYSTEM);
	// Send it.
	if(http_out_send() != HTTP_OK) {
		weather_layer_set_icon(&weather_layer, WEATHER_ICON_NO_WEATHER);
		return;
	}
}