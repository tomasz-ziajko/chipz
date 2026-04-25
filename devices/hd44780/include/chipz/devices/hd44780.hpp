// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_DEVICES_HD44780_HPP
#define CHIPZ_DEVICES_HD44780_HPP

#include <chipz/core/chip.hpp>
#include <cstdint>
#include <string>

namespace chipz {
namespace devices {

/**
 * @brief Driver for HD44780 LCD Controller
 *
 * Drives an HD44780-compatible LCD in 4-bit mode through any CommunicationInterface
 * that supports the timed transmit overload — typically a ParallelInterface<6>.
 *
 * All bus timing is delegated to the interface's CompletionSources, so the driver is
 * completely agnostic to whether the bus is direct GPIO or an I2C GPIO expander:
 *
 *   - Direct GPIO + TimerCompletionSource: timer enforces E-pulse and command delays
 *   - I2C expander + ExternalCompletionSource: driver waits for each I2C transaction
 *   - Both combined: barrier fires when the slower of the two completes
 *
 * Expected bus bit layout (ParallelInterface<6> or equivalent):
 *   bits [3:0]  D4–D7 (nibble data, high nibble transmitted first)
 *   bit  4      RS    (0 = command register, 1 = character data)
 *   bit  5      E     (enable; HD44780 latches data on falling edge)
 *
 * Scheduling:
 *   Uninit      — first run() returns delayMs(DELAY_INIT_MS) for power-on settle.
 *   Initializing — each run() advances one nibble/byte phase, suspends on comm.
 *   Idle        — suspends on WaitCondition::demand(); call Core::wake(*this)
 *                 after writeBuffer() / writeBufferAtPosition() to resume.
 *   Transfer    — each run() advances one nibble/byte phase, suspends on comm.
 *
 * @tparam CommInterface Communication interface type (must support timed transmit)
 */
template <typename CommInterface>
class HD44780 : public Chip<CommInterface> {
    public:
    enum class DisplaySize {
        Size16x2,
        Size20x2,
        Size20x4,
        Size40x2
    };

    struct Config {
        DisplaySize size;
        bool        cursorVisible;
        bool        cursorBlink;
    };

    /**
     * @brief Construct HD44780 driver
     *
     * @param comm   Interface wired per bus bit layout described above
     * @param config Display geometry and cursor settings
     */
    HD44780(CommInterface& comm, const Config& config) :
        Chip<CommInterface>(comm),
        config_(config),
        status_(ChipBase::Status::Uninitialized),
        transfer_state_(TransferState::Idle),
        init_step_(0),
        current_byte_(0),
        single_nibble_value_(0),
        current_rs_(false),
        past_init_(false),
        last_transfer_ok_(false),
        buffer_write_in_progress_(false),
        buffer_write_index_(0),
        buffer_write_total_(0),
        buffer_ptr_(nullptr),
        buffer_write_partial_(false),
        buffer_write_start_pos_(0),
        buffer_write_length_(0),
        buffer_write_chars_sent_(0),
        buffer_write_need_cursor_(false)
    {
        switch (config_.size) {
            case DisplaySize::Size16x2:
                rows_    = 2;
                columns_ = 16;
                break;
            case DisplaySize::Size20x2:
                rows_    = 2;
                columns_ = 20;
                break;
            case DisplaySize::Size20x4:
                rows_    = 4;
                columns_ = 20;
                break;
            case DisplaySize::Size40x2:
                rows_    = 2;
                columns_ = 40;
                break;
        }
    }

    bool initialize() override
    {
        if (!this->get<CommInterface>().isReady()) {
            status_ = ChipBase::Status::Error;
            return false;
        }
        transfer_state_           = TransferState::Idle;
        init_step_                = 0;
        past_init_                = false;
        last_transfer_ok_         = false;
        current_byte_             = 0;
        current_rs_               = false;
        single_nibble_value_      = 0;
        buffer_write_in_progress_ = false;
        buffer_write_index_       = 0;
        buffer_write_total_       = 0;
        buffer_ptr_               = nullptr;
        buffer_write_partial_     = false;
        buffer_write_start_pos_   = 0;
        buffer_write_length_      = 0;
        buffer_write_chars_sent_  = 0;
        buffer_write_need_cursor_ = false;
        status_                   = ChipBase::Status::Ready;
        return true;
    }

