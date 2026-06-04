#define _USE_MATH_DEFINES // 一部の環境でM_PIを有効化するために必須
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/endpoints.h> // HIDレポート送信用に追加
#include <dt-bindings/zmk/keys.h>

// M_PI が math.h から取得できなかった場合の安全なフォールバック
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DT_DRV_COMPAT zmk_input_processor_toucan_circle

// 40mm CirqueのPinnacle最大解像度範囲（通常0〜2047、中心1024）
#define TRACKPAD_CENTER_X 1024
#define TRACKPAD_CENTER_Y 1024

// 辺縁2.5mmに相当するカウント閾値（半径約896カウント以上を外周とする）
#define RIM_THRESHOLD_RADIUS 896 

// 角度のラジアンから度への変換
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

// ZMKの正しいHID構造体を叩いてキーを擬似的に送出するヘルパー関数
static void send_key(zmk_key_t key, bool press) {
    if (press) {
        zmk_hid_press_key(key);
    } else {
        zmk_hid_release_key(key);
    }
    // 変更されたHIDレポートを実際にPCへ送信する
    zmk_endpoints_send_report(zmk_hid_get_profile());
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

        // 中心からの相対座標を算出
        int32_t dx = data->last_x - TRACKPAD_CENTER_X;
        int32_t dy = data->last_y - TRACKPAD_CENTER_Y;

        // 三平方の定理で半径の距離を確認
        uint32_t current_r = (uint32_t)(dx * dx + dy * dy);
        
        // 判定高速化のため、閾値を二乗してルート(sqrt)の計算を回避
        if (current_r < (RIM_THRESHOLD_RADIUS * RIM_THRESHOLD_RADIUS)) {
            data->is_first_touch = true;
            data->accumulated_angle = 0.0;
            return; // 内周なら何もせず後続プロセッサに流す
        }

        // 角度θを算出（結果は -π 〜 +π）
        // Zephyrのツールチェーンで競合を避けるため、直接計算ロジックを展開、または組込み関数を使用
        // ここでは一般的な近似、または単純なatan2を使用。標準Cとして処理
        double current_angle = atan2((double)dy, (double)dx);
        if (current_angle < 0) current_angle += 2 * M_PI;

        if (data->is_first_touch) {
            data->last_angle = current_angle;
            data->is_first_touch = false;
            return;
        }

        // 角度の変化量（dθ）を計算
        double d_angle = current_angle - data->last_angle;
        if (d_angle > M_PI) d_angle -= 2 * M_PI;
        else if (d_angle < -M_PI) d_angle += 2 * M_PI;

        data->accumulated_angle += RAD_TO_DEG(d_angle);
        data->last_angle = current_angle;

        // 現在Fnレイヤーがアクティブかどうかを取得
        bool fn_active = zmk_keymap_layer_active(config->fn_layer_index);

        // 指が現在の「上半周」か「下半周」かを判定
        bool is_upper_half = (current_angle >= 0 && current_angle < M_PI);

        // ジェスチャー判定
        if (fn_active) {
            // 【Fnホールド状態】1周（360度）回転
            if (data->accumulated_angle >= 360.0) {
                // 時計回り：ピンチアウト (Ctrl + Wheel Up)
                send_key(KC_LCTRL, true);
                zmk_hid_mouse_scroll_up();
                zmk_endpoints_send_report(zmk_hid_get_profile());
                send_key(KC_LCTRL, false);
                data->accumulated_angle = 0.0;
            } else if (data->accumulated_angle <= -360.0) {
                // 反時計回り：ピンチイン (Ctrl + Wheel Down)
                send_key(KC_LCTRL, true);
                zmk_hid_mouse_scroll_down();
                zmk_endpoints_send_report(zmk_hid_get_profile());
                send_key(KC_LCTRL, false);
                data->accumulated_angle = 0.0;
            }
        } else {
            // 【通常状態】
            if (is_upper_half) {
                // 上半周：音量調整（閾値30度）
                if (data->accumulated_angle >= 30.0) {
                    send_key(KC_C_VOL_UP, true);
                    send_key(KC_C_VOL_UP, false);
                    data->accumulated_angle = 0.0;
                } else if (data->accumulated_angle <= -30.0) {
                    send_key(KC_C_VOL_DN, true);
                    send_key(KC_C_VOL_DN, false);
                    data->accumulated_angle = 0.0;
                }
            } else {
                // 下半周：ページ進み・戻り（閾値60度）
                if (data->accumulated_angle >= 60.0) {
                    // 時計回り：1ページ戻り（Alt + Left）
                    send_key(KC_LALT, true);
                    send_key(KC_LEFT, true);
                    send_key(KC_LEFT, false);
                    send_key(KC_LALT, false);
                    data->accumulated_angle = 0.0;
                } else if (data->accumulated_angle <= -60.0) {
                    // 反時計回り：1ページ送り（Alt + Right）
                    send_key(KC_LALT, true);
                    send_key(KC_RIGHT, true);
                    send_key(KC_RIGHT, false);
                    send_key(KC_LALT, false);
                    data->accumulated_angle = 0.0;
                }
            }
        }
        
        // イベントをダミー化して通常のマウス移動信号を完全に消去
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

