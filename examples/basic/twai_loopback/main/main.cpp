#include "esp32libfun.hpp"

constexpr int TwaiTx = 4;
constexpr int TwaiRx = 5;
constexpr uint32_t Bitrate = 500000;
constexpr uint32_t RepeatMs = 1000;

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(serial.at(true));
    ESP_ERROR_CHECK(twai.begin(TwaiTx, TwaiRx, Bitrate, 5, true, true));

    serial.println(M "Hello from ESP32LibFun!");
    serial.println(B "TWAI loopback self-test");
    serial.println(C "TX=%d RX=%d bitrate=%lu", TwaiTx, TwaiRx, static_cast<unsigned long>(Bitrate));
    serial.println(C "AT ready: " W "AT" W ", " W "AT+HELP?");

    uint8_t counter = 0;
    while (true) {
        const uint8_t payload[] = {0xA5, counter, static_cast<uint8_t>(counter + 1U), 0x5A};
        ESP_ERROR_CHECK(twai.write(TwaiTx, 0x123, payload, sizeof(payload)));
        ESP_ERROR_CHECK(twai.wait(TwaiTx, 1000));

        TwaiFrame frame = {};
        esp_err_t rx_err = twai.read(TwaiTx, &frame, 1000);
        if (rx_err == ESP_OK) {
            serial.println(C "RX id=0x%lX len=%u data=%02X %02X %02X %02X",
                           static_cast<unsigned long>(frame.id),
                           static_cast<unsigned>(frame.len),
                           frame.data[0],
                           frame.data[1],
                           frame.data[2],
                           frame.data[3]);
        } else {
            serial.println(R "TWAI read failed: %s", esp_err_to_name(rx_err));
        }

        TwaiStatus status = {};
        if (twai.status(TwaiTx, &status) == ESP_OK) {
            serial.println(C "status state=%d txerr=%u rxerr=%u buserr=%lu",
                           status.state,
                           status.tx_error_count,
                           status.rx_error_count,
                           static_cast<unsigned long>(status.bus_error_count));
        }

        ++counter;
        delay.ms(RepeatMs);
    }
}
