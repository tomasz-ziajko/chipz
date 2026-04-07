#ifndef CHIPZ_DEVICES_HD44780_HPP
#define CHIPZ_DEVICES_HD44780_HPP

#include "../peripheral.hpp"
#include "../concepts.hpp"
#include <cstdint>
#include <string>
#include <functional>

namespace chipz {
namespace devices {

/**
 * @brief Driver for HD44780 LCD Controller
 *
 * The HD44780 is a popular LCD controller used in character LCDs.
 * This implementation supports 4-bit mode via GPIO expander (e.g., PCF8574).
 *
 * This implementation mirrors the state machine design from the C version,
 * providing full compatibility with embedded systems requirements.
 *
 * @tparam CommInterface Communication interface type (typically GPIO)
 */
template<chipz::concepts::CommunicationInterface CommInterface>
class HD44780 : public Peripheral<CommInterface> {
public:
    enum class InterfaceMode {
        Bit4,
        Bit8
    };

    enum class DisplaySize {
        Size16x2,
        Size20x2,
        Size20x4,
        Size40x2
    };

    struct Config {
        InterfaceMode mode;
        DisplaySize size;
        bool cursorVisible;
        bool cursorBlink;
    };

    /**
     * @brief Construct HD44780 driver with communication interface and configuration
     * @param comm Reference to communication interface (GPIO)
     * @param config Display configuration
     * @param get_tick Function to get current system tick in milliseconds
     */
    HD44780(CommInterface& comm,
            const Config& config,
            std::function<uint32_t()> get_tick = nullptr,
            std::function<void(uint8_t d4_d7, bool rs, bool e)> update_pins = nullptr)
        : Peripheral<CommInterface>(comm)
        , config_(config)
        , status_(Status::Uninitialized)
        , state_(State::Uninit)
        , transfer_state_(TransferState::Idle)
        , tick_timer_(0)
        , last_tick_(0)
        , delay_(0)
        , init_step_(0)
        , transfer_complete_(true)
        , current_byte_(0)
        , single_nibble_value_(0)
        , current_rs_(false)
        , buffer_write_in_progress_(false)
        , buffer_write_index_(0)
        , buffer_write_total_(0)
        , buffer_ptr_(nullptr)
        , buffer_write_partial_(false)
        , buffer_write_start_pos_(0)
        , buffer_write_length_(0)
        , buffer_write_chars_sent_(0)
        , buffer_write_need_cursor_(false)
        , get_tick_(get_tick)
        , update_pins_(update_pins)
    {
        // Get rows and columns from config
        switch (config_.size) {
            case DisplaySize::Size16x2:
                rows_ = 2;
                columns_ = 16;
                break;
            case DisplaySize::Size20x2:
                rows_ = 2;
                columns_ = 20;
                break;
            case DisplaySize::Size20x4:
                rows_ = 4;
                columns_ = 20;
                break;
            case DisplaySize::Size40x2:
                rows_ = 2;
                columns_ = 40;
                break;
        }

        // Set up GPIO completion callback
        comm_.setTransferCompleteCallback(
            [this](bool success) { this->onTransferComplete(success); }
        );
    }

    // Peripheral interface implementation
    bool initialize() override {
        if (!comm_.isReady()) {
            status_ = Status::Error;
            return false;
        }

        // Reset state machine
        state_ = State::Uninit;
        transfer_state_ = TransferState::Idle;
        tick_timer_ = 0;
        delay_ = 0;
        init_step_ = 0;
        transfer_complete_ = true;
        current_byte_ = 0;
        current_rs_ = false;
        single_nibble_value_ = 0;
        buffer_write_in_progress_ = false;
        buffer_write_index_ = 0;
        buffer_write_total_ = 0;
        buffer_ptr_ = nullptr;
        buffer_write_partial_ = false;
        buffer_write_start_pos_ = 0;
        buffer_write_length_ = 0;
        buffer_write_chars_sent_ = 0;
        buffer_write_need_cursor_ = false;

        if (get_tick_) {
            last_tick_ = get_tick_();
        }

        status_ = Status::Ready;
        return true;
    }

    bool reset() override {
        status_ = Status::Uninitialized;
        return initialize();
    }

    bool isReady() const override {
        return status_ == Status::Ready && comm_.isReady() && state_ == State::Idle;
    }

    Status getStatus() const override {
        return status_;
    }

