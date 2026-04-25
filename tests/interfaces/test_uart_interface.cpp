// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include <chipz/interfaces/uart_interface.hpp>
#include <vector>

using namespace chipz::interfaces;
using chipz::CommunicationInterface;

class UARTInterfaceTest : public ::testing::Test {
    protected:
    struct TxCall {
        std::vector<uint8_t> data;
    };
    struct RxCall {
        uint8_t* buffer;
        uint16_t size;
    };

    std::vector<TxCall> tx_calls;
    std::vector<RxCall> rx_calls;
    int                 tx_return = 0;
    int                 rx_return = 0;

    UARTInterface makeUART()
    {
        return UARTInterface(
            [this](const uint8_t* data, uint16_t size) -> int {
                tx_calls.push_back({{data, data + size}});
                return tx_return;
            },
            [this](uint8_t* buffer, uint16_t size) -> int {
                rx_calls.push_back({buffer, size});
                return rx_return;
            });
    }
};

TEST_F(UARTInterfaceTest, InitialStateIsFullyReady)
{
    auto uart = makeUART();
    EXPECT_TRUE(uart.isReady());
    EXPECT_TRUE(uart.isTxReady());
    EXPECT_TRUE(uart.isRxReady());
}

TEST_F(UARTInterfaceTest, TransmitCallsTxFuncWithCorrectData)
{
    auto    uart   = makeUART();
    uint8_t data[] = {0x41, 0x42, 0x43};
    EXPECT_TRUE(uart.transmit(data, 3));

    ASSERT_EQ(tx_calls.size(), 1u);
    EXPECT_EQ(tx_calls[0].data, (std::vector<uint8_t>{0x41, 0x42, 0x43}));
}

TEST_F(UARTInterfaceTest, TransmitSetsTxBusy)
{
    auto    uart   = makeUART();
    uint8_t data[] = {1};
    uart.transmit(data, 1);
    EXPECT_FALSE(uart.isTxReady());
    EXPECT_FALSE(uart.isReady());  // overall not ready while TX busy
}

TEST_F(UARTInterfaceTest, TransmitReturnsFalseWhenTxBusy)
{
    auto    uart   = makeUART();
    uint8_t data[] = {1};
    uart.transmit(data, 1);
    EXPECT_FALSE(uart.transmit(data, 1));
}

TEST_F(UARTInterfaceTest, TransmitReturnsFalseAndClearsBusyOnTxError)
{
    auto uart      = makeUART();
    tx_return      = -1;
    uint8_t data[] = {1};
    EXPECT_FALSE(uart.transmit(data, 1));
    EXPECT_TRUE(uart.isTxReady());  // cleared on error
    EXPECT_TRUE(uart.hasInterruptPending());
    EXPECT_EQ(uart.getPendingInterruptType(), CommunicationInterface::InterruptType::Error);
}

TEST_F(UARTInterfaceTest, ReceiveArmsHALWithInternalRxBuffer)
{
    auto    uart   = makeUART();
    uint8_t buf[4] = {};
    EXPECT_TRUE(uart.receive(buf, 4));

    ASSERT_EQ(rx_calls.size(), 1u);
    EXPECT_EQ(rx_calls[0].buffer, uart.getRxBuffer());
    EXPECT_EQ(rx_calls[0].size, 4u);
}

TEST_F(UARTInterfaceTest, ReceiveSetsRxBusy)
{
    auto    uart   = makeUART();
    uint8_t buf[1] = {};
    uart.receive(buf, 1);
    EXPECT_FALSE(uart.isRxReady());
    EXPECT_FALSE(uart.isReady());
}

TEST_F(UARTInterfaceTest, ReceiveReturnsFalseWhenRxBusy)
{
    auto    uart   = makeUART();
    uint8_t buf[1] = {};
    uart.receive(buf, 1);
    EXPECT_FALSE(uart.receive(buf, 1));
}

