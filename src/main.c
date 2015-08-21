#include <pebble.h>

#define STRING_MAX_SIZE 32
#define STOPS_MAX_SIZE 41
#define ARRIVALS_MAX_SIZE 50
#define SCROLL_WAIT 500
#define MAX_VISIBLE 160
#define MINIMUM_UPDATE_SECONDS 20
#define ROUTE_MAX_SIZE 100
#define ALERT_INTERVAL 2000
#define VERSION "v2.10"
    
enum {
    KEY_BUS_STOPS = 0,
    KEY_BUS_SERVICES = 1,
    KEY_BUS_ROUTE = 2,
    KEY_WAKE_BOOL = 3,
    KEY_WAKE_SUMMARY = 4,
    INDEX_DESCRIPTION = 100,
    INDEX_DETAILS = 200,
    INDEX_SERVICE = 300,
    INDEX_ARRIVAL = 400,
};

typedef struct Stops {
    char stopDescription[STRING_MAX_SIZE];
    char stopDetails[(STRING_MAX_SIZE/4)*3];
} Stop;
static int sizeBusStops;
static Stop busStops[STOPS_MAX_SIZE];

static int sizeBusRoute;
static char busRoute[ROUTE_MAX_SIZE][STRING_MAX_SIZE];

typedef struct Buses {
    char service[STRING_MAX_SIZE/2];
    char arrivals[STRING_MAX_SIZE];
} Bus;
static int sizeBusServices;
static Bus busArrivals[ARRIVALS_MAX_SIZE];

static char lastStopDescription[STRING_MAX_SIZE];
static char lastStopRequest[STRING_MAX_SIZE];
static char lastServiceRequest[STRING_MAX_SIZE];
static time_t lastUpdate;

static char destination[STRING_MAX_SIZE] = "NULL";
static char destinationSummary[STRING_MAX_SIZE*5];
static AppTimer *wakeAlertTimer;

static Window *s_nearbyStopsWindow;
static MenuLayer *s_menuLayer;
static ScrollLayer *s_scrollLayer;
static bool nrbyStps_reloadingToScroll = false;
static bool nrbyStps_scrollingStillRequired = false;
static int nrbyStps_scrollOffset = 0;
static AppTimer *nrbyStps_scrollTimer;
static int nrbyStps_scrollIndex = 0;
static char nrbyStps_scrollTmp[STRING_MAX_SIZE];

static Window *s_busArrivalsWindow;
static MenuLayer *s_menuLayer2;
static ScrollLayer *s_scrollLayer2;

static Window *s_splashWindow;
static TextLayer *s_splashText;
static AppTimer *initalFailure;

static Window *s_routeWindow;
static MenuLayer *s_menuLayer4;
static ScrollLayer *s_scrollLayer4;
static bool route_reloadingToScroll = false;
static bool route_scrollingStillRequired = false;
static int route_scrollOffset = 0;
static AppTimer *route_scrollTimer;
static int route_scrollIndex = 0;
static char route_scrollTmp[STRING_MAX_SIZE];

#ifdef PBL_SDK_3
static StatusBarLayer *s_statusBar;
static StatusBarLayer *s_statusBar2;
static StatusBarLayer *s_statusBar3;
static StatusBarLayer *s_statusBar4;
static Layer *s_battery;
static Layer *s_battery2;
static Layer *s_battery3;
static Layer *s_battery4;

