#include <string.h>
#include <Arduino.h>
#include <SEGGER_SYSVIEW.h>
#include <Adafruit_USBD_CDC.h>
#include <Adafruit_USBD_HID.h>
#include <SEGGER_RTT.h>
#include "RotaryEncoder.h"

#include "mouse_sensor"
#include "hid_3dx.h"
#include "gyro.h"

#include "bipbuf.hpp"

// HID report descriptor using TinyUSB's template
// Generic In Out with 64 bytes report (max)
uint8_t const desc_hid_report[] =
        {
                TUD_HID_REPORT_DESC_KUSTOM( )
        };

//Encoder myEnc(PIN_D19, PIN_D20);

// USB HID object
Adafruit_USBD_HID usb_hid;

#define STACK_SZ       (256*4)

bipbuf_t BipBuf_ball;

struct ball_data_t {
    int16_t cx, cy, cz;
    Quaternion q;
    uint32_t ts;
};

static TaskHandle_t  _bsensHandle;

[[noreturn]] static void ball_task(void* arg) {
    (void) arg;

    int8_t m1x, m1y, m2x, m2y;
    uint8_t stat1, stat2, qua1, qua2;

    bipbuf_init(&BipBuf_ball, BIPBUF_SIZE);

    while(true) {
//        yield();

        read_paw3204_status(DEV1, &stat1);
        if (stat1 & 0x80) {
            read_paw3204_data(DEV1, &qua1, &m1x, &m1y);
        } else {
            m1x = 0;
            m1y = 0;
        }


        read_paw3204_status(DEV2, &stat2);
        if (stat2 & 0x80) {
            read_paw3204_data(DEV2, &qua2, &m2x, &m2y);
        } else {
            m2x = 0;
            m2y = 0;
        }

        if (stat1 & 0x80 || stat2 & 0x80) {
            // второй сенсор повёрнут относительно первого на 180 градусов
            m1x = -m1x;
            m1y = -m1y;

// делаем вообще по-другому. теперь x/y будет отдаваться одним сенсором, полностью. а z - другим. благо они под 90 градусов стоят

            const float cx = m1x;
            const float cy = m1y;
            const float cz = m2x;



            auto pkt = (ball_data_t *) (bipbuf_request(&BipBuf_ball, sizeof(ball_data_t)));
            if (pkt != nullptr) {
                pkt->cx = cx;
                pkt->cy = cy;
                pkt->cz = cz;
                pkt->ts = millis();
//                pkt->q = Quaternion(w, ha.x, ha.y, ha.z);
                bipbuf_push(&BipBuf_ball, sizeof(ball_data_t));
            } else {
                printf("OVERFLOW!!!!\n");
            }
        }

        delay(5);
    }

//    hwReadConfigBlock

}


#ifdef USE_GYRO

static TaskHandle_t  _gyroHandle;

[[noreturn]] static void gyro_task(void* arg) {
    (void) arg;

    Gyro.init();
//    Gyro.enableSendRawGyro(true);

    while(true) {
        Gyro.update_from_mpu();
//        yield();
        delay(5);
    }

}

#endif


#define PIN_ENC_BUTTON PIN_D21

uint8_t prev_btn;

