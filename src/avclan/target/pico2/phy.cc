#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <hardware/clocks.h>

#include "avclan.h"
#include "hal/phy.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "iebus.pio.h"
#include "phy_debug.hpp"

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

constexpr uint32_t parity(uint32_t val) {
  static_assert(sizeof(uint32_t) == sizeof(unsigned int));
  if consteval {
    int N = 0;
    for (; val != 0UL; N++) {
      val = val & (val - 1);
    }
    return (N & 1);
  }

  return __builtin_parity(val);
}

// CYCLES_PER_READBIT_PERIOD PIO cycles should take ~AVCLAN_READBIT_THRESHOLD
// μs. All three bus SMs share it: iebus_rx and iebus_ack are cross-PIO IRQ
// partners, which the datasheet (S11.4) requires to have equal dividers,
// synchronised by a single CTRL write -- see the enable in Phy::init.
inline float bus_clkdiv() {
  return clock_get_hz(clk_sys) /
         (CYCLES_PER_READBIT_PERIOD / (float)AVCLAN_READBIT_THRESHOLD);
}

// The reader runs from the PIO's RX-FIFO interrupt rather than from the calling
// thread, so slots are drained and the next script queued no matter what the
// main loop is doing. That is not polish: the RX FIFO is 4 deep and printf over
// USB CDC blocks for milliseconds, which is exactly the failure rx_log_push
// exists to dodge. It is also what lets a frame we lost arbitration to still
// arrive -- the engine never stopped receiving.
//
// The frames it completes are handed out whole; hal/phy.h's read calls pick
// their field out of one.
class IEBusRx {
  // Field widths, in the SM's "data bits" terms. The broadcast bit carries no
  // parity, so it is the one field read as a bare bit (count 0 => 1 bit).
  static constexpr uint8_t W_BROADCAST = 0;
  static constexpr uint8_t W_ADDR = 12;
  static constexpr uint8_t W_CONTROL = 4;
  static constexpr uint8_t W_BYTE = 8;

  enum class RxField : uint8_t {
    Broadcast,
    Controller,
    Peripheral,
    Control,
    Length,
    Data,
  };

  // Must be a power of two (the index wrap is a mask).
  static constexpr uint32_t RXQ_N = 4;
  static_assert((RXQ_N & (RXQ_N - 1)) == 0,
                "RX buffer size must be a power of 2");

public:
  struct RxFrame {
    uint16_t controller_addr;
    uint16_t peripheral_addr;
    uint8_t control;
    uint8_t length;
    uint8_t data[32];
    bool is_unicast;
    Read err;
  };

  IEBusRx() = default;
  IEBusRx(const IEBusRx &) = delete;
  IEBusRx &operator=(const IEBusRx &) = delete;

  ~IEBusRx() {
    if (instance_ != this)
      return;
    irq_set_enabled(irq_, false);
    irq_remove_handler(irq_, irq_handler);
    pio_set_irq0_source_enabled(
        pio_, pio_get_rx_fifo_not_empty_interrupt_source(sm_), false);
    pio_sm_set_enabled(pio_, sm_, false);
    pio_remove_program_and_unclaim_sm(&iebus_rx_program, pio_, sm_, offset_);
    instance_ = nullptr;
  }

  // Claims and configures the iebus_rx SM and its interrupt, but doesn't start
  // the SM.
  void init(PIO pio, uint pin_rx, uint16_t address) {
    hard_assert(instance_ == nullptr);
    instance_ = this;
    pio_ = pio;
    pin_ = pin_rx;
    self_addrp_ = ((uint32_t)address << 1) | parity(address);
    offset_ = (uint)pio_add_program(pio_, &iebus_rx_program);
    sm_ = (uint)pio_claim_unused_sm(pio_, true);

    gpio_init(pin_);
    gpio_set_dir(pin_, false); // CAN/AVCLAN RX; PIO reads it without a claim
    gpio_set_input_enabled(pin_, true);
    pio_sm_set_consecutive_pindirs(pio_, sm_, pin_, 1, false);

    pio_sm_config cfg = iebus_rx_program_get_default_config(offset_);
    sm_config_set_in_pins(&cfg, pin_);
    sm_config_set_jmp_pin(&cfg, pin_);
    sm_config_set_clkdiv(&cfg, bus_clkdiv());

    // Entry is `wait_read`, not the program start -- offset 0 is rx_startbit,
    // which would fall through into the header reads before the driver's own
    // jmp to it, leaving every later field one read out of step.
    pio_sm_init(pio_, sm_, offset_ + iebus_rx_wrap_target, &cfg);
    prepare_ack();
    begin_frame();

    pio_set_irq0_source_enabled(
        pio_, pio_get_rx_fifo_not_empty_interrupt_source(sm_), true);
    irq_ = (uint)pio_get_irq_num(pio_, 0);
    irq_set_exclusive_handler(irq_, irq_handler);
    irq_set_priority(irq_, PICO_HIGHEST_IRQ_PRIORITY);
    irq_set_enabled(irq_, true);
  }