    bool reset() override
    {
        status_ = ChipBase::Status::Uninitialized;
        return initialize();
    }

    bool isReady() const override
    {
        return status_ == ChipBase::Status::Ready && this->get<CommInterface>().isReady() && past_init_ &&
               !buffer_write_in_progress_;
    }

    ChipBase::Status getStatus() const override
    {
        return status_;
    }

    std::string getDeviceId() const override
    {
        return "HD44780 LCD";
    }

    bool main() override
    {
        return true;
    }

    DriverTask run() override
    {
        co_yield WaitCondition::delayMs(DELAY_INIT_MS);

        if (status_ != ChipBase::Status::Ready) {
            while (true) {
                co_yield WaitCondition::demand();
            }
        }

        // Initialization sequence
        handleInitializingState();  // starts first nibble
        while (!past_init_) {
            co_yield WaitCondition::comm(this->get<CommInterface>());
            if (!last_transfer_ok_) {
                while (true) {
                    co_yield WaitCondition::demand();
                }
            }
            handleInitializingState();
        }

        // Main loop: demand until writeBuffer/writeBufferAtPosition, then drive transfer
        while (true) {
            co_yield WaitCondition::demand();
            if (status_ != ChipBase::Status::Ready) {
                continue;
            }
            handleTransferState();  // kicks off first nibble of first byte
            while (buffer_write_in_progress_ || transfer_state_ != TransferState::Idle) {
                co_yield WaitCondition::comm(this->get<CommInterface>());
                if (!last_transfer_ok_) {
                    break;
                }
                handleTransferState();
            }
        }
    }

    /**
     * @brief Write the full display buffer (rows × columns characters)
     *
     * @param buffer Pointer to character data; caller must keep it alive
     *               until isReady() returns true.
     * @return true if write started, false if busy
     */
    bool writeBuffer(const char* buffer)
    {
        if (!past_init_ || buffer_write_in_progress_) {
            return false;
        }
        buffer_ptr_               = buffer;
        buffer_write_index_       = 0;
        buffer_write_total_       = static_cast<uint16_t>(rows_) * (columns_ + 1u);
        buffer_write_in_progress_ = true;
        buffer_write_partial_     = false;
        transfer_state_           = TransferState::Idle;
        return true;
    }

    /**
     * @brief Write a partial buffer at a specific screen position
     *
     * @param buffer   Data to write; caller must keep it alive until isReady()
     * @param position Linear position (0 = top-left corner)
     * @param length   Number of characters to write
     * @return true if write started, false if busy or parameters out of range
     */
    bool writeBufferAtPosition(const char* buffer, uint16_t position, uint16_t length)
    {
        if (!past_init_ || buffer_write_in_progress_) {
            return false;
        }
        uint16_t total = static_cast<uint16_t>(rows_) * columns_;
        if (position >= total || length == 0 || position + length > total) {
            return false;
        }
        buffer_ptr_               = buffer;
        buffer_write_start_pos_   = position;
        buffer_write_length_      = length;
        buffer_write_chars_sent_  = 0;
        buffer_write_need_cursor_ = true;
        buffer_write_in_progress_ = true;
        buffer_write_partial_     = true;
        transfer_state_           = TransferState::Idle;
        return true;
    }

    private:
    // -------------------------------------------------------------------------
    // State machine
    // -------------------------------------------------------------------------

    enum class TransferState {
        Idle,
        HighNibbleEHigh,    ///< High nibble sent with E=1, awaiting completion
        HighNibbleELow,     ///< High nibble sent with E=0, awaiting completion
        LowNibbleEHigh,     ///< Low nibble sent with E=1, awaiting completion
        LowNibbleELow,      ///< Low nibble sent with E=0, awaiting completion
        SingleNibbleEHigh,  ///< Single nibble sent with E=1 (init only)
        SingleNibbleELow    ///< Single nibble sent with E=0 (init only)
    };