void setup()
{
// board_config.update("build.hwids", [
//    ["0x046D",  "0xC62b" ]
//])
//
//board_config.update("vendor", "Naka")
//board_config.update("build.usb_product", "SpaceMouse PRO")

    usb_hid.enableOutEndpoint(true);
    usb_hid.setPollInterval(1);
    usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
    usb_hid.setReportCallback(get_report_callback, set_report_callback);

    usb_hid.begin();

    Serial.begin(115200);
    printf("nrf52 mouse test\n");
    printf("---------------------------\n");

    bipbuf_init(&BipBuf_hid_mirror, BIPBUF_SIZE);

    init_paw3204(DEV1);
    if( read_pid_paw3204(DEV1) != SENSOR_PRODUCT_ID1 ) {
        Serial.println("failed to init SENSOR1 !!!");
    }
    set_dpi_paw3204(DEV1, DPI_1600);

    init_paw3204(DEV2);
    if( read_pid_paw3204(DEV2) != SENSOR_PRODUCT_ID1 ) {
        Serial.println("failed to init SENSOR2 !!!");
    }
    set_dpi_paw3204(DEV2, DPI_1600);

    // Initialize Encoder
    RotaryEncoder.begin(PIN_D19, PIN_D20);
    // Start encoder
    RotaryEncoder.start();

    pinMode(PIN_ENC_BUTTON, INPUT);
    prev_btn = digitalRead(PIN_ENC_BUTTON);

    ledOn(LED_GREEN);

    SEGGER_RTT_printf(0, "setup end\n");


    xTaskCreate(ball_task, "ball", STACK_SZ, NULL, TASK_PRIO_HIGH, &_bsensHandle);

#ifdef USE_GYRO
    xTaskCreate(gyro_task, "gyro", STACK_SZ, NULL, TASK_PRIO_LOW, &_gyroHandle);
#endif

}





paw3204_all_reg dat;

int8_t m1x, m1y, m2x, m2y;
uint8_t stat1, stat2, qua1, qua2;

int32_t encoder_value = 0;
long oldPosition  = -999;

//enum {
//    BTN_RELEASED = 0,
//    BTN_JUST_PRESSED,
//    BTN_PRESSED,
//    BTN_JUST_RELEASED,
//};

//uint8_t btn_state = BTN_RELEASED;
//uint btn_ticks = 0;
//
//void upd_btn() {
//    switch (btn_state) {
//        case BTN_RELEASED:
//            if (digitalRead(PIN_ENC_BUTTON)) {
//                btn_state = BTN_JUST_PRESSED;
//                btn_ticks = 0
//            }
//            break;
//
//        case BTN_JUST_PRESSED:
//            if (digitalRead(PIN_ENC_BUTTON)) {
//                if (btn_ticks > 10) {
//                    btn_state = BTN_PRESSED;
//                } else {
//                    btn_ticks += 1;
//                }
//            } else {
//                btn_state = BTN_RELEASED;
//            }
//            break;
//
//        case BTN_PRESSED:
//            break;
//
//        case BTN_JUST_RELEASED:
//            break;
//    }
//}


float prev_angles[3] = {0,0,0};

uint32_t do_encoder();

bool zero_motion_sent = true;

const int USB_WAIT_DELAY_MS = 1;
const int GYRO_SEND_INTERVAL_MS = 1000/60; // 60Hz

uint32_t t_gyro_send_prev = 0;