  PIO pio() const { return pio_; }
  uint32_t sm_mask() const { return 1U << sm_; }

  // Muted means we still listen, we just don't answer: no ack, ever. Disarming
  // the latch is what makes that true of the SM as well.
  void mute(bool mute) {
    muted_ = mute;
    sync_ack_arming();
  }

  void deafen(bool deaf) {
    deafened_ = deaf;
    sync_ack_arming();
  }

  // Disarm RX ACK'ing behavior; called prior to frame TX to prevent
  // self-ACK'ing. Safe to rearm any time after sending controller addr.
  void disarm_ack() {
    transmitting_ = true;
    sync_ack_arming();
  }

  // Rearm RX ACK'ing behavior; called after sending controller addr.
  void rearm_ack() {
    transmitting_ = false;
    sync_ack_arming();
  }

  // Buffered frames exist to be read
  bool frame_pending() const { return rxq_tail_ != rxq_head_; }

  // Only valid while frame_pending().
  const RxFrame &frame() const { return rxq_[rxq_tail_]; }

  void release() { rxq_tail_ = (rxq_tail_ + 1) & (RXQ_N - 1); }

private:
  // True when publish() would have nowhere to put the frame being received.
  bool queue_full() const {
    return ((rxq_head_ + 1) & (RXQ_N - 1)) == rxq_tail_;
  }

  static void __time_critical_func(irq_handler)() { instance_->isr(); }

  void __time_critical_func(isr)() {
    while (!pio_sm_is_rx_fifo_empty(pio_, sm_)) {
      const auto slot = (uint16_t)pio_sm_get(pio_, sm_);
      uint16_t value = 0;

      switch (state_) {
        case RxField::Broadcast:
          building_.is_unicast = (slot & 1U) != 0U;
          state_ = RxField::Controller;
          break;

        case RxField::Controller:
          // Judged on the peripheral slot: that read is already running, and a
          // restart now would take its slot for the next frame's broadcast bit.
          controller_ok_ = update_value<W_ADDR>(slot, &value);
          building_.controller_addr = value;
          state_ = RxField::Peripheral;
          break;

        case RxField::Peripheral: {
          // Past arbitration our own frame is the tx SM's business: it reads
          // its own acks, and a copy here would only cost a queue slot. A frame
          // we owe an ack for is refused when there is no room for it -- the
          // NAK asks the sender to send it again, rather than losing it behind
          // an ack we can't honour. Both come before the field checks: a frame
          // being given up needs no parity verdict, and reporting one would
          // name the wrong cause for the same NAK.
          const bool ours =
              building_.controller_addr == (uint16_t)(self_addrp_ >> 1);
          const bool refuse =
              !ours && queue_full() && pio_interrupt_get(pio_, ack_latch);

          if (ours || refuse) {
            if (refuse)
              rxq_refused_ = rxq_refused_ + 1;
            // Withdrawing the ack is part of giving the frame up; its slot is
            // still ahead of the SM. Re-dispatching the start-bit block hands
            // the rest of the frame back to it: those bits are far too short to
            // read as a start bit, so it re-syncs on the next real one by
            // itself. This also settles a race on our own frames -- rearm_ack()
            // restores y the moment we win the controller address, in time for
            // that same read to have matched it.
            pio_interrupt_clear(pio_, ack_latch);
            begin_frame();
            break;
          }

          if (!controller_ok_) {
            publish(BAD_CONTROLLER_PARITY);
            break;
          }
          if (!update_value<W_ADDR>(slot, &value)) {
            publish(BAD_PERIPHERAL_PARITY);
            break;
          }
          building_.peripheral_addr = value;
          next_after_ack(RxField::Control, W_CONTROL);
          break;
        }

        case RxField::Control:
          if (!update_value<W_CONTROL>(slot, &value)) {
            publish(BAD_CONTROL_PARITY);
            break;
          }
          building_.control = (uint8_t)value;
          next_after_ack(RxField::Length, W_BYTE);
          break;

        case RxField::Length:
          if (!update_value<W_BYTE>(slot, &value)) {
            publish(BAD_LENGTH_PARITY);
            break;
          }
          building_.length = (uint8_t)value;
          if (value == 0 || value > sizeof(building_.data)) {
            publish(BAD_LENGTH_RANGE);
            break;
          }
          next_after_ack(RxField::Data, W_BYTE);
          break;

        case RxField::Data:
          if (!update_value<W_BYTE>(slot, &value)) {
            publish(BAD_DATA_PARITY);
            break;
          }
          building_.data[data_i_++] = (uint8_t)value;
          if (data_i_ >= building_.length)
            publish(Read{0});
          else
            next_after_ack(RxField::Data, W_BYTE);
          break;
      }
    }
  }

