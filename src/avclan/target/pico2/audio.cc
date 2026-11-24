// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

#include "FreeRTOS.h" // IWYU pragma: export

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"

#include "bluetooth/audio/output.h"
#include "i2s.pio.h"

using avclan::bluetooth::audio::output::Block;
using avclan::bluetooth::audio::output::BLOCK_FRAMES;
using avclan::bluetooth::audio::output::StereoFrame;

namespace {
class I2SAudio {
public:
  I2SAudio() = default;
  I2SAudio(const I2SAudio &) = delete;
  I2SAudio &operator=(const I2SAudio &) = delete;

  ~I2SAudio() {
    if (instance_ != this)
      return;

    instance_ = nullptr;
  }

  void init() {
    hard_assert(instance_ == nullptr);
    instance_ = this;

    sm = (uint)pio_claim_unused_sm(pio, true);

    const int off = pio_add_program(pio, &audio_i2s_program);
    hard_assert(off >= 0);
    offset = (uint)off;

    pio_gpio_init(pio, I2S_DATA);
    pio_gpio_init(pio, I2S_BCLK);
    pio_gpio_init(pio, I2S_BCLK + 1);

    pio_sm_config cfg = audio_i2s_program_get_default_config(offset);
    sm_config_set_out_pins(&cfg, I2S_DATA, 1);
    sm_config_set_sideset_pins(&cfg, I2S_BCLK);
    sm_config_set_out_shift(&cfg, false, true, 32);
    sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_TX);
    pio_sm_init(pio, sm, offset, &cfg);
    pio_sm_set_pindirs_with_mask(pio, sm, PIN_MASK, PIN_MASK);
    restart_sm();

    for (uint &ch : dma)
      ch = (uint)dma_claim_unused_channel(true);
    irq_set_exclusive_handler(DMA_IRQ_1, dma_irq_handler);
    irq_set_priority(DMA_IRQ_1, configMAX_SYSCALL_INTERRUPT_PRIORITY);
  }

  uint32_t start(uint32_t rate, auto src) {
    source = std::move(src);

    // Two PIO cycles per bit and 32 bits per frame
    constexpr uint32_t CYCLES_PER_FRAME = 2 * 32;
    const uint64_t clk = clock_get_hz(clk_sys);
    uint32_t div_int;
    uint8_t div_frac8;
    pio_calculate_clkdiv8_from_float(
        static_cast<float>(clk) / static_cast<float>(rate * CYCLES_PER_FRAME),
        &div_int, &div_frac8);
    pio_sm_set_clkdiv_int_frac8(pio, sm, div_int, div_frac8);

    for (size_t i = 0; i < 2; i++) {
      dma_channel_config c = dma_channel_get_default_config(dma[i]);
      channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
      channel_config_set_chain_to(&c, dma[1 - i]);
      dma_channel_configure(dma[i], &c, &pio->txf[sm],
                            source().value_or(silence).data(), BLOCK_FRAMES,
                            false);
      dma_channel_acknowledge_irq1(dma[i]);
      dma_channel_set_irq1_enabled(dma[i], true);
    }
    irq_set_enabled(DMA_IRQ_1, true);
    dma_channel_start(dma[0]);
    pio_sm_set_enabled(pio, sm, true);

    // System clocks per frame, in 1/256ths
    const uint64_t div =
        ((uint64_t{div_int} << 8) | div_frac8) * CYCLES_PER_FRAME;
    return static_cast<uint32_t>(((clk << 8) + (div / 2)) / div);
  }

  void stop() {
    irq_set_enabled(DMA_IRQ_1, false);
    // RP2350-E5: a chained channel can re-trigger an aborted one unless both
    // are disabled first
    for (const uint ch : dma)
      hw_clear_bits(&dma_hw->ch[ch].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    for (const uint ch : dma)
      dma_channel_cleanup(ch);
    restart_sm();
  }

private:
  static constexpr uint I2S_DATA = 9;
  // LRCLK is BCLK + 1: the program's side-set pair
  static constexpr uint I2S_BCLK = 10;
  static constexpr uint32_t PIN_MASK = (1U << I2S_DATA) | (3U << I2S_BCLK);

  static void __time_critical_func(dma_irq_handler)() { instance_->isr(); }

  // Re-arms the channel that finished while the other one plays
  void __time_critical_func(isr)() {
    for (const uint ch : dma) {
      if (!dma_channel_get_irq1_status(ch))
        continue;
      dma_channel_acknowledge_irq1(ch);
      dma_channel_set_read_addr(ch, source().value_or(silence).data(), false);
    }
  }

  // NOLINTNEXTLINE(readability-make-member-function-const)
  void restart_sm() {
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_set_pins_with_mask(pio, sm, 0, PIN_MASK);
    pio_sm_exec(pio, sm, pio_encode_jmp(offset + audio_i2s_offset_entry_point));
  }

  inline static constinit I2SAudio *instance_ = nullptr;

  // Disallow re-assigning pio pointer (value is non-const)
  const PIO pio = pio2; // NOLINT(misc-misplaced-const)
  uint sm;
  uint offset;
  uint dma[2];
  std::move_only_function<std::optional<Block>()> source;
  const StereoFrame silence[BLOCK_FRAMES]{};
};

I2SAudio i2s;
} // namespace

namespace avclan::bluetooth::audio::output {
void init() { i2s.init(); }

uint32_t start(uint32_t rate,
               std::move_only_function<std::optional<Block>()> source) {
  return i2s.start(rate, std::move(source));
}

void stop() { i2s.stop(); }

} // namespace avclan::bluetooth::audio::output