static void battery_proc(Layer *layer, GContext *ctx) {
    // Emulator battery meter on Aplite
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_rect(ctx, GRect(126, 4, 14, 8));
    graphics_draw_line(ctx, GPoint(140, 6), GPoint(140, 9));

    BatteryChargeState state = battery_state_service_peek();
    int width = (int)(float)(((float)state.charge_percent / 100.0F) * 10.0F);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(128, 6, width, 4), 0, GCornerNone);
    
    graphics_draw_text(ctx, VERSION, fonts_get_system_font(FONT_KEY_GOTHIC_09), GRect(4, 2, 70, 25), 
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

// Battery State Handler Update
static void batteryHandler(BatteryChargeState charge) {
    if (window_stack_get_top_window() == s_nearbyStopsWindow) {
        layer_mark_dirty(s_battery);
    } else if (window_stack_get_top_window() == s_busArrivalsWindow) {
        layer_mark_dirty(s_battery2);
    } else if (window_stack_get_top_window() == s_splashWindow) {
        layer_mark_dirty(s_battery3);
    } else if (window_stack_get_top_window() == s_routeWindow) {
        layer_mark_dirty(s_battery4);
    }
}
#endif

// Data Request
// Params- char*:request (location/update/lastStopRequest)
static void requestData(char *request) {
    time(&lastUpdate);
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    dict_write_cstring(iter, 45, request);
    app_message_outbox_send();
}

// Ticktimer Handler Update
// s_nearbyStopsWindow: get current bus stop selection and scrolling offset. Request new data.
// s_busArrivalsWindow: get current bus service selection and scrolling offset. Reqeust new data.
static void tickHandler(struct tm *tickTime, TimeUnits unitsChanged) {
    if (window_stack_get_top_window() == s_nearbyStopsWindow) {
        requestData("location");
    } else if (window_stack_get_top_window() == s_busArrivalsWindow) {
        requestData(lastStopRequest);
    }
}

// Accelerometer Handler Update
// s_nearbyStopsWindow: get current bus stop selection and scrolling offset. Request new data.
// s_busArrivalsWindow: get current bus service selection and scrolling offset. Reqeust new data.
static void accelerometerHandler(AccelAxisType axis, int32_t direction) {
    light_enable_interaction();
    
    time_t now = time(NULL);
    if (difftime(now, lastUpdate) < MINIMUM_UPDATE_SECONDS) {
        return;
    }

    if (window_stack_get_top_window() == s_nearbyStopsWindow) {
        requestData("location");
    } else if (window_stack_get_top_window() == s_busArrivalsWindow) {
        requestData(lastStopRequest);
    } 
}

static uint16_t menu_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
    return 1;
}

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    return sizeBusStops;
}

static int16_t menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void menu_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
    menu_cell_basic_header_draw(ctx, cell_layer, "Bus Stops Nearby");
}

static int textWidth(char *str) {
    TextLayer *txtLyr = text_layer_create(GRect(0, 0, 1024, 50));
    text_layer_set_text(txtLyr, str);
    text_layer_set_font(txtLyr, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
    GSize size = text_layer_get_content_size(txtLyr);
    text_layer_destroy(txtLyr);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "%d", (int)size.w);
    return size.w;
}

static void scroll_menu_callback(void) {
    nrbyStps_scrollTimer = NULL;
    nrbyStps_scrollOffset++;
    if (!nrbyStps_scrollingStillRequired) {
        nrbyStps_scrollOffset = -1;
        nrbyStps_scrollingStillRequired = true;
        nrbyStps_scrollTimer = app_timer_register(SCROLL_WAIT, (AppTimerCallback)scroll_menu_callback, NULL);
        return;
    }

    MenuIndex index = menu_layer_get_selected_index(s_menuLayer);
    if (index.row != 0) {
        nrbyStps_reloadingToScroll = true;
    }
    nrbyStps_scrollingStillRequired = false;
    menu_layer_reload_data(s_menuLayer);
    nrbyStps_scrollTimer = app_timer_register(SCROLL_WAIT, (AppTimerCallback)scroll_menu_callback, NULL);
}

static void initiateScrollTimer(void) {
    bool needToCreateTimer = true;
    nrbyStps_scrollingStillRequired = true;
    nrbyStps_scrollOffset = 0;
    nrbyStps_reloadingToScroll = false;

    if (nrbyStps_scrollTimer) {
        needToCreateTimer = !app_timer_reschedule(nrbyStps_scrollTimer, SCROLL_WAIT);
    }

    if (needToCreateTimer) {
        nrbyStps_scrollTimer = app_timer_register(SCROLL_WAIT, (AppTimerCallback)scroll_menu_callback, NULL);
    }
}