  // Start (or restart) a frame. The start-bit block is dispatched like any
  // other field, except the streamed instruction is a jmp instead of a read's
  // setup. It only falls through on a start bit, so the header's reads queue
  // right behind it. Four words fill the FIFO, which is empty here: the SM
  // takes each word before pushing the slot that ends a frame.
  void __time_critical_func(begin_frame)() {
    state_ = RxField::Broadcast;
    building_ = {};
    data_i_ = 0;
    pio_sm_put(pio_, sm_,
               pio_encode_jmp(offset_ + iebus_rx_offset_rx_startbit));
    enqueue_rx(W_BROADCAST, false);
    enqueue_rx(W_ADDR, false);
    enqueue_rx(W_ADDR, true);
  }

  // Load our address (with parity) into the SM's Y register.
  // The SM compares read values to Y to set the ack latch and trigger ACK'ing.
  // SM must be stopped with an empty FIFO.
  void prepare_ack() {
    pio_sm_put(pio_, sm_, self_addrp_);
    pio_sm_exec(pio_, sm_, pio_encode_pull(false, true));
    pio_sm_exec(pio_, sm_, pio_encode_out(pio_y, 32));
    ack_armed_ = true;
  }

  // Bring the latch's arming in line with the reasons to withhold an ack.
  // Disarming inverts the Y register (self address + parity), which leaves the
  // upper 19 bits set and so prevents any read from matching.
  void sync_ack_arming() {
    const bool arm = !muted_ && !deafened_ && !transmitting_;
    if (arm == ack_armed_)
      return;
    pio_sm_exec_wait_blocking(pio_, sm_, pio_encode_mov_not(pio_y, pio_y));
    ack_armed_ = arm;
  }

  // One read's script word: [has_ack 31][zeros 30:16][instr 15:0]. The SM reads
  // bits + 1 (the field plus its parity); the instruction zeroes its parity
  // accumulator and leaves that many bits before the OSR threshold. `bits` must
  // be <= 13: at 14 the instruction would encode as `out x, 0`, which the ISA
  // reads as 32. `has_ack` has the SM consume the ack slot after the field.
  static constexpr uint32_t encode_rx(uint8_t bits, bool has_ack) {
    return ((uint32_t)has_ack << 31) | pio_encode_out(pio_x, 14 - bits);
  }

  // Update `*value` with the `read` value. Returns true for a correct parity.
  template <auto N> static bool update_value(uint16_t read, uint16_t *value) {
    *value = (uint16_t)((read >> 1) & ((1U << N) - 1U));
    return parity(*value) == (unsigned)(read & 1U);
  }

  void __time_critical_func(enqueue_rx)(uint8_t bits, bool has_ack) {
    pio_sm_put_blocking(pio_, sm_, encode_rx(bits, has_ack));
  }