    // -------------------------------------------------------------------------
    // Timing constants
    // -------------------------------------------------------------------------

    /// Power-on delay requested through Core's defer_ms (min 15 ms per datasheet)
    static constexpr uint32_t DELAY_INIT_MS = 15;

    /// E=1 pulse width (min 230 ns; 1 µs is well within spec)
    static constexpr uint32_t DELAY_E_HIGH_US = 1;

    /// E=0 hold + normal command execution time (min 37 µs; 50 µs with margin)
    static constexpr uint32_t DELAY_E_LOW_US = 50;

    /// Clear Display / Return Home execution time (min 1.52 ms)
    static constexpr uint32_t DELAY_CLEAR_US = 2000;

    // -------------------------------------------------------------------------
    // HD44780 command bytes and flags
    // -------------------------------------------------------------------------

    static constexpr uint8_t CMD_CLEAR_DISPLAY   = 0x01;
    static constexpr uint8_t CMD_RETURN_HOME     = 0x02;
    static constexpr uint8_t CMD_ENTRY_MODE_SET  = 0x04;
    static constexpr uint8_t CMD_DISPLAY_CONTROL = 0x08;
    static constexpr uint8_t CMD_FUNCTION_SET    = 0x20;
    static constexpr uint8_t CMD_SET_DDRAM_ADDR  = 0x80;

    static constexpr uint8_t ENTRY_INCREMENT = 0x02;
    static constexpr uint8_t ENTRY_SHIFT_OFF = 0x00;
    static constexpr uint8_t DISPLAY_ON      = 0x04;
    static constexpr uint8_t CURSOR_ON       = 0x02;
    static constexpr uint8_t CURSOR_OFF      = 0x00;
    static constexpr uint8_t BLINK_ON        = 0x01;
    static constexpr uint8_t BLINK_OFF       = 0x00;
    static constexpr uint8_t BIT4_MODE       = 0x00;
    static constexpr uint8_t LINE2           = 0x08;
    static constexpr uint8_t DOTS_5x8        = 0x00;

    static constexpr uint8_t ROW1_ADDR = 0x00;
    static constexpr uint8_t ROW2_ADDR = 0x40;
    static constexpr uint8_t ROW3_ADDR = 0x14;
    static constexpr uint8_t ROW4_ADDR = 0x54;

    // -------------------------------------------------------------------------
    // Members
    // -------------------------------------------------------------------------

    Config           config_;
    ChipBase::Status status_;
    TransferState    transfer_state_;
    uint8_t          rows_{};
    uint8_t          columns_{};

    uint8_t init_step_;
    uint8_t current_byte_;
    uint8_t single_nibble_value_;
    bool    current_rs_;
    bool    past_init_;
    bool    last_transfer_ok_;

    bool        buffer_write_in_progress_;
    uint16_t    buffer_write_index_;
    uint16_t    buffer_write_total_;
    const char* buffer_ptr_;
    bool        buffer_write_partial_;
    uint16_t    buffer_write_start_pos_;
    uint16_t    buffer_write_length_;
    uint16_t    buffer_write_chars_sent_;
    bool        buffer_write_need_cursor_;

    // -------------------------------------------------------------------------
    // Transfer complete callback
    // -------------------------------------------------------------------------

    void onTransferComplete(CommunicationInterface& /*which*/, bool success) override
    {
        last_transfer_ok_ = success;
        if (!success) {
            status_ = ChipBase::Status::Error;
        }
    }

    // -------------------------------------------------------------------------
    // Bus helpers
    // -------------------------------------------------------------------------

    /**
     * @brief Pack nibble, RS, and E flag into the 6-bit bus value
     *
     *   bits [3:0] = nibble (D4–D7)
     *   bit  4     = RS
     *   bit  5     = E
     */
    static constexpr uint8_t busVal(uint8_t nibble, bool rs, bool e) noexcept
    {
        return (nibble & 0x0Fu) | (rs ? 0x10u : 0u) | (e ? 0x20u : 0u);
    }