    std::string getDeviceId() const override {
        return "HD44780 LCD";
    }

    bool main() override {
        if (status_ != Status::Ready) {
            return false;
        }

        // Calculate elapsed time since last call
        if (get_tick_) {
            uint32_t current_tick = get_tick_();
            uint32_t elapsed_ms = current_tick - last_tick_;
            last_tick_ = current_tick;

            // Update tick timer
            tick_timer_ += elapsed_ms;

            // Decrement delay timer
            if (delay_ > elapsed_ms) {
                delay_ -= elapsed_ms;
            } else {
                delay_ = 0;
            }
        }

        // Wait for transfer to complete
        if (!transfer_complete_) {
            return true;
        }

        // Wait for delay before processing
        if (delay_ > 0) {
            return true;
        }

        // Main state machine
        switch (state_) {
            case State::Uninit:
                // Wait for power-on delay
                if (tick_timer_ >= DELAY_INIT) {
                    state_ = State::Initializing;
                    init_step_ = 0;
                    transfer_state_ = TransferState::Idle;
                }
                break;

            case State::Initializing:
                handleInitializingState();
                break;

            case State::Transfer:
                handleTransferState();
                break;

            case State::Idle:
                // Just waiting, nothing to do
                break;
        }

        return true;
    }

    // HD44780-specific interface

    /**
     * @brief Write full buffer to display
     * @param buffer Pointer to buffer (rows * columns bytes)
     * @return true if write started successfully, false if busy
     */
    bool writeBuffer(const char* buffer) {
        if (state_ != State::Idle || buffer_write_in_progress_) {
            return false;
        }

        buffer_ptr_ = buffer;
        buffer_write_index_ = 0;
        buffer_write_total_ = rows_ * (columns_ + 1);
        buffer_write_in_progress_ = true;
        buffer_write_partial_ = false;

        state_ = State::Transfer;
        transfer_state_ = TransferState::Idle;
        delay_ = 0;

        return true;
    }

    /**
     * @brief Write partial buffer at specific position
     * @param buffer Pointer to buffer
     * @param position Linear position (0 to rows*columns-1)
     * @param length Number of characters to write
     * @return true if write started successfully, false otherwise
     */
    bool writeBufferAtPosition(const char* buffer, uint16_t position, uint16_t length) {
        if (state_ != State::Idle || buffer_write_in_progress_) {
            return false;
        }

        uint16_t total_screen_size = rows_ * columns_;
        if (position >= total_screen_size || length == 0) {
            return false;
        }

        if (position + length > total_screen_size) {
            return false;
        }

        buffer_ptr_ = buffer;
        buffer_write_start_pos_ = position;
        buffer_write_length_ = length;
        buffer_write_chars_sent_ = 0;
        buffer_write_need_cursor_ = true;
        buffer_write_in_progress_ = true;
        buffer_write_partial_ = true;

        state_ = State::Transfer;
        transfer_state_ = TransferState::Idle;
        delay_ = 0;

        return true;
    }

private:
    enum class State {
        Uninit,       // Uninitialized state
        Initializing, // Performing initialization sequence
        Idle,         // Idle, ready for commands
        Transfer      // Transferring data to display
    };

    enum class TransferState {
        Idle,
        HighNibbleEHigh,
        HighNibbleELow,
        LowNibbleEHigh,
        LowNibbleELow,
        SingleNibbleEHigh,
        SingleNibbleELow
    };

    Config config_;
    Status status_;
    State state_;
    TransferState transfer_state_;

    // Display size
    uint8_t rows_;
    uint8_t columns_;

    // Timing
    uint32_t tick_timer_;
    uint32_t last_tick_;
    uint32_t delay_;
    std::function<uint32_t()> get_tick_;

    // Initialization
    uint8_t init_step_;
    bool transfer_complete_;

    // Current transfer data
    uint8_t current_byte_;
    uint8_t single_nibble_value_;
    bool current_rs_;

    // Buffer write state
    bool buffer_write_in_progress_;
    uint16_t buffer_write_index_;
    uint16_t buffer_write_total_;
    const char* buffer_ptr_;

    // Partial buffer write state
    bool buffer_write_partial_;
    uint16_t buffer_write_start_pos_;
    uint16_t buffer_write_length_;
    uint16_t buffer_write_chars_sent_;
    bool buffer_write_need_cursor_;

    // Pin update function
    std::function<void(uint8_t d4_d7, bool rs, bool e)> update_pins_;