  // New frames are dropped if the rx queue is full. A frame we acked is not
  // among them: there was room for it at the peripheral slot, and from here the
  // queue only gains space. What is left to lose is what no ack was owed for.
  void __time_critical_func(publish)(Read err) {
    // The SM NAKs bad parity itself; this vetoes what only we can judge
    // (controller parity, length range) before its late read of the latch.
    if (err != Read{0})
      pio_interrupt_clear(pio_, ack_latch);
    building_.err = err;
    if (queue_full()) {
      rxq_drops_ = rxq_drops_ + 1; // Never stall the bus for a lagging reader
    } else {
      rxq_[rxq_head_] = building_;
      rxq_head_ = (rxq_head_ + 1) & (RXQ_N - 1);
    }
    begin_frame();
  }

  // Every field after the controller address is followed by an ack slot, which
  // the SM consumes whether or not it drives it.
  void __time_critical_func(next_after_ack)(RxField next, uint8_t bits) {
    state_ = next;
    enqueue_rx(bits, true);
  }

  // The initialized instance, for irq_handler: SDK IRQ handlers take no
  // context.
  static inline IEBusRx *instance_ = nullptr;

  PIO pio_;
  uint sm_;
  uint offset_;
  uint pin_;
  uint irq_;

  // Our address as the rx SM sees it: a slot is the field plus its parity bit,
  // and parity is a function of the field, so there is exactly one legal slot
  // value for us.
  uint32_t self_addrp_;

  bool ack_armed_ = false; // hardware: y holds self_addrp_, or its complement
  bool muted_ = false;     // we don't answer on the bus
  bool deafened_ = false;  // we don't answer, but may still transmit
  bool transmitting_ = false; // our own frame is on the wire

  std::array<RxFrame, RXQ_N> rxq_ = {};
  volatile uint32_t rxq_head_ = 0; // written by the ISR only
  volatile uint32_t rxq_tail_ = 0; // written by the reader only
  // Lost: no ack was owed, so there was nothing to refuse -- a broadcast, or a
  // unicast addressed elsewhere.
  volatile uint32_t rxq_drops_ = 0;
  // NAK'd for want of room; the sender still owns the frame and sends it again.
  volatile uint32_t rxq_refused_ = 0;

  RxFrame building_ = {};
  RxField state_ = RxField::Broadcast;
  uint8_t data_i_ = 0;
  bool controller_ok_ = false; // parity verdict, held until the peripheral slot
};

// The transmit engine: the iebus_tx SM, plus the iebus_ack SM that drives the
// ack slot on the rx engine's behalf. Both live on one PIO because they share
// the TX pin.
//
// Everything past the arbitration window is queued, not sent, so the fields
// report nothing and the frame's verdict comes from send_done.
class IEBusTx {
  // The flag the SM parked on. Sticky until reported; nothing more is queued
  // meanwhile.
  enum class Fault : uint8_t { None, Nak, Mismatch };

public:
  // The ack match lives in the rx SM but exists for our sake: it has to be
  // parked while our own address is on the wire, and put back the moment we
  // stop transmitting -- including when we lose arbitration mid-field.
  explicit IEBusTx(IEBusRx &rx) : rx_(rx) {}
  IEBusTx(const IEBusTx &) = delete;
  IEBusTx &operator=(const IEBusTx &) = delete;

  // Hands both SMs and their program memory back. Stopping them leaves the pad
  // at its last driven level, which is recessive -- the same property mute
  // relies on. The pad keeps its PIO function select: handing it back to SIO
  // would float the line, and a floating bus reads dominant.
  ~IEBusTx() {
    if (!claimed_)
      return;
    pio_set_sm_mask_enabled(pio_, sm_mask(), false);
    pio_remove_program_and_unclaim_sm(&iebus_tx_program, pio_, tx_sm_,
                                      tx_offset_);
    pio_remove_program_and_unclaim_sm(&iebus_ack_program, pio_, ack_sm_,
                                      ack_offset_);
  }