    void sendBusValue(uint8_t val, uint32_t duration_us)
    {
        this->template transmit<CommInterface>(&val, 1u, duration_us);
    }

    /// Start a full byte transfer (high nibble first, 4-bit mode)
    void startByte(uint8_t byte, bool rs)
    {
        current_byte_ = byte;
        current_rs_   = rs;
        sendBusValue(busVal(static_cast<uint8_t>((byte >> 4u) & 0x0Fu), rs, true), DELAY_E_HIGH_US);
        transfer_state_ = TransferState::HighNibbleEHigh;
    }

    /// Start a single-nibble transfer (used during init to switch bus width)
    void startSingleNibble(uint8_t nibble)
    {
        single_nibble_value_ = nibble;
        sendBusValue(busVal(nibble, false, true), DELAY_E_HIGH_US);
        transfer_state_ = TransferState::SingleNibbleEHigh;
    }

    // -------------------------------------------------------------------------
    // Nibble-phase state machine
    //
    // Advances one phase per call — always exactly one bus write.
    // Returns true  → wrote something, caller should return and wait.
    // Returns false → this byte/nibble is complete, caller should advance.
    //
    // post_byte_delay_us is the duration applied to the final E=0 write of a
    // full byte; it encodes the command execution time for that byte.
    // -------------------------------------------------------------------------

    bool advanceNibblePhase(uint32_t post_byte_delay_us)
    {
        switch (transfer_state_) {
            case TransferState::HighNibbleEHigh:
                sendBusValue(busVal(static_cast<uint8_t>((current_byte_ >> 4u) & 0x0Fu), current_rs_, false),
                             DELAY_E_HIGH_US);
                transfer_state_ = TransferState::HighNibbleELow;
                return true;

            case TransferState::HighNibbleELow:
                sendBusValue(busVal(static_cast<uint8_t>(current_byte_ & 0x0Fu), current_rs_, true), DELAY_E_HIGH_US);
                transfer_state_ = TransferState::LowNibbleEHigh;
                return true;

            case TransferState::LowNibbleEHigh:
                sendBusValue(busVal(static_cast<uint8_t>(current_byte_ & 0x0Fu), current_rs_, false),
                             post_byte_delay_us);
                transfer_state_ = TransferState::LowNibbleELow;
                return true;

            case TransferState::LowNibbleELow:
                transfer_state_ = TransferState::Idle;
                return false;  // byte complete

            case TransferState::SingleNibbleEHigh:
                sendBusValue(busVal(single_nibble_value_, false, false), DELAY_E_LOW_US);
                transfer_state_ = TransferState::SingleNibbleELow;
                return true;

            case TransferState::SingleNibbleELow:
                transfer_state_ = TransferState::Idle;
                return false;  // nibble complete

            default:
                return false;
        }
    }

    // -------------------------------------------------------------------------
    // Initialization state handler
    // -------------------------------------------------------------------------

    void handleInitializingState()
    {
        if (transfer_state_ != TransferState::Idle) {
            // Clear Display and Return Home need the longer execution delay.
            uint32_t post_delay = (current_byte_ == CMD_CLEAR_DISPLAY || current_byte_ == CMD_RETURN_HOME)
                                      ? DELAY_CLEAR_US
                                      : DELAY_E_LOW_US;
            if (advanceNibblePhase(post_delay)) {
                return;
            }
            init_step_++;
        }
        handleInitStep();
    }