void loop()
{

    bool something_sent = false;

    if (bipbuf_used(&BipBuf_hid_mirror) > 2) {

        while (! usb_hid.ready() ) {
            delay(USB_WAIT_DELAY_MS);
        }

//        tud_hid_report(REPORT_ID_JOYSTICK, joystick_buffer[joystick_buffer_tail], JOYSTICK_REPORT_SIZE);
//        joystick_buffer_tail = (joystick_buffer_tail + 1) % JOYSTICK_FEED_BUFFER_SIZE;

        const uint8_t *hdr = bipbuf_peek(&BipBuf_hid_mirror, 2);
        const uint8_t report_id = hdr[0];
        const uint8_t report_sz = hdr[1];
        bipbuf_poll(&BipBuf_hid_mirror, 2);

        // может оказаться, что заголовок уже залит, а сам пакет ещё нет.
        // или не может, тут же всё-таки не многопоточность

        // while (bipbuf_used(&BipBuf) < report_sz) delay(1);

        const uint8_t *report_data = bipbuf_peek(&BipBuf_hid_mirror, report_sz);
        tud_hid_report(report_id, report_data, report_sz);
        bipbuf_poll(&BipBuf_hid_mirror, report_sz);

        something_sent = true;

//        printf("sending report_id: report_id:0x%02x %d bytes (used after: %d bytes)\n", report_id, report_sz, bipbuf_used(&BipBuf));

    }

//    while (! usb_hid.ready() ) {
//        delay(USB_WAIT_DELAY_MS);
//    }

//    SEGGER_RTT_printf(0, "loop tick\n");

    uint32_t encoder_delta = do_encoder();
//    ledOn(LED_BLUE);
//    ledOff(LED_BLUE);

    uint32_t m = millis();

#ifdef USE_GYRO

    if ( m - t_gyro_send_prev > GYRO_SEND_INTERVAL_MS ) {

        t_gyro_send_prev = m;

        Quaternion q_accumulated = Quaternion();
        if (Gyro.get_delta_quat(&q_accumulated) && translation_mode != MODE_MOUSE_1RDOF) {
//        printf("angles loop: %5.2f\t%5.2f\t%5.2f\n", xyz[0], xyz[1], xyz[2]);

            float xyz[3];
            float ypr[3];
            GetEuler(ypr, &q_accumulated);
            ConvertToDegrees(ypr, xyz);


            int16_t dx = xyz[1] * 10;
            int16_t dy = -xyz[2] * 10;
            int16_t dz = xyz[0] * 10;

            if (abs(dx) > 1 || abs(dy) > 1 || abs(dz) > 1) {
                send_motion(dx, dy, dz, 0);

//                printf("send_motion: %5d\t%5d\t%5d\n", dx, dy, dz);

                while (!usb_hid.ready()) {
                    delay(USB_WAIT_DELAY_MS);
                }
            }

        }
    }

#endif

//    printf("used: %d\n", bipbuf_used(&BipBuf_ball));
    while ( bipbuf_used(&BipBuf_ball) >= sizeof(ball_data_t) ) {
        while (! usb_hid.ready() ) {
            delay(USB_WAIT_DELAY_MS);
        }

        auto ball_data = (ball_data_t *)(bipbuf_peek(&BipBuf_ball, sizeof(ball_data_t)));
        send_motion(ball_data->cx, ball_data->cy, ball_data->cz, encoder_delta, encoder_value, ball_data->ts);
        bipbuf_poll(&BipBuf_ball, sizeof(ball_data_t));

        something_sent = true;
    }

    if (! something_sent) {
        delay(2);
    }


}

uint32_t do_encoder() {
    uint8_t cur_btn = digitalRead(PIN_ENC_BUTTON);
    if (cur_btn != prev_btn) {
        prev_btn = cur_btn;
//        SEGGER_RTT_printf(0, "enc button %s\n", cur_btn ? "pressed" : "released");
//        send_3dx_report_buttons(cur_btn ? 0 : V3DK_1 );
//        return;

        if (cur_btn) {

            Gyro.reset_origin();

            ledOff(LED_RED);
            ledOff(LED_GREEN);
            ledOff(LED_BLUE);;

            switch (translation_mode) {
                case MODE_ROT_3DOF:
                    translation_mode = MODE_MOUSE_1RDOF;
                    ledOn(LED_BLUE);
                    break;
                case MODE_MOUSE_1RDOF:
                    translation_mode = MODE_RAW;
                    ledOn(LED_RED);
                    break;
                case MODE_RAW:
                    translation_mode = MODE_ROT_3DOF;
                    ledOn(LED_GREEN);
                    break;
            }
        }

    }

    int enc_delta = RotaryEncoder.read();
    if (enc_delta) {
        encoder_value += enc_delta;
//        SEGGER_RTT_printf(0, "encoder: %d\n", encoder_value);
//        if(translation_mode == MODE_ROT_3DOF ) {
//            SENSOR_R_SCALE += 0.05f * (float) (enc_delta);
//            if (SENSOR_R_SCALE < 0.2) {
//                SENSOR_R_SCALE = 0.2;
//            } else if (SENSOR_R_SCALE > 5) {
//                SENSOR_R_SCALE = 5;
//            }
//        }
    }

    return enc_delta;

}