  // Claims and configures both SMs, but doesn't start them.
  void init(PIO pio, uint pin_rx, uint pin_tx) {
    pio_ = pio;
    pin_tx_ = pin_tx;
    tx_offset_ = (uint)pio_add_program(pio_, &iebus_tx_program);
    ack_offset_ = (uint)pio_add_program(pio_, &iebus_ack_program);
    tx_sm_ = (uint)pio_claim_unused_sm(pio_, true);
    ack_sm_ = (uint)pio_claim_unused_sm(pio_, true);

    // Drive the pad recessive *before* handing its function select to this PIO,
    // so the handover cannot glitch the bus dominant. Level and direction are
    // block-wide registers, so setting them through one SM covers both.
    pio_sm_set_pins_with_mask(pio_, tx_sm_, 1U << pin_tx, 1U << pin_tx);
    pio_sm_set_consecutive_pindirs(pio_, tx_sm_, pin_tx, 1, true);
    pio_gpio_init(pio_, pin_tx);

    pio_sm_config tx_cfg = iebus_tx_program_get_default_config(tx_offset_);
    sm_config_set_out_pins(&tx_cfg, pin_tx, 1);
    sm_config_set_set_pins(&tx_cfg, pin_tx, 1);
    // Both the bit value and the arbitration check come from the readback.
    sm_config_set_jmp_pin(&tx_cfg, pin_rx);
    sm_config_set_clkdiv(&tx_cfg, bus_clkdiv());

    // Entry is `reset`, not the program start -- offset 0 is handle_nak, which
    // would read the first word's top bit as the NAK flag.
    pio_sm_init(pio_, tx_sm_, tx_offset_ + iebus_tx_wrap_target, &tx_cfg);

    pio_sm_config ack_cfg = iebus_ack_program_get_default_config(ack_offset_);
    sm_config_set_sideset_pins(&ack_cfg, pin_tx);
    sm_config_set_clkdiv(&ack_cfg, bus_clkdiv());

    pio_sm_init(pio_, ack_sm_, ack_offset_, &ack_cfg);
    claimed_ = true;
  }

  PIO pio() const { return pio_; }
  uint32_t sm_mask() const { return (1U << tx_sm_) | (1U << ack_sm_); }

  // Must only be called between transactions to ensure the TX pin is left
  // high/recessive.
  void mute(bool mute) {
    pio_set_sm_mask_enabled(pio_, sm_mask(), !mute);
    muted_ = mute;
  }

  bool is_muted() const { return muted_; }

  // Hold the bus at a given level.
  // Overrides and restores mute state upon release (i.e. set recessive).
  void set_state(bool dominant) {
    static bool saved_mute = false;
    if (dominant) {
      saved_mute = muted_;
      mute(true);
      pio_sm_set_pins_with_mask(pio_, tx_sm_, 0U, 1U << pin_tx_);
    } else {
      pio_sm_set_pins_with_mask(pio_, tx_sm_, 1U << pin_tx_, 1U << pin_tx_);
      // The ack SM parks on `wait 1 irq`, which consumes a flag raised while it
      // was down and drives the slot immediately -- Phy::mute's ordering.
      pio_interrupt_clear(pio_, ack_irq);
      mute(saved_mute);
    }
  }

  // Send start and broadcast bits.
  Send send_header(bool is_unicast) {
    if (muted_)
      return MUTED;

    rx_.disarm_ack();

    // Jump to the tx_startbit section from the default "reset" stall on pull
    pio_sm_exec(pio_, tx_sm_,
                pio_encode_jmp(tx_offset_ + iebus_tx_offset_tx_startbit));

    // The broadcast bit carries no parity, so it is sent as a bare bit: length
    // 0 (one bit) with the parity slot standing in for the bit itself.
    return arbitrate(is_unicast ? 1U : 0U, 0);
  }

  Send send_controller_addr(uint16_t addr) {
    // Last field of the arbitration window; no acknowledge slot follows it.
    const Send err = arbitrate(addr, 12);
    if (err == Send{0}) {
      // We won: nobody else is transmitting, so the match can come back.
      rx_.rearm_ack();
      words_ = 0;
    }
    return err;
  }

  // Past arbitration a field is only queued, and its outcome left to
  // send_done. Once one has failed, the rest of the frame is dropped.
  Send send_field(size_t len, uint32_t bits, bool expect_ack) {
    if (muted_)
      return MUTED;
    if (!check())
      put(encode_tx(len, (uint16_t)bits, true, expect_ack));
    return Send{0};
  }