    // HD44780 Commands
    static constexpr uint8_t CMD_CLEAR_DISPLAY = 0x01;
    static constexpr uint8_t CMD_RETURN_HOME = 0x02;
    static constexpr uint8_t CMD_ENTRY_MODE_SET = 0x04;
    static constexpr uint8_t CMD_DISPLAY_CONTROL = 0x08;
    static constexpr uint8_t CMD_CURSOR_SHIFT = 0x10;
    static constexpr uint8_t CMD_FUNCTION_SET = 0x20;
    static constexpr uint8_t CMD_SET_CGRAM_ADDR = 0x40;
    static constexpr uint8_t CMD_SET_DDRAM_ADDR = 0x80;

    // Timing requirements (in milliseconds)
    static constexpr uint32_t DELAY_INIT = 15;
    static constexpr uint32_t DELAY_LONG_CMD = 2;
    static constexpr uint32_t DELAY_SHORT_CMD = 0;

    // Entry Mode flags
    static constexpr uint8_t ENTRY_INCREMENT = 0x02;
    static constexpr uint8_t ENTRY_DECREMENT = 0x00;
    static constexpr uint8_t ENTRY_SHIFT_ON = 0x01;
    static constexpr uint8_t ENTRY_SHIFT_OFF = 0x00;

    // Display Control flags
    static constexpr uint8_t DISPLAY_ON = 0x04;
    static constexpr uint8_t DISPLAY_OFF = 0x00;
    static constexpr uint8_t CURSOR_ON = 0x02;
    static constexpr uint8_t CURSOR_OFF = 0x00;
    static constexpr uint8_t BLINK_ON = 0x01;
    static constexpr uint8_t BLINK_OFF = 0x00;

    // Function Set flags
    static constexpr uint8_t BIT8_MODE = 0x10;
    static constexpr uint8_t BIT4_MODE = 0x00;
    static constexpr uint8_t LINE2 = 0x08;
    static constexpr uint8_t LINE1 = 0x00;
    static constexpr uint8_t DOTS_5x10 = 0x04;
    static constexpr uint8_t DOTS_5x8 = 0x00;

    // DDRAM addresses for rows
    static constexpr uint8_t ROW1_ADDR = 0x00;
    static constexpr uint8_t ROW2_ADDR = 0x40;
    static constexpr uint8_t ROW3_ADDR = 0x14;
    static constexpr uint8_t ROW4_ADDR = 0x54;

    /**
     * @brief GPIO transfer completion callback
     * @param success True if transfer succeeded, false on error
     */
    void onTransferComplete(bool success) {
        if (!success) {
            status_ = Status::Error;
            state_ = State::Idle;
            return;
        }

        transfer_complete_ = true;
    }

    /**
     * @brief Handle initialization state
     */
    void handleInitializingState() {
        switch (transfer_state_) {
            case TransferState::Idle:
                handleInitStep();
                break;

            case TransferState::HighNibbleELow:
                {
                    uint8_t nibble = (current_byte_ >> 4) & 0x0F;
                    transfer_complete_ = false;
                    if (update_pins_) {
                        update_pins_(nibble, current_rs_, false);
                    }
                    transfer_state_ = TransferState::LowNibbleEHigh;
                }
                break;

            case TransferState::LowNibbleEHigh:
                {
                    uint8_t nibble = current_byte_ & 0x0F;
                    transfer_complete_ = false;
                    if (update_pins_) {
                        update_pins_(nibble, current_rs_, true);
                    }
                    transfer_state_ = TransferState::LowNibbleELow;
                }
                break;

            case TransferState::LowNibbleELow:
                {
                    uint8_t nibble = current_byte_ & 0x0F;
                    transfer_complete_ = false;
                    if (update_pins_) {
                        update_pins_(nibble, current_rs_, false);
                    }
                    transfer_state_ = TransferState::Idle;

                    // Set delay based on command
                    if (current_byte_ == CMD_CLEAR_DISPLAY || current_byte_ == CMD_RETURN_HOME) {
                        delay_ = DELAY_LONG_CMD;
                    } else {
                        delay_ = DELAY_SHORT_CMD;
                    }
                    init_step_++;
                }
                break;

            case TransferState::SingleNibbleELow:
                transfer_complete_ = false;
                if (update_pins_) {
                    update_pins_(single_nibble_value_, false, false);
                }
                transfer_state_ = TransferState::Idle;
                delay_ = DELAY_SHORT_CMD;
                init_step_++;
                break;

            default:
                transfer_state_ = TransferState::Idle;
                break;
        }
    }