static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    int index = cell_index->row;

    if (nrbyStps_scrollIndex == index) {
        if (nrbyStps_scrollOffset == 0) {
            strcpy(nrbyStps_scrollTmp, busStops[index].stopDescription);
        }

        int txtWidth = textWidth(nrbyStps_scrollTmp);
        if (txtWidth > MAX_VISIBLE) {
            int charIndex = 0;
            int maxLen = strlen(busStops[index].stopDescription);
            for (int i=nrbyStps_scrollOffset; i<maxLen; i++) {
                nrbyStps_scrollTmp[charIndex++] = busStops[index].stopDescription[i];
            }
            nrbyStps_scrollTmp[charIndex] = '\0';
            nrbyStps_scrollingStillRequired = true;
            
            if (nrbyStps_scrollIndex == index) {
                light_enable_interaction();
            }
        }
        menu_cell_basic_draw(ctx, cell_layer, nrbyStps_scrollTmp, busStops[index].stopDetails, NULL);
    } else {
        menu_cell_basic_draw(ctx, cell_layer, busStops[index].stopDescription, busStops[index].stopDetails, NULL);
    }
}

// Menu Item Selection
// Last index item: View more bus stops
// Other items: request data using the stop details which is selected
// Stop description is retrieved to be used for the busArrivalsWindow showing bus services
static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    int index = cell_index->row;
    if (strcmp(busStops[index].stopDescription, "DISPLAY MORE") == 0) {
        requestData("more");
    } else {
        snprintf(lastStopRequest, sizeof(lastStopRequest), "%s", busStops[index].stopDetails);
        snprintf(lastStopDescription, sizeof(lastStopDescription), "%s", busStops[index].stopDescription);
        // APP_LOG(// APP_LOG_LEVEL_DEBUG, "Selected: %s", stopDetails[index]);

        window_stack_push(s_splashWindow, true);
        text_layer_set_text(s_splashText, "Getting estimated bus arrival timings...");

        requestData(lastStopRequest);
    }
}

static void menu_selection_changed_callback(MenuLayer *menu_layer, MenuIndex new_index,
                                            MenuIndex old_index, void *callback_context) {
    nrbyStps_scrollIndex = new_index.row;
    if (!nrbyStps_reloadingToScroll) {
        initiateScrollTimer();
    } else {
        nrbyStps_reloadingToScroll = false;
    }
}

static void nearbyStopsWindow_load(Window *window) {
    Layer *windowLayer = window_get_root_layer(window);
    GRect bounds = layer_get_frame(windowLayer);

    #ifdef PBL_SDK_3
    bounds = GRect(0, 16, 144, 168-16);
    #endif

    s_menuLayer = menu_layer_create(bounds);
    s_scrollLayer = menu_layer_get_scroll_layer(s_menuLayer);
    scroll_layer_set_shadow_hidden(s_scrollLayer, false);

    menu_layer_set_callbacks(s_menuLayer, NULL, (MenuLayerCallbacks) {
        .get_num_sections = menu_get_num_sections_callback,
        .get_num_rows = menu_get_num_rows_callback,
        .get_header_height = menu_get_header_height_callback,
        .draw_header = menu_draw_header_callback,
        .draw_row = menu_draw_row_callback,
        .select_click = menu_select_callback,
        .selection_changed = menu_selection_changed_callback,
    });

    // Bind the menu layer's click config provider to the window for interactivity
    menu_layer_set_click_config_onto_window(s_menuLayer, window);

    layer_add_child(windowLayer, menu_layer_get_layer(s_menuLayer));

    initiateScrollTimer();

    #ifdef PBL_SDK_3
    s_statusBar = status_bar_layer_create();
    layer_add_child(windowLayer, status_bar_layer_get_layer(s_statusBar));
    s_battery = layer_create(GRect(bounds.origin.x, bounds.origin.y-16, bounds.size.w, STATUS_BAR_LAYER_HEIGHT));
    layer_set_update_proc(s_battery, battery_proc);
    layer_add_child(windowLayer, s_battery);
    #endif
}

static void nearbyStopsWindow_unload(Window *window) {
    menu_layer_destroy(s_menuLayer);
    #ifdef PBL_SDK_3
    status_bar_layer_destroy(s_statusBar);
    layer_destroy(s_battery);
    #endif
}

static uint16_t menu_get_num_sections_callback2(MenuLayer *menu_layer, void *data) {
    return 1;
}

static uint16_t menu_get_num_rows_callback2(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    return sizeBusServices;
}