  Send send_done(uint8_t *data_index) {
    wait_done();
    check();
    const Fault fault = fault_;
    fault_ = Fault::None;

    if (fault == Fault::None)
      return Send{0};
    if (fault == Fault::Mismatch)
      return CONTENDED_BUS;
    switch (failed_word_) {
      case 0: return NAK_ADDRESS;
      case 1: return NAK_CONTROL;
      case 2: return NAK_MESSAGE_LENGTH;
      default: *data_index = (uint8_t)(failed_word_ - 3); return NAK_DATA;
    }
  }

private:
  // One field's word: [len:4][data+parity:len+1][has_ack:1][nak_ok:1][pad]. The
  // SM sends count + 1 bits, so the count is `len` and the parity bit the
  // driver appends rides along as the extra one. `has_ack` emits the
  // acknowledge slot; a NAK in it raises nak_irq unless `nak_ok`, i.e. unless
  // we don't `expect_ack`.
  static constexpr uint32_t encode_tx(size_t len, uint16_t bits, bool has_ack,
                                      bool expect_ack) {
    const auto n = (uint8_t)(len + 1);
    // Masked so a bit above the field (the broadcast bit's copy of itself, when
    // len is 0) can't spill into the count.
    const uint16_t with_parity =
        ((bits << 1) | parity(bits)) & ((1U << n) - 1U);
    uint32_t word = (uint32_t)len << 28;
    word |= with_parity << (28 - n);
    word |= (uint32_t)has_ack << (27 - n);
    word |= (uint32_t)!expect_ack << (26 - n);
    return word;
  }

  bool flagged() const {
    return pio_interrupt_get(pio_, iebus_tx_lost_arb_irq) ||
           pio_interrupt_get(pio_, iebus_tx_nak_irq);
  }

  // The SM has nothing left to do: every path through a word ends back on
  // `reset`'s pull. The pc test only counts once the FIFO is empty -- before
  // the SM takes a word it is still sitting on that same pull. A parked SM is
  // not idle; see flagged.
  bool idle() const {
    return pio_sm_is_tx_fifo_empty(pio_, tx_sm_) && // Read before the pc
           pio_sm_get_pc(pio_, tx_sm_) == tx_offset_ + iebus_tx_wrap_target;
  }

  void wait_done() {
    while (!(flagged() || idle()))
      tight_loop_contents();
  }

  // Collect a flag if the SM has raised one, without waiting. Returns whether
  // the frame has failed, now or earlier.
  bool check() {
    const bool lost_arb = pio_interrupt_get(pio_, iebus_tx_lost_arb_irq);
    if (!lost_arb && !pio_interrupt_get(pio_, iebus_tx_nak_irq))
      return fault_ != Fault::None;

    // Read the level before the clear throws it away, and clear before the
    // flag: releasing the SM with words still queued would send the rest of the
    // frame.
    failed_word_ =
        (uint8_t)(words_ - pio_sm_get_tx_fifo_level(pio_, tx_sm_) - 1U);
    pio_sm_clear_fifos(pio_, tx_sm_);

    if (lost_arb) {
      pio_interrupt_clear(pio_, iebus_tx_lost_arb_irq);
      // The winner's frame is still arriving; put the ack match back so we can
      // answer it if it turns out to be addressed to us.
      rx_.rearm_ack();
      fault_ = Fault::Mismatch;
    } else {
      pio_interrupt_clear(pio_, iebus_tx_nak_irq);
      fault_ = Fault::Nak;
    }
    return true;
  }

  // Only a full FIFO waits, and not on a parked SM: it never frees a slot,
  // which is why this isn't pio_sm_put_blocking.
  void put(uint32_t word) {
    while (pio_sm_is_tx_fifo_full(pio_, tx_sm_)) {
      if (check())
        return;
      tight_loop_contents();
    }
    pio_sm_put(pio_, tx_sm_, word);
    words_++;
  }

  // The arbitration window goes out a field at a time: a lost bid has to be
  // known before anything more is queued.
  Send arbitrate(uint32_t bits, uint8_t len) {
    if (muted_)
      return MUTED;
    put(encode_tx(len, (uint16_t)bits, false, false));
    wait_done();
    // Reported here and now, so nothing is left for send_done. With no ack slot
    // in these fields, the only fault is a lost bid.
    const bool lost = check();
    fault_ = Fault::None;
    return lost ? LOST_ARBITRATION : Send{0};
  }

  IEBusRx &rx_;

  PIO pio_;
  uint pin_tx_;
  uint tx_sm_;
  uint tx_offset_;
  uint ack_sm_;
  uint ack_offset_;
  bool claimed_ = false; // init() ran, so the destructor has something to undo