    /**
     * @brief Handle individual initialization steps
     */
    void handleInitStep() {
        switch (init_step_) {
            case 0:
            case 1:
            case 2:
                // First three: Function set (8-bit mode) - single nibble 0x03
                single_nibble_value_ = 0x03;
                transfer_complete_ = false;
                if (update_pins_) {
                    update_pins_(single_nibble_value_, false, true);
                }
                transfer_state_ = TransferState::SingleNibbleELow;
                break;

            case 3:
                // Fourth: Function set (4-bit mode) - single nibble 0x02
                single_nibble_value_ = 0x02;
                transfer_complete_ = false;
                if (update_pins_) {
                    update_pins_(single_nibble_value_, false, true);
                }
                transfer_state_ = TransferState::SingleNibbleELow;
                break;

            case 4:
                // Function set: 4-bit mode, 2 lines, 5x8 dots
                current_byte_ = CMD_FUNCTION_SET | BIT4_MODE | LINE2 | DOTS_5x8;
                current_rs_ = false;
                {
                    uint8_t nibble = (current_byte_ >> 4) & 0x0F;
                    transfer_complete_ = false;
                    if (update_pins_) {
                        update_pins_(nibble, false, true);
                    }
                    transfer_state_ = TransferState::HighNibbleELow;
                }
                break;

            case 5:
                // Display control: Display on, cursor off, blink off
                current_byte_ = CMD_DISPLAY_CONTROL | DISPLAY_ON | CURSOR_OFF | BLINK_OFF;
                current_rs_ = false;
                {
                    uint8_t nibble = (current_byte_ >> 4) & 0x0F;
                    transfer_complete_ = false;
                    if (update_pins_) {
                        update_pins_(nibble, false, true);
                    }
                    transfer_state_ = TransferState::HighNibbleELow;
                }
                break;

            case 6:
                // Clear display
                current_byte_ = CMD_CLEAR_DISPLAY;
                current_rs_ = false;
                {
                    uint8_t nibble = (current_byte_ >> 4) & 0x0F;
                    transfer_complete_ = false;
                    if (update_pins_) {
                        update_pins_(nibble, false, true);
                    }
                    transfer_state_ = TransferState::HighNibbleELow;
                }
                break;

            case 7:
                // Entry mode set: Increment cursor, no display shift
                current_byte_ = CMD_ENTRY_MODE_SET | ENTRY_INCREMENT | ENTRY_SHIFT_OFF;
                current_rs_ = false;
                {
                    uint8_t nibble = (current_byte_ >> 4) & 0x0F;
                    transfer_complete_ = false;
                    if (update_pins_) {
                        update_pins_(nibble, false, true);
                    }
                    transfer_state_ = TransferState::HighNibbleELow;
                }
                break;

            case 8:
                // Initialization complete!
                state_ = State::Idle;
                break;

            default:
                state_ = State::Idle;
                break;
        }
    }

    /**
     * @brief Handle transfer state
     */
    void handleTransferState() {
        switch (transfer_state_) {
            case TransferState::Idle:
                handleTransferIdle();
                break;

            case TransferState::HighNibbleELow:
                {
                    uint8_t nibble = (current_byte_ >> 4) & 0x0F;
                    transfer_complete_ = false;
                    if (update_pins_) {
                        update_pins_(nibble, current_rs_, false);
                    }
                    transfer_state_ = TransferState::LowNibbleEHigh;
                }
                break;

            case TransferState::LowNibbleEHigh:
                {
                    uint8_t nibble = current_byte_ & 0x0F;
                    transfer_complete_ = false;
                    if (update_pins_) {
                        update_pins_(nibble, current_rs_, true);
                    }
                    transfer_state_ = TransferState::LowNibbleELow;
                }
                break;

            case TransferState::LowNibbleELow:
                {
                    uint8_t nibble = current_byte_ & 0x0F;
                    transfer_complete_ = false;
                    if (update_pins_) {
                        update_pins_(nibble, current_rs_, false);
                    }
                    transfer_state_ = TransferState::Idle;
                    delay_ = DELAY_SHORT_CMD;
                }
                break;

            default:
                transfer_state_ = TransferState::Idle;
                break;
        }
    }

