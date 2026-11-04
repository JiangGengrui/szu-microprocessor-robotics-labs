#include "effects.h"
#include "led.h"

static uint8_t s_effect = 1;     /* 当前特效号 */
static uint8_t s_step   = 0;    /* 当前步 */

void EFFECT_Set(uint8_t id)
{
    if ((id >= 1) && (id <= EFFECT_COUNT)) {
        s_effect = id;
        s_step = 0;
        LED_Write(0x00);        /* 切换瞬间先全灭，保证起点干净 */
    }
}

uint8_t EFFECT_Get(void) { return s_effect; }

void EFFECT_Step(void)
{
    uint8_t pattern = 0;

    switch (s_effect) {

    case 1:   /* 特效1：单灯单向流水 */
        pattern = (uint8_t)(1u << s_step);
        s_step  = (s_step + 1) % 8;
        break;

    case 2: { /* 特效2：往返（乒乓）流水 */
        static const uint8_t seq[14] =
            {0,1,2,3,4,5,6,7,6,5,4,3,2,1};
        pattern = (uint8_t)(1u << seq[s_step]);
        s_step  = (s_step + 1) % 14;
        break; }

    case 3: { /* 特效3：双灯对称开合（外->内->外） */
        static const uint8_t seq[4] =
            {0x81, 0x42, 0x24, 0x18};
        pattern = seq[s_step];
        s_step  = (s_step + 1) % 4;
        break; }

    case 4:   /* 特效4：奇偶交替闪烁 */
        pattern = (s_step & 1u) ? 0xAA : 0x55;
        s_step  = (s_step + 1) % 2;
        break;
    }

    LED_Write(pattern);
}