    void handleInitStep()
    {
        switch (init_step_) {
            // Steps 0–2: send nibble 0x03 (function-set in 8-bit mode) × 3
            case 0:
            case 1:
            case 2:
                startSingleNibble(0x03u);
                break;

            // Step 3: send nibble 0x02 (switch to 4-bit mode)
            case 3:
                startSingleNibble(0x02u);
                break;

            // Step 4: function set — 4-bit, 2+ lines, 5×8 dots
            case 4:
                startByte(CMD_FUNCTION_SET | BIT4_MODE | LINE2 | DOTS_5x8, false);
                break;

            // Step 5: display control — apply cursor/blink from config
            case 5: {
                uint8_t ctrl = CMD_DISPLAY_CONTROL | DISPLAY_ON | (config_.cursorVisible ? CURSOR_ON : CURSOR_OFF) |
                               (config_.cursorBlink ? BLINK_ON : BLINK_OFF);
                startByte(ctrl, false);
                break;
            }

            // Step 6: clear display (uses DELAY_CLEAR_US via advanceNibblePhase)
            case 6:
                startByte(CMD_CLEAR_DISPLAY, false);
                break;

            // Step 7: entry mode — increment cursor, no display shift
            case 7:
                startByte(CMD_ENTRY_MODE_SET | ENTRY_INCREMENT | ENTRY_SHIFT_OFF, false);
                break;

            // Initialization complete
            default:
                past_init_ = true;
                break;
        }
    }

    // -------------------------------------------------------------------------
    // Transfer (display write) state handler
    // -------------------------------------------------------------------------

    void handleTransferState()
    {
        if (transfer_state_ != TransferState::Idle) {
            if (advanceNibblePhase(DELAY_E_LOW_US)) {
                return;
            }
            // byte complete — fall through to load next
        }
        handleTransferIdle();
    }

    void handleTransferIdle()
    {
        if (!buffer_write_in_progress_) {
            return;
        }
        if (buffer_write_partial_) {
            handlePartialBufferWrite();
        }
        else {
            handleFullBufferWrite();
        }
    }

    void handlePartialBufferWrite()
    {
        if (buffer_write_chars_sent_ >= buffer_write_length_) {
            buffer_write_in_progress_ = false;
            return;
        }

        if (buffer_write_need_cursor_) {
            uint16_t      pos       = buffer_write_start_pos_ + buffer_write_chars_sent_;
            uint8_t       row       = static_cast<uint8_t>(pos / columns_);
            uint8_t       col       = static_cast<uint8_t>(pos % columns_);
            const uint8_t offsets[] = {ROW1_ADDR, ROW2_ADDR, ROW3_ADDR, ROW4_ADDR};
            startByte(CMD_SET_DDRAM_ADDR | static_cast<uint8_t>(offsets[row] + col), false);
            buffer_write_need_cursor_ = false;
        }
        else {
            startByte(static_cast<uint8_t>(buffer_ptr_[buffer_write_chars_sent_]), true);
            buffer_write_chars_sent_++;

            if (buffer_write_chars_sent_ < buffer_write_length_) {
                uint16_t prev_row = (buffer_write_start_pos_ + buffer_write_chars_sent_ - 1u) / columns_;
                uint16_t next_row = (buffer_write_start_pos_ + buffer_write_chars_sent_) / columns_;
                if (next_row != prev_row) {
                    buffer_write_need_cursor_ = true;
                }
            }

            if (buffer_write_chars_sent_ >= buffer_write_length_) {
                buffer_write_in_progress_ = false;
            }
        }
    }

    void handleFullBufferWrite()
    {
        if (buffer_write_index_ >= buffer_write_total_) {
            buffer_write_in_progress_ = false;
            return;
        }

        uint16_t chars_written = buffer_write_index_ % (columns_ + 1u);

        if (chars_written == 0u) {
            const uint8_t offsets[] = {ROW1_ADDR, ROW2_ADDR, ROW3_ADDR, ROW4_ADDR};
            uint8_t       row       = static_cast<uint8_t>(buffer_write_index_ / (columns_ + 1u));
            startByte(CMD_SET_DDRAM_ADDR | offsets[row], false);
        }
        else {
            uint16_t char_idx =
                static_cast<uint16_t>((buffer_write_index_ / (columns_ + 1u)) * columns_ + (chars_written - 1u));
            startByte(static_cast<uint8_t>(buffer_ptr_[char_idx]), true);
        }

        buffer_write_index_++;
        if (buffer_write_index_ >= buffer_write_total_) {
            buffer_write_in_progress_ = false;
        }
    }
};

}  // namespace devices
}  // namespace chipz

#endif  // CHIPZ_DEVICES_HD44780_HPP