    /**
     * @brief Handle transfer idle state (send next byte from buffer)
     */
    void handleTransferIdle() {
        if (!buffer_write_in_progress_) {
            state_ = State::Idle;
            return;
        }

        if (buffer_write_partial_) {
            handlePartialBufferWrite();
        } else {
            handleFullBufferWrite();
        }
    }

    /**
     * @brief Handle partial buffer write
     */
    void handlePartialBufferWrite() {
        if (buffer_write_chars_sent_ >= buffer_write_length_) {
            buffer_write_in_progress_ = false;
            state_ = State::Idle;
            return;
        }

        if (buffer_write_need_cursor_) {
            // Send cursor positioning command
            uint16_t current_pos = buffer_write_start_pos_ + buffer_write_chars_sent_;
            uint8_t current_row = current_pos / columns_;
            uint8_t current_col = current_pos % columns_;

            uint8_t row_offsets[] = {ROW1_ADDR, ROW2_ADDR, ROW3_ADDR, ROW4_ADDR};
            current_byte_ = CMD_SET_DDRAM_ADDR | (row_offsets[current_row] + current_col);
            current_rs_ = false;
            uint8_t nibble = (current_byte_ >> 4) & 0x0F;
            transfer_complete_ = false;
            if (update_pins_) {
                update_pins_(nibble, false, true);
            }
            transfer_state_ = TransferState::HighNibbleELow;
            buffer_write_need_cursor_ = false;
        } else {
            // Send character data
            current_byte_ = static_cast<uint8_t>(buffer_ptr_[buffer_write_chars_sent_]);
            current_rs_ = true;
            uint8_t nibble = (current_byte_ >> 4) & 0x0F;
            transfer_complete_ = false;
            if (update_pins_) {
                update_pins_(nibble, true, true);
            }
            transfer_state_ = TransferState::HighNibbleELow;

            buffer_write_chars_sent_++;

            // Check if next character is on a different row
            if (buffer_write_chars_sent_ < buffer_write_length_) {
                uint16_t prev_pos = buffer_write_start_pos_ + buffer_write_chars_sent_ - 1;
                uint16_t next_pos = buffer_write_start_pos_ + buffer_write_chars_sent_;
                uint8_t prev_row = prev_pos / columns_;
                uint8_t next_row = next_pos / columns_;

                if (next_row != prev_row) {
                    buffer_write_need_cursor_ = true;
                }
            }

            // Check if done
            if (buffer_write_chars_sent_ >= buffer_write_length_) {
                buffer_write_in_progress_ = false;
            }
        }
    }

    /**
     * @brief Handle full buffer write
     */
    void handleFullBufferWrite() {
        if (buffer_write_index_ >= buffer_write_total_) {
            buffer_write_in_progress_ = false;
            state_ = State::Idle;
            return;
        }

        uint16_t chars_written = buffer_write_index_ % (columns_ + 1);

        if (chars_written == 0) {
            // Send cursor position command for new row
            uint8_t row_offsets[] = {ROW1_ADDR, ROW2_ADDR, ROW3_ADDR, ROW4_ADDR};
            uint8_t current_row = buffer_write_index_ / (columns_ + 1);
            current_byte_ = CMD_SET_DDRAM_ADDR | row_offsets[current_row];
            current_rs_ = false;
            uint8_t nibble = (current_byte_ >> 4) & 0x0F;
            transfer_complete_ = false;
            if (update_pins_) {
                update_pins_(nibble, false, true);
            }
            transfer_state_ = TransferState::HighNibbleELow;
        } else {
            // Send character data
            uint16_t buffer_char_index = (buffer_write_index_ / (columns_ + 1)) * columns_ +
                                         (chars_written - 1);
            current_byte_ = static_cast<uint8_t>(buffer_ptr_[buffer_char_index]);
            current_rs_ = true;
            uint8_t nibble = (current_byte_ >> 4) & 0x0F;
            transfer_complete_ = false;
            if (update_pins_) {
                update_pins_(nibble, true, true);
            }
            transfer_state_ = TransferState::HighNibbleELow;
        }

        buffer_write_index_++;

        // Check if done
        if (buffer_write_index_ >= buffer_write_total_) {
            buffer_write_in_progress_ = false;
        }
    }
};

} // namespace devices
} // namespace chipz

#endif // CHIPZ_DEVICES_HD44780_HPP