  bool muted_ = false;

  // Words put since arbitration was won, taken by the SM or not. Its flags park
  // it, which freezes the FIFO, so the word it failed on is the last one it
  // pulled: words_ - level - 1. Wrapping is harmless; the FIFO holds at most 8.
  uint8_t words_;
  uint8_t failed_word_;
  Fault fault_;
};

// The bus as a whole. Only it can hold the invariants that span the two
// engines: they sit on different PIOs and so must be started by one
// synchronised CTRL write, and muting has to reach both.
class Phy {
public:
  Phy() = default;
  Phy(const Phy &) = delete;
  Phy &operator=(const Phy &) = delete;

  ~Phy() { activity_leds_deinit(); }

  void init(uint16_t address) {
    rx_.init(pio0, IEBUS_RX, address);
    tx_.init(pio1, IEBUS_RX, IEBUS_TX);

    // Cross-PIO IRQ partners must share a clock divider *and* have it restarted
    // in the same cycle (S11.4). One CTRL write starts all three in step.
    // Reception runs from here on, independent of the main loop.
    pio_enable_sm_multi_mask_in_sync(rx_.pio(), 0, rx_.sm_mask(),
                                     tx_.sm_mask());

    activity_leds_init();
  }

  // "Muted" means we still listen but neither transmit nor ACK. Ordered so no
  // ack request can be stranded across the transition: the rx engine stops
  // asking before its driver goes away, and any flag it did leave behind is
  // dropped before that driver comes back -- the iebus_ack SM's `wait 1 irq`
  // clears an already-set flag and drives the slot immediately.
  void mute(bool mute) {
    muted_ = mute;
    if (mute) {
      rx_.mute(true);
      tx_.mute(true);
    } else {
      pio_interrupt_clear(tx_.pio(), ack_irq);
      tx_.mute(false);
      rx_.mute(false);
    }
  }

  bool is_muted() const { return muted_; }

  IEBusRx &rx() { return rx_; }
  IEBusTx &tx() { return tx_; }

private:
  // Point one mirror SM at one src -> dst pair. Only the destination gets
  // `pio_gpio_init`: taking the function select of a source pin would hand it
  // to this PIO block and cut whoever actually drives it (IEBUS_TX) loose.
  // Reading needs nothing but the pad's input buffer, which is on for both bus
  // pins already -- asserted here so the mirror cannot go dark if that changes.
  void pin_mirror_sm_init(uint sm, uint offset, uint src, uint dst) {
    PIO pio = tx_.pio();
    gpio_set_input_enabled(src, true);
    pio_gpio_init(pio, dst);
    pio_sm_set_consecutive_pindirs(pio, sm, dst, 1, true);

    pio_sm_config cfg = pin_mirror_program_get_default_config(offset);
    sm_config_set_in_pins(&cfg, src);
    sm_config_set_out_pins(&cfg, dst, 1);
    // Default clkdiv: one copy per system clock, so the LED tracks the line far
    // faster than a bit period.

    pio_sm_init(pio, sm, offset, &cfg);
    pio_sm_set_enabled(pio, sm, true);
  }

  // Initialize hardware to display bus TX/RX activity on two LEDs
  // The bus idles HIGH, so the PIO program mirrors inverted pin state from the
  // IEBUS TX/RX pins to the LED pins so that dominant bus activity (i.e. LOW
  // state for IEBUS TX/RX pins) lights the respective LED
  void activity_leds_init() {
    PIO pio = tx_.pio(); // iebus_rx fills its PIO
    // Indicators are cosmetic; never fail the bus bring-up for them.
    if (!pio_can_add_program(pio, &pin_mirror_program))
      return;
    const int msm_rx = pio_claim_unused_sm(pio, false);
    if (msm_rx < 0)
      return;
    const int msm_tx = pio_claim_unused_sm(pio, false);
    if (msm_tx < 0) {
      pio_sm_unclaim(pio, (uint)msm_rx);
      return;
    }

    led_offset_ = (uint)pio_add_program(pio, &pin_mirror_program);
    led_sm_rx_ = (uint)msm_rx;
    led_sm_tx_ = (uint)msm_tx;
    leds_claimed_ = true;
    pin_mirror_sm_init(led_sm_rx_, led_offset_, IEBUS_RX, LED_RX);
    pin_mirror_sm_init(led_sm_tx_, led_offset_, IEBUS_TX, LED_TX);
  }

