#include "hal/phy.h"
#include "avclan.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "iebus.pio.h"
#include <hardware/clocks.h>

#define TICK_US 1000000
#include "timing.h"

using namespace avclan;
using enum detail::Error::Read;
using enum detail::Error::Send;

namespace {
constexpr int IEBUS_TX = 16;
constexpr int IEBUS_RX = 17;

// Activity indicators: each follows its bus pin exactly, so an LED wired
// supply -> resistor -> pin is lit while that line is dominant (low). The bus
// pins themselves stay unloaded.
constexpr int LED_RX = 8;
constexpr int LED_TX = 26;

PIO pio;
uint sm;
uint offset;

inline void iebus_rx_program_init(PIO pio, uint sm, uint offset, uint pin_rx,
                                  uint pin_tx) {
  pio_sm_set_consecutive_pindirs(pio, sm, pin_rx, 1, false);
  pio_sm_set_consecutive_pindirs(pio, sm, pin_tx, 1, true);
  pio_sm_set_pins_with_mask(pio, sm, (1U << pin_tx), (1U << pin_tx));
  pio_gpio_init(pio, pin_rx);
  pio_gpio_init(pio, pin_tx);

  pio_sm_config cfg = iebus_rx_program_get_default_config(offset);
  sm_config_set_in_pins(&cfg, pin_rx);
  sm_config_set_jmp_pin(&cfg, pin_rx);
  sm_config_set_set_pins(&cfg, pin_tx, 1);

  // CYCLES_PER_READBIT_PERIOD PIO cycles should take
  // ~AVCLAN_READBIT_THRESHOLD μs
  float div = clock_get_hz(clk_sys) /
              (CYCLES_PER_READBIT_PERIOD / (float)AVCLAN_READBIT_THRESHOLD);
  sm_config_set_clkdiv(&cfg, div);

  pio_sm_init(pio, sm, offset, &cfg);
  pio_sm_set_enabled(pio, sm, true);
}

// Point one mirror SM at one src -> dst pair. Only the destination gets
// `pio_gpio_init`: taking the function select of a source pin would hand it to
// this PIO block and cut whoever actually drives it (IEBUS_TX) loose. Reading
// needs nothing but the pad's input buffer, which is on for both bus pins
// already -- asserted here so the mirror cannot go dark if that changes.
void pin_mirror_sm_init(PIO mpio, uint msm, uint moffset, uint src, uint dst) {
  gpio_set_input_enabled(src, true);
  pio_gpio_init(mpio, dst);
  pio_sm_set_consecutive_pindirs(mpio, msm, dst, 1, true);

  pio_sm_config cfg = pin_mirror_program_get_default_config(moffset);
  sm_config_set_in_pins(&cfg, src);
  sm_config_set_out_pins(&cfg, dst, 1);
  // Default clkdiv: one copy per system clock, so the LED tracks the line far
  // faster than a bit period.

  pio_sm_init(mpio, msm, moffset, &cfg);
  pio_sm_set_enabled(mpio, msm, true);
}
} // namespace

extern "C" void phy_init() {
  gpio_init(IEBUS_TX);
  gpio_init(IEBUS_RX);
  gpio_set_dir(IEBUS_TX, true);  // CAN/AVCLAN TX
  gpio_set_dir(IEBUS_RX, false); // CAN/AVCLAN RX

  bool success =
      pio_claim_free_sm_and_add_program(&iebus_rx_program, &pio, &sm, &offset);
  hard_assert(success);

  iebus_rx_program_init(pio, sm, offset, IEBUS_RX, IEBUS_TX);
  pio_sm_exec(pio, sm, pio_encode_irq_set(false, should_ack_irq));

}


extern "C" void phy_mute(bool mute) {}

extern "C" bool phy_is_muted() { return true; }

extern "C" bool phy_active() { return false; }

extern "C" void phy_guard_enter() {}

extern "C" void phy_guard_leave() {}

extern "C" Read phy_read_startbit() { return BAD_STARTBIT; }

extern "C" Send phy_send_startbit() { return MUTED; }

extern "C" Send phy_read_ack() { return MUTED; }

extern "C" void phy_send_ack() {}

extern "C" void phy_send_bit(Bit bit) {}

extern "C" Bit phy_send_bits_u8(const uint8_t *bits, int8_t len) {
  return Bit::bit_zero;
};

extern "C" Bit phy_send_bits_u16(const uint16_t *bits, int8_t len) {
  return Bit::bit_zero;
};

extern "C" Bit phy_send_byte(const uint8_t *byte) { return Bit::bit_zero; };

extern "C" Bit phy_read_bits_u8(uint8_t *bits, uint8_t len) {
  return Bit::bit_zero;
};

extern "C" Bit phy_read_bits_u16(uint16_t *bits, int8_t len) {
  return Bit::bit_zero;
};

extern "C" Bit phy_read_byte(uint8_t *byte) { return Bit::bit_zero; };

#if !defined(NDEBUG) && defined(MEASURE_BUS)
// Sample and dump bus bit timing over the serial link (REPL `M`).
void phy_measure(void);
#endif