static int16_t menu_get_header_height_callback2(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void menu_draw_header_callback2(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
    menu_cell_basic_header_draw(ctx, cell_layer, lastStopDescription);
}

static void menu_draw_row_callback2(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    int index = cell_index->row;
    menu_cell_basic_draw(ctx, cell_layer, busArrivals[index].service, busArrivals[index].arrivals, NULL);
}

// Menu Item Selection
// This will retrieve the selection index and request new bus arrival data
// Update the user current choice view settings
static void menu_select_callback2(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    // APP_LOG(// APP_LOG_LEVEL_DEBUG, "ETA manual refresh requested.");
    int index = cell_index->row;
    snprintf(lastServiceRequest, sizeof(lastServiceRequest), "%s", busArrivals[index].service);
    
    window_stack_push(s_splashWindow, true);
    text_layer_set_text(s_splashText, "Getting bus route...");
    
    requestData(lastServiceRequest);
}

static void arrivingAlert(char *arrival) {    
    if ('1' == arrival[0] && ' ' == arrival[1]) {
        vibes_short_pulse();
    } else if ('A' == arrival[0]) {
        vibes_long_pulse();
    }
}

static void busArrivalsWindow_load(Window *window) {
    Layer *windowLayer = window_get_root_layer(window);
    GRect bounds = layer_get_frame(windowLayer);

    #ifdef PBL_SDK_3
    bounds = GRect(0, 16, 144, 168-16);
    #endif

    s_menuLayer2 = menu_layer_create(bounds);
    s_scrollLayer2 = menu_layer_get_scroll_layer(s_menuLayer2);
    scroll_layer_set_shadow_hidden(s_scrollLayer2, false);

    menu_layer_set_callbacks(s_menuLayer2, NULL, (MenuLayerCallbacks) {
        .get_num_sections = menu_get_num_sections_callback2,
        .get_num_rows = menu_get_num_rows_callback2,
        .get_header_height = menu_get_header_height_callback2,
        .draw_header = menu_draw_header_callback2,
        .draw_row = menu_draw_row_callback2,
        .select_click = menu_select_callback2,
    });

    // Bind the menu layer's click config provider to the window for interactivity
    menu_layer_set_click_config_onto_window(s_menuLayer2, window);

    layer_add_child(windowLayer, menu_layer_get_layer(s_menuLayer2));
    
    #ifdef PBL_SDK_3
    s_statusBar2 = status_bar_layer_create();
    layer_add_child(windowLayer, status_bar_layer_get_layer(s_statusBar2));
    s_battery2 = layer_create(GRect(bounds.origin.x, bounds.origin.y-16, bounds.size.w, STATUS_BAR_LAYER_HEIGHT));
    layer_set_update_proc(s_battery2, battery_proc);
    layer_add_child(windowLayer, s_battery2);
    #endif
}

static void busArrivalsWindow_unload(Window *window) {
    menu_layer_destroy(s_menuLayer2);

    #ifdef PBL_SDK_3
    status_bar_layer_destroy(s_statusBar2);
    layer_destroy(s_battery2);
    #endif
}

static void failureRecall(void) {
    if (window_stack_get_top_window() == s_splashWindow && !window_stack_contains_window(s_nearbyStopsWindow)) {
        // APP_LOG(// APP_LOG_LEVEL_DEBUG, "Failed to receive any data response.");
        requestData("location");
        initalFailure = app_timer_register(2000, (AppTimerCallback)failureRecall, NULL);
    }
}

static void splashWindow_load(Window *window) {
    Layer *windowLayer = window_get_root_layer(window);
//     GRect bounds = layer_get_frame(windowLayer);
    GRect bounds = GRect(1, 0, 142, 168);
    
    #ifdef PBL_SDK_3
    bounds = GRect(1, 16, 142, 168-16);
    #endif

    s_splashText = text_layer_create(bounds);
    text_layer_set_background_color(s_splashText, GColorWhite);
    text_layer_set_text_color(s_splashText, GColorBlack);
    text_layer_set_text_alignment(s_splashText, GTextAlignmentCenter);
    text_layer_set_overflow_mode(s_splashText, GTextOverflowModeWordWrap);
    text_layer_set_font(s_splashText, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
    text_layer_set_text(s_splashText, "Searching for bus stops nearby...");
    layer_add_child(windowLayer, text_layer_get_layer(s_splashText));

    #ifdef PBL_SDK_3
    s_statusBar3 = status_bar_layer_create();
    layer_add_child(windowLayer, status_bar_layer_get_layer(s_statusBar3));
    s_battery3 = layer_create(GRect(bounds.origin.x, bounds.origin.y-16, bounds.size.w, STATUS_BAR_LAYER_HEIGHT));
    layer_set_update_proc(s_battery3, battery_proc);
    layer_add_child(windowLayer, s_battery3);
    #endif

    if (strcmp(destination, "NULL") == 0) {
        light_enable_interaction();        
    }
    initalFailure = app_timer_register(2000, (AppTimerCallback)failureRecall, NULL);
}

static void splashWindow_unload(Window *window) {
    text_layer_destroy(s_splashText);

    #ifdef PBL_SDK_3
    status_bar_layer_destroy(s_statusBar3);
    layer_destroy(s_battery3);
    #endif
}

static uint16_t menu_get_num_sections_callback4(MenuLayer *menu_layer, void *data) {
    return 1;
}

static uint16_t menu_get_num_rows_callback4(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    return sizeBusRoute;
}

static int16_t menu_get_header_height_callback4(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void menu_draw_header_callback4(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
    menu_cell_basic_header_draw(ctx, cell_layer, lastServiceRequest);
}

static void scroll_menu_callback4(void) {
    route_scrollTimer = NULL;
    route_scrollOffset++;
    if (!route_scrollingStillRequired) {
        route_scrollOffset = -1;
        route_scrollingStillRequired = true;
        route_scrollTimer = app_timer_register(SCROLL_WAIT, (AppTimerCallback)scroll_menu_callback4, NULL);
        return;
    }

    MenuIndex index = menu_layer_get_selected_index(s_menuLayer4);
    if (index.row != 0) {
        route_reloadingToScroll = true;
    }
    route_scrollingStillRequired = false;
    menu_layer_reload_data(s_menuLayer4);
    route_scrollTimer = app_timer_register(SCROLL_WAIT, (AppTimerCallback)scroll_menu_callback4, NULL);
}

static void initiateScrollTimer4(void) {
    bool needToCreateTimer = true;
    route_scrollingStillRequired = true;
    route_scrollOffset = 0;
    route_reloadingToScroll = false;

    if (route_scrollTimer) {
        needToCreateTimer = !app_timer_reschedule(route_scrollTimer, SCROLL_WAIT);
    }

    if (needToCreateTimer) {
        route_scrollTimer = app_timer_register(SCROLL_WAIT, (AppTimerCallback)scroll_menu_callback4, NULL);
    }
}

static void menu_draw_row_callback4(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    int index = cell_index->row;

    if (route_scrollIndex == index) {
        if (route_scrollOffset == 0) {
            strcpy(route_scrollTmp, busRoute[index]);
        }

        int txtWidth = textWidth(route_scrollTmp);
        if (txtWidth > MAX_VISIBLE) {
            int charIndex = 0;
            int maxLen = strlen(busRoute[index]);
            for (int i=route_scrollOffset; i<maxLen; i++) {
                route_scrollTmp[charIndex++] = busRoute[index][i];
            }
            route_scrollTmp[charIndex] = '\0';
            route_scrollingStillRequired = true;
            
            if (route_scrollIndex == index) {
                light_enable_interaction();
            }
        }
        menu_cell_basic_draw(ctx, cell_layer, route_scrollTmp, NULL, NULL);
    } else {
        menu_cell_basic_draw(ctx, cell_layer, busRoute[index], NULL, NULL);
    }
}

// Menu Item Selection
// Last index item: View more bus stops
// Other items: request data using the stop details which is selected
// Stop description is retrieved to be used for the busArrivalsWindow showing bus services
static void menu_select_callback4(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    int index = cell_index->row;
    
    if (strcmp(busRoute[index], "NO ROUTE FOUND") == 0) {
        return;
    }

    snprintf(destination, sizeof(destination), "Distance / %d", index);
    
    window_stack_push(s_splashWindow, true);
    text_layer_set_text(s_splashText, "Calculating distance...");
    
    requestData(destination);
}

static void menu_selection_changed_callback4(MenuLayer *menu_layer, MenuIndex new_index,
                                            MenuIndex old_index, void *callback_context) {
    route_scrollIndex = new_index.row;
    if (!route_reloadingToScroll) {
        initiateScrollTimer4();
    } else {
        route_reloadingToScroll = false;
    }
}

static void routeWindow_load(Window *window) {
    Layer *windowLayer = window_get_root_layer(window);
    GRect bounds = layer_get_frame(windowLayer);

    #ifdef PBL_SDK_3
    bounds = GRect(0, 16, 144, 168-16);
    #endif
        
    s_menuLayer4 = menu_layer_create(bounds);
    s_scrollLayer4 = menu_layer_get_scroll_layer(s_menuLayer4);
    scroll_layer_set_shadow_hidden(s_scrollLayer4, false);
    
    menu_layer_set_callbacks(s_menuLayer4, NULL, (MenuLayerCallbacks) {
        .get_num_sections = menu_get_num_sections_callback4,
        .get_num_rows = menu_get_num_rows_callback4,
        .get_header_height = menu_get_header_height_callback4,
        .draw_header = menu_draw_header_callback4,
        .draw_row = menu_draw_row_callback4,
        .select_click = menu_select_callback4,
        .selection_changed = menu_selection_changed_callback4,
    });
    
    // Bind the menu layer's click config provider to the window for interactivity
    menu_layer_set_click_config_onto_window(s_menuLayer4, window);

    layer_add_child(windowLayer, menu_layer_get_layer(s_menuLayer4));    
    
    initiateScrollTimer4();
        
    #ifdef PBL_SDK_3
    s_statusBar4 = status_bar_layer_create();
    layer_add_child(windowLayer, status_bar_layer_get_layer(s_statusBar4));
    s_battery4 = layer_create(GRect(bounds.origin.x, bounds.origin.y-16, bounds.size.w, STATUS_BAR_LAYER_HEIGHT));
    layer_set_update_proc(s_battery4, battery_proc);
    layer_add_child(windowLayer, s_battery4);
    #endif
}

static void routeWindow_unload(Window *window) {
    menu_layer_destroy(s_menuLayer4);
    #ifdef PBL_SDK_3
    status_bar_layer_destroy(s_statusBar4);
    layer_destroy(s_battery4);
    #endif    
}

static void alertCallback(void) {
    wakeAlertTimer = NULL;
    if (window_stack_get_top_window() == s_splashWindow && strcmp(destination, "NULL") != 0) {
        vibes_long_pulse();
        wakeAlertTimer = app_timer_register(ALERT_INTERVAL, (AppTimerCallback) alertCallback, NULL);
    }
}

// Data Received
static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
    // Check for key containing number of bus stops
    Tuple *t = dict_find(iterator, KEY_BUS_STOPS);

    // Key containing number of bus stops exists
    if (t) {
        // Store number of bus stops
        sizeBusStops = t->value->int32;

        // Retrieve data from the corresponding keys for each bus stop
        for (int i=0; i<sizeBusStops; i++) {
            t = dict_find(iterator, i+INDEX_DESCRIPTION);
            snprintf(busStops[i].stopDescription, sizeof(busStops[i].stopDescription), "%s", t->value->cstring);
            t = dict_find(iterator, i+INDEX_DETAILS);
            snprintf(busStops[i].stopDetails, sizeof(busStops[i].stopDetails), "%s", t->value->cstring);
            // APP_LOG(// APP_LOG_LEVEL_DEBUG, "%s | %s", busStops[i].stopDescription, busStops[i].stopDetails);
        }
        
        MenuIndex chosen = MenuIndex(0, 0);
        GPoint point = GPoint(0, 0);
        
        // Check if it is initial run 
        if (window_stack_get_top_window() == s_splashWindow && !window_stack_contains_window(s_nearbyStopsWindow)) {
            window_stack_pop_all(false);
            window_stack_push(s_nearbyStopsWindow, true);
        } else if (window_stack_get_top_window() == s_splashWindow && window_stack_contains_window(s_routeWindow)) {
            requestData(lastStopRequest);
        }

        if (window_stack_get_top_window() == s_nearbyStopsWindow) {
            chosen = menu_layer_get_selected_index(s_menuLayer);
            point = scroll_layer_get_content_offset(s_scrollLayer);
            menu_layer_reload_data(s_menuLayer);
            menu_layer_set_selected_index(s_menuLayer, chosen, MenuRowAlignNone, false);
            scroll_layer_set_content_offset(s_scrollLayer, point, false);
        }

        // Light it up!
        light_enable_interaction();
    }

    // Retrieve key containing the number of bus services
    t = dict_find(iterator, KEY_BUS_SERVICES);
    if (t) {
        // Store number of bus services
        sizeBusServices = t->value->int32;

        // Retrieve data from the corresponding keys for each bus service
        for (int i=0; i<sizeBusServices; i++) {
            t = dict_find(iterator, i+INDEX_SERVICE);
            snprintf(busArrivals[i].service, sizeof(busArrivals[i].service), "%s", t->value->cstring);
            t = dict_find(iterator, i+INDEX_ARRIVAL);
            snprintf(busArrivals[i].arrivals, sizeof(busArrivals[i].arrivals), "%s", t->value->cstring);
            // APP_LOG(// APP_LOG_LEVEL_DEBUG, "%s: %s", busArrivals[i].service, busArrivals[i].arrivals);
        }
        
        MenuIndex chosen = MenuIndex(0, 0);
        GPoint point = GPoint(0, 0);

        if (window_stack_get_top_window() == s_splashWindow && !window_stack_contains_window(s_busArrivalsWindow)) {
            window_stack_pop(false);
        } else if (window_stack_get_top_window() == s_splashWindow && window_stack_contains_window(s_routeWindow)) {
            requestData(lastServiceRequest);
        }
        
        if (window_stack_get_top_window() == s_nearbyStopsWindow) {
            window_stack_push(s_busArrivalsWindow, true);
        } else if (window_stack_get_top_window() == s_busArrivalsWindow) {
            chosen = menu_layer_get_selected_index(s_menuLayer2);
            point = scroll_layer_get_content_offset(s_scrollLayer2);
            arrivingAlert(busArrivals[chosen.row].arrivals);
        }
        
        if (window_stack_get_top_window() == s_busArrivalsWindow) {
            menu_layer_reload_data(s_menuLayer2);
            menu_layer_set_selected_index(s_menuLayer2, chosen, MenuRowAlignNone, false);
            scroll_layer_set_content_offset(s_scrollLayer2, point, false);
        }
                
        // Light it up!
        light_enable_interaction();
    }
    
    // Retrieve key containing the number of bus stops in route
    t = dict_find(iterator, KEY_BUS_ROUTE);
    if (t) {
        // Store number of bus stops in route
        sizeBusRoute = t->value->int32;
        
        // Retrieve data from the corresponding keys for each bus stop
        for (int i=0; i<sizeBusRoute; i++) {
            t = dict_find(iterator, i+INDEX_DESCRIPTION);
            snprintf(busRoute[i], sizeof(busRoute[i]), "%s", t->value->cstring);
        }

        if (window_stack_get_top_window() == s_splashWindow && !window_stack_contains_window(s_routeWindow)) {
            window_stack_pop(false);
        } else if (window_stack_get_top_window() == s_splashWindow && window_stack_contains_window(s_routeWindow)) {
            requestData(destination);
        }
        
        if (window_stack_get_top_window() == s_busArrivalsWindow) {
            window_stack_push(s_routeWindow, true);
        }
        
        if (window_stack_get_top_window() == s_routeWindow) {
            menu_layer_reload_data(s_menuLayer4);
        }
           
        // Light it up!
        light_enable_interaction();
    }
    
    t = dict_find(iterator, KEY_WAKE_BOOL);
    if (t && window_stack_get_top_window() == s_splashWindow && strcmp(destination, "NULL") != 0) {
        int toWake = t->value->int32;
        t = dict_find(iterator, KEY_WAKE_SUMMARY);
        snprintf(destinationSummary, sizeof(destinationSummary), "%s", t->value->cstring);
        text_layer_set_text(s_splashText, destinationSummary);
        
        // Vibration Alert
        if (toWake) {
            vibes_long_pulse();
            bool createTimer = true;
            if (wakeAlertTimer) {
                createTimer = !app_timer_reschedule(wakeAlertTimer, ALERT_INTERVAL);
            }
            if (createTimer) {
                wakeAlertTimer = app_timer_register(ALERT_INTERVAL, (AppTimerCallback) alertCallback, NULL);    
            }
        }
    } else if (t && window_stack_get_top_window() != s_splashWindow) {
        requestData("Distance / null");
        snprintf(destination, sizeof(destination), "NULL");
    }
}

// APPLogs
char *translate_error(AppMessageResult result) {
    switch (result) {
        case APP_MSG_OK: return "APP_MSG_OK";
        case APP_MSG_SEND_TIMEOUT: return "APP_MSG_SEND_TIMEOUT";
        case APP_MSG_SEND_REJECTED: return "APP_MSG_SEND_REJECTED";
        case APP_MSG_NOT_CONNECTED: return "APP_MSG_NOT_CONNECTED";
        case APP_MSG_APP_NOT_RUNNING: return "APP_MSG_APP_NOT_RUNNING";
        case APP_MSG_INVALID_ARGS: return "APP_MSG_INVALID_ARGS";
        case APP_MSG_BUSY: return "APP_MSG_BUSY";
        case APP_MSG_BUFFER_OVERFLOW: return "APP_MSG_BUFFER_OVERFLOW";
        case APP_MSG_ALREADY_RELEASED: return "APP_MSG_ALREADY_RELEASED";
        case APP_MSG_CALLBACK_ALREADY_REGISTERED: return "APP_MSG_CALLBACK_ALREADY_REGISTERED";
        case APP_MSG_CALLBACK_NOT_REGISTERED: return "APP_MSG_CALLBACK_NOT_REGISTERED";
        case APP_MSG_OUT_OF_MEMORY: return "APP_MSG_OUT_OF_MEMORY";
        case APP_MSG_CLOSED: return "APP_MSG_CLOSED";
        case APP_MSG_INTERNAL_ERROR: return "APP_MSG_INTERNAL_ERROR";
        default: return "UNKNOWN ERROR";
    }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
	// APP_LOG(// APP_LOG_LEVEL_ERROR, "Message dropped!");
    // APP_LOG(// APP_LOG_LEVEL_DEBUG, "In dropped: %i - %s", reason, translate_error(reason));
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
	// APP_LOG(// APP_LOG_LEVEL_ERROR, "Outbox send failed!");
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
	// APP_LOG(// APP_LOG_LEVEL_INFO, "Outbox send success!");
}

// Primary Structure
static void init() {
    // create Window element and assign to pointer
    s_nearbyStopsWindow = window_create();
    s_busArrivalsWindow = window_create();
    s_splashWindow = window_create();
    s_routeWindow = window_create();

    // set handlers to manage the elements inside the Window
    window_set_window_handlers(s_nearbyStopsWindow, (WindowHandlers) {
        .load = nearbyStopsWindow_load,
        .unload = nearbyStopsWindow_unload
    });

    window_set_window_handlers(s_busArrivalsWindow, (WindowHandlers) {
        .load = busArrivalsWindow_load,
        .unload = busArrivalsWindow_unload
    });

    window_set_window_handlers(s_splashWindow, (WindowHandlers) {
        .load = splashWindow_load,
        .unload = splashWindow_unload
    });

    window_set_window_handlers(s_routeWindow, (WindowHandlers) {
        .load = routeWindow_load,
        .unload = routeWindow_unload
    });
    
    window_stack_push(s_splashWindow, true);

    // register with TickTimerService
    tick_timer_service_subscribe(MINUTE_UNIT, tickHandler);

    // register with AccelerometerService
    accel_tap_service_subscribe(accelerometerHandler);

    #ifdef PBL_SDK_3
    // register with BatteryStateService
    battery_state_service_subscribe(batteryHandler);
    #endif
        
    // Register callbacks
    app_message_register_inbox_received(inbox_received_callback);
    app_message_register_inbox_dropped(inbox_dropped_callback);
    app_message_register_outbox_failed(outbox_failed_callback);
    app_message_register_outbox_sent(outbox_sent_callback);

    // Open AppMessage
    app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
}

// Primary Structure
static void deinit() {
    window_destroy(s_nearbyStopsWindow);
    window_destroy(s_busArrivalsWindow);
    window_destroy(s_splashWindow);
    window_destroy(s_routeWindow);
}

// Primary Structure
int main(void) {
    init();
    app_event_loop();
    deinit();
}