  // The two mirrors share one copy of the program, so the slots go back
  // individually but the program memory only once -- which is why this isn't
  // two pio_remove_program_and_unclaim_sm calls.
  void activity_leds_deinit() {
    if (!leds_claimed_)
      return;
    PIO pio = tx_.pio();
    pio_sm_set_enabled(pio, led_sm_rx_, false);
    pio_sm_set_enabled(pio, led_sm_tx_, false);
    pio_sm_unclaim(pio, led_sm_rx_);
    pio_sm_unclaim(pio, led_sm_tx_);
    pio_remove_program(pio, &pin_mirror_program, led_offset_);
    leds_claimed_ = false;
  }

  IEBusRx rx_;
  IEBusTx tx_{rx_};
  bool muted_ = false;

  uint led_sm_rx_;
  uint led_sm_tx_;
  uint led_offset_;
  bool leds_claimed_ = false;
};

Phy phy;
} // namespace

extern "C" void phy_init(uint16_t address) { phy.init(address); }

extern "C" void phy_mute(bool mute) { phy.mute(mute); }

extern "C" bool phy_is_muted() { return phy.is_muted(); }

extern "C" void phy_deafen(bool deaf) { phy.rx().deafen(deaf); }

extern "C" bool phy_frame_pending() { return phy.rx().frame_pending(); }

extern "C" void phy_guard_enter() {}
extern "C" void phy_guard_leave() {}

// --- Reads: served from the buffered frame ----------------------------------
//
// The engine has already checked every parity bit and driven every ack slot, so
// these only hand back fields. The error it recorded is reported by the first
// call of the frame; the caller abandons the frame on it, which releases it.

extern "C" Read phy_read_header(bool *is_unicast) {
  const IEBusRx::RxFrame &frame = phy.rx().frame();
  if (frame.err != Read{0}) {
    phy.rx().release();
    return frame.err;
  }
  *is_unicast = frame.is_unicast;
  return Read{0};
}

extern "C" Read phy_read_controller_addr(uint16_t *addr) {
  *addr = phy.rx().frame().controller_addr;
  return Read{0};
}

extern "C" Read phy_read_peripheral_addr(uint16_t *addr) {
  *addr = phy.rx().frame().peripheral_addr;
  return Read{0};
}

extern "C" Read phy_read_control(uint8_t *control) {
  *control = phy.rx().frame().control;
  return Read{0};
}

extern "C" Read phy_read_length(uint8_t *length) {
  *length = phy.rx().frame().length;
  return Read{0};
}

extern "C" Read phy_read_data(uint8_t *data) {
  static uint8_t idx;
  const IEBusRx::RxFrame &frame = phy.rx().frame();
  if (idx >= frame.length)
    idx = 0;
  *data = frame.data[idx++];
  if (idx >= frame.length) { // Frame consumed
    idx = 0;
    phy.rx().release();
  }
  return Read{0};
}

extern "C" Send phy_send_header(bool is_unicast) {
  return phy.tx().send_header(is_unicast);
}

extern "C" Send phy_send_controller_addr(uint16_t addr) {
  return phy.tx().send_controller_addr(addr);
}

extern "C" Send phy_send_peripheral_addr(uint16_t addr, bool expect_ack) {
  return phy.tx().send_field(12, addr, expect_ack);
}

extern "C" Send phy_send_control(uint8_t control, bool expect_ack) {
  return phy.tx().send_field(4, control, expect_ack);
}

extern "C" Send phy_send_length(uint8_t length, bool expect_ack) {
  return phy.tx().send_field(8, length, expect_ack);
}

extern "C" Send phy_send_data(uint8_t data, bool expect_ack) {
  return phy.tx().send_field(8, data, expect_ack);
}

extern "C" Send phy_send_done(uint8_t *data_index) {
  return phy.tx().send_done(data_index);
}

#ifndef NDEBUG

extern "C" void phy_set_dominant() { phy.tx().set_state(true); }
extern "C" void phy_set_recessive() { phy.tx().set_state(false); }

  #ifdef MEASURE_BUS
// Sample and dump bus bit timing over the serial link (REPL `M`).
void phy_measure(void) {};
  #endif
#endif
