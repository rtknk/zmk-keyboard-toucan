#define _USE_MATH_DEFINES
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <math.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/endpoints.h>
#include <dt-bindings/zmk/keys.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DT_DRV_COMPAT zmk_input_processor_toucan_circle

// 40mm CirqueのPinnacle最大解像度範囲（中心1024）
#define TRACKPAD_CENTER_X 1024
#define TRACKPAD_CENTER_Y 1024
#define RIM_THRESHOLD_RADIUS 896 

#define RAD_TO_DEG(r) ((r) * 180.0 / M_PI)

struct ip_toucan_circle_config {
    uint8_t fn_layer_index;
};

struct ip_toucan_circle_data {
    int16_t last_x;
    int16_t last_y;
    double last_angle;
    double accumulated_angle;
    bool is_first_touch;
};

// ZMKの正しい引数1つ（Usage ID）の型に修正
static void send_key(uint32_t usage_id, bool press) {
    if (press) {
        zmk_hid_keyboard_press(usage_id);
    } else {
        zmk_hid_keyboard_release(usage_id);
    }
    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
}

static void process_toucan_circle(const struct device *dev, struct input_event *evt) {
    const struct ip_toucan_circle_config *config = dev->config;
    struct ip_toucan_circle_data *data = dev->data;

    // タッチが離れた（Touch Up）イベントの検知
    if (evt->type == INPUT_EV_KEY && evt->code == INPUT_BTN_TOUCH && evt->value == 0) {
        data->is_first_touch = true;
        data->accumulated_angle = 0.0;
        return;
    }

    // 座標イベントの追跡
    if (evt->type == INPUT_EV_ABS) {
        if (evt->code == INPUT_ABS_X) data->last_x = evt->value;
        if (evt->code == INPUT_ABS_Y) data->last_y = evt->value;

        int32_t dx = data->last_x - TRACKPAD_CENTER_X;
        int32_t dy = data->last_y - TRACKPAD_CENTER_Y;

        uint32_t current_r = (uint32_t)(dx * dx + dy * dy);
        
        // 辺縁エリア判定
        if (current_r < (RIM_THRESHOLD_RADIUS * RIM_THRESHOLD_RADIUS)) {
            data->is_first_touch = true;
            data->accumulated_angle = 0.0;
            return; 
        }

        double current_angle = atan2((double)dy, (double)dx);
        if (current_angle < 0) current_angle += 2 * M_PI;

        if (data->is_first_touch) {
            data->last_angle = current_angle;
            data->is_first_touch = false;
            return;
        }

        double d_angle = current_angle - data->last_angle;
        if (d_angle > M_PI) d_angle -= 2 * M_PI;
        else if (d_angle < -M_PI) d_angle += 2 * M_PI;

        data->accumulated_angle += RAD_TO_DEG(d_angle);
        data->last_angle = current_angle;

        bool fn_active = zmk_keymap_layer_active(config->fn_layer_index);
        bool is_upper_half = (current_angle >= 0 && current_angle < M_PI);

        // -----------------------------------------------------------------
        // ジェスチャー判定
        // 各キーコードの引数を ZMKコアが想定する単一のキーUsage（1ポート）に修正
        // 左Ctrl=0xE0, 左Alt=0xE2, Left=0x50, Right=0x4F
        // -----------------------------------------------------------------
        if (fn_active) {
            // 【Fnホールド状態】1周（360度）回転
            if (data->accumulated_angle >= 360.0) {
                // 時計回り：ピンチアウト (Ctrl + Wheel Up)
                zmk_hid_keyboard_press(0xE0); // 左Ctrl
                zmk_hid_mouse_scroll_up();
                zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
                zmk_hid_keyboard_release(0xE0);
                data->accumulated_angle = 0.0;
            } else if (data->accumulated_angle <= -360.0) {
                // 反時計回り：ピンチイン (Ctrl + Wheel Down)
                zmk_hid_keyboard_press(0xE0); // 左Ctrl
                zmk_hid_mouse_scroll_down();
                zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
                zmk_hid_keyboard_release(0xE0);
                data->accumulated_angle = 0.0;
            }
        } else {
            // 【通常状態】
            if (is_upper_half) {
                // 上半周：音量調整（閾値30度）
                if (data->accumulated_angle >= 30.0) {
                    // 音量1段階UP
                    zmk_hid_consumer_press(0x00E9); 
                    zmk_hid_consumer_release(0x00E9);
                    zmk_endpoints_send_report(HID_USAGE_GD_CONSUMER);
                    data->accumulated_angle = 0.0;
                } else if (data->accumulated_angle <= -30.0) {
                    // 音量1段階DOWN
                    zmk_hid_consumer_press(0x00EA); 
                    zmk_hid_consumer_release(0x00EA);
                    zmk_endpoints_send_report(HID_USAGE_GD_CONSUMER);
                    data->accumulated_angle = 0.0;
                }
            } else {
                // 下半周：ページ進み・戻り（閾値60度）
                if (data->accumulated_angle >= 60.0) {
                    // 時計回り：1ページ戻り（Alt + Left）
                    zmk_hid_keyboard_press(0xE2);   // 左Alt
                    zmk_hid_keyboard_press(0x50);   // Left Arrow
                    zmk_hid_keyboard_release(0x50);
                    zmk_hid_keyboard_release(0xE2);
                    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
                    data->accumulated_angle = 0.0;
                } else if (data->accumulated_angle <= -60.0) {
                    // 反時計回り：1ページ送り（Alt + Right）
                    zmk_hid_keyboard_press(0xE2);   // 左Alt
                    zmk_hid_keyboard_press(0x4F);   // Right Arrow
                    zmk_hid_keyboard_release(0x4F);
                    zmk_hid_keyboard_release(0xE2);
                    zmk_endpoints_send_report(HID_USAGE_GD_KEYBOARD);
                    data->accumulated_angle = 0.0;
                }
            }
        }
        
        evt->type = INPUT_EV_DUMMY; 
    }
}

static int ip_toucan_circle_init(const struct device *dev) { return 0; }

#define INST_IP_TOUCAN_CIRCLE(n)                                              \
    static struct ip_toucan_circle_data ip_toucan_circle_data_##n = {         \
        .is_first_touch = true,                                               \
        .accumulated_angle = 0.0,                                             \
    };                                                                        \
    static const struct ip_toucan_circle_config ip_toucan_circle_config_##n = { \
        .fn_layer_index = DT_INST_PROP(n, fn_layer_index),                    \
    };                                                                        \
    INPUT_PROCESSOR_DEFINE(DT_DRV_INST(n), process_toucan_circle,             \
                           &ip_toucan_circle_data_##n,                        \
                           &ip_toucan_circle_config_##n,                      \
                           ip_toucan_circle_init, POST_KERNEL,                \
                           CONFIG_APPLICATION_INIT_PRIORITY);

DT_INST_FOREACH_STATUS_OKAY(INST_IP_TOUCAN_CIRCLE)
