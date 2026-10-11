#include "kbd_tca8418.h"
#include <algorithm>
#include "utility/Adafruit_TCA8418/Adafruit_TCA8418.h"
#include "utility/Adafruit_TCA8418/Adafruit_TCA8418_registers.h"
#include <M5Unified.h>
#include <memory>

#define POSEIDON_TCA_INT 11

static std::unique_ptr<Adafruit_TCA8418> s_tca;
static volatile bool s_isr_flag = false;

static void IRAM_ATTR tca_isr(void *)
{
    s_isr_flag = true;
}

void PoseidonTcaReader::begin()
{
    s_tca = std::make_unique<Adafruit_TCA8418>();
    if (!s_tca->begin()) {
        printf("[kbd] TCA8418 begin failed\n");
        return;
    }
    s_tca->matrix(7, 8);
    s_tca->flush();
    pinMode(POSEIDON_TCA_INT, INPUT);
    attachInterruptArg(digitalPinToInterrupt(POSEIDON_TCA_INT), tca_isr, nullptr, CHANGE);
    s_tca->enableInterrupts();
}

void PoseidonTcaReader::update()
{
    if (!s_tca) return;
    int guard = 16;
    while (guard-- && s_tca->available()) {
        uint8_t ev = s_tca->getEvent();
        if (!ev) break;
        bool state = ev & 0x80;
        uint16_t buffer = ev & 0x7F;
        if (buffer) buffer--;
        uint8_t row = (uint8_t)(buffer / 10);
        uint8_t col = (uint8_t)(buffer % 10);
        uint8_t mcol = (uint8_t)(row * 2);
        if (col > 3) mcol++;
        uint8_t mrow = (uint8_t)((col + 4) % 4);
        Point2D_t point;
        point.x = mcol;
        point.y = mrow;
        auto it = std::find(_key_list.begin(), _key_list.end(), point);
        if (state) {
            if (it == _key_list.end()) _key_list.push_back(point);
        } else if (it != _key_list.end()) {
            _key_list.erase(it);
        }
    }
    s_tca->writeRegister8(TCA8418_REG_INT_STAT, 0x1F);
    s_isr_flag = false;
}