TEST_F(UARTInterfaceTest, ReceiveReturnsFalseAndClearsRxBusyOnRxError)
{
    auto uart      = makeUART();
    rx_return      = -1;
    uint8_t buf[1] = {};
    EXPECT_FALSE(uart.receive(buf, 1));
    EXPECT_TRUE(uart.isRxReady());
}

TEST_F(UARTInterfaceTest, TxAndRxAreIndependentPaths)
{
    auto    uart   = makeUART();
    uint8_t buf[1] = {0};

    uart.transmit(buf, 1);
    EXPECT_FALSE(uart.isTxReady());
    EXPECT_TRUE(uart.isRxReady());  // RX still free while TX busy

    uart.receive(buf, 1);
    EXPECT_FALSE(uart.isTxReady());
    EXPECT_FALSE(uart.isRxReady());  // both busy
    EXPECT_FALSE(uart.isReady());
}

TEST_F(UARTInterfaceTest, IsReadyOnlyWhenBothPathsIdle)
{
    auto    uart   = makeUART();
    uint8_t buf[1] = {0};

    uart.transmit(buf, 1);
    uart.notifyTxComplete();
    EXPECT_TRUE(uart.isReady());   // TX done, RX never started

    uart.receive(buf, 1);
    EXPECT_FALSE(uart.isReady());  // RX running
}

TEST_F(UARTInterfaceTest, NotifyTxCompleteSignalsTransferCompleteAndClearsTxBusy)
{
    auto    uart   = makeUART();
    uint8_t data[] = {1};
    uart.transmit(data, 1);
    uart.notifyTxComplete();

    EXPECT_TRUE(uart.isTxReady());
    EXPECT_TRUE(uart.hasInterruptPending());
    EXPECT_EQ(uart.getPendingInterruptType(), CommunicationInterface::InterruptType::TransferComplete);
    EXPECT_TRUE(uart.getInterruptSuccess());
}

TEST_F(UARTInterfaceTest, NotifyRxCompleteCopiesDataToCallerBuffer)
{
    auto    uart       = makeUART();
    uint8_t ext_buf[3] = {0, 0, 0};
    uart.receive(ext_buf, 3);

    // Simulate HAL writing into the internal rx_buffer_
    uart.getRxBuffer()[0] = 0x11;
    uart.getRxBuffer()[1] = 0x22;
    uart.getRxBuffer()[2] = 0x33;

    uart.notifyRxComplete();

    EXPECT_EQ(ext_buf[0], 0x11);
    EXPECT_EQ(ext_buf[1], 0x22);
    EXPECT_EQ(ext_buf[2], 0x33);
}

TEST_F(UARTInterfaceTest, NotifyRxCompleteClearsRxBusyAndSignalsInterrupt)
{
    auto    uart   = makeUART();
    uint8_t buf[1] = {};
    uart.receive(buf, 1);
    uart.notifyRxComplete();

    EXPECT_TRUE(uart.isRxReady());
    EXPECT_TRUE(uart.hasInterruptPending());
    EXPECT_EQ(uart.getPendingInterruptType(), CommunicationInterface::InterruptType::RxComplete);
}

TEST_F(UARTInterfaceTest, NotifyRxCompleteSkipsCopyWhenCallerPassedRxBuffer)
{
    auto uart = makeUART();

    // Pre-allocate by doing one full cycle
    uint8_t dummy[2] = {};
    uart.receive(dummy, 2);
    uart.notifyRxComplete();
    uart.clearInterrupt();

    // Now receive using getRxBuffer() directly — no copy should occur
    uart.receive(uart.getRxBuffer(), 2);
    uart.getRxBuffer()[0] = 0xAB;
    uart.notifyRxComplete();

    // Data stays in getRxBuffer() untouched (no copy-over-itself)
    EXPECT_EQ(uart.getRxBuffer()[0], 0xAB);
    EXPECT_TRUE(uart.isRxReady());
}
