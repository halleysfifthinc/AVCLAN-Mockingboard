#include "hal/phy.h"
#include "avclan.h"

using namespace avclan;
using enum detail::Error::Read;
using enum detail::Error::Send;

extern "C" void phy_init() {}

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
