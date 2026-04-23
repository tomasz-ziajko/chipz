// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>
#include <chipz/interfaces/spi_interface.hpp>
#include <vector>

using namespace chipz::interfaces;
using chipz::CommunicationInterface;

class SPIInterfaceTest : public ::testing::Test {
protected:
    struct TransferCall {
        std::vector<uint8_t> tx_data;
        uint16_t size;
    };

    std::vector<TransferCall> transfer_calls;
    int transfer_return = 0;

    SPIInterface makeSPI() {
        return SPIInterface([this](uint8_t* tx, uint8_t* rx, uint16_t size) -> int {
            transfer_calls.push_back({{tx, tx + size}, size});
            return transfer_return;
        });
    }
};

TEST_F(SPIInterfaceTest, TransmitCallsTransferWithCorrectData) {
    auto spi = makeSPI();
    uint8_t data[] = {0x01, 0x02, 0x03};
    EXPECT_TRUE(spi.transmit(data, 3));

    ASSERT_EQ(transfer_calls.size(), 1u);
    EXPECT_EQ(transfer_calls[0].size, 3u);
    EXPECT_EQ(transfer_calls[0].tx_data, (std::vector<uint8_t>{0x01, 0x02, 0x03}));
}

TEST_F(SPIInterfaceTest, TransmitSetsTransferInProgress) {
    auto spi = makeSPI();
    uint8_t data[] = {1};
    spi.transmit(data, 1);
    EXPECT_FALSE(spi.isReady());
}

TEST_F(SPIInterfaceTest, TransmitReturnsFalseWhenBusy) {
    auto spi = makeSPI();
    uint8_t data[] = {1};
    spi.transmit(data, 1);
    EXPECT_FALSE(spi.transmit(data, 1));
}

TEST_F(SPIInterfaceTest, TransmitReturnsFalseAndSignalsErrorOnTransferFailure) {
    auto spi = makeSPI();
    transfer_return = -1;
    uint8_t data[] = {1};
    EXPECT_FALSE(spi.transmit(data, 1));
    EXPECT_TRUE(spi.hasInterruptPending());
    EXPECT_EQ(spi.getPendingInterruptType(), CommunicationInterface::InterruptType::Error);
}

TEST_F(SPIInterfaceTest, TransmitAssertsChipSelectBeforeTransfer) {
    auto spi = makeSPI();
    bool cs_asserted = false;
    auto id = spi.registerConnection([&](bool select) { cs_asserted = select; });
    spi.selectConnection(id);

    uint8_t data[] = {0xFF};
    spi.transmit(data, 1);
    EXPECT_TRUE(cs_asserted);
}

TEST_F(SPIInterfaceTest, TransmitErrorDeassertsChipSelect) {
    auto spi = makeSPI();
    transfer_return = -1;
    bool cs_state = true;
    auto id = spi.registerConnection([&](bool select) { cs_state = select; });
    spi.selectConnection(id);

    uint8_t data[] = {0xFF};
    spi.transmit(data, 1);
    EXPECT_FALSE(cs_state);
}

TEST_F(SPIInterfaceTest, ReceiveSendsZeroBytesAsDummy) {
    auto spi = makeSPI();
    uint8_t buf[4] = {};
    spi.receive(buf, 4);

    ASSERT_EQ(transfer_calls.size(), 1u);
    EXPECT_EQ(transfer_calls[0].size, 4u);
    for (auto b : transfer_calls[0].tx_data) EXPECT_EQ(b, 0x00);
}

TEST_F(SPIInterfaceTest, ReceiveCopiesRxDataToCallerBuffer) {
    SPIInterface spi([](uint8_t*, uint8_t* rx, uint16_t size) -> int {
        for (uint16_t i = 0; i < size; ++i) rx[i] = static_cast<uint8_t>(i + 0x10);
        return 0;
    });

    uint8_t buf[3] = {};
    spi.receive(buf, 3);
    EXPECT_EQ(buf[0], 0x10);
    EXPECT_EQ(buf[1], 0x11);
    EXPECT_EQ(buf[2], 0x12);
}

TEST_F(SPIInterfaceTest, ReceiveReturnsFalseWhenBusy) {
    auto spi = makeSPI();
    uint8_t buf[1] = {};
    spi.receive(buf, 1);
    EXPECT_FALSE(spi.receive(buf, 1));
}

TEST_F(SPIInterfaceTest, ReceiveReturnsFalseAndSignalsErrorOnTransferFailure) {
    auto spi = makeSPI();
    transfer_return = -1;
    uint8_t buf[1] = {};
    EXPECT_FALSE(spi.receive(buf, 1));
    EXPECT_TRUE(spi.hasInterruptPending());
    EXPECT_EQ(spi.getPendingInterruptType(), CommunicationInterface::InterruptType::Error);
}

TEST_F(SPIInterfaceTest, TransferSendsAndReceivesSimultaneously) {
    SPIInterface spi([](uint8_t* tx, uint8_t* rx, uint16_t size) -> int {
        for (uint16_t i = 0; i < size; ++i) rx[i] = static_cast<uint8_t>(tx[i] ^ 0xFF);
        return 0;
    });

    uint8_t tx[] = {0xA0, 0xB0};
    uint8_t rx[2] = {};
    EXPECT_TRUE(spi.transfer(tx, rx, 2));
    EXPECT_EQ(rx[0], 0x5F);
    EXPECT_EQ(rx[1], 0x4F);
}

TEST_F(SPIInterfaceTest, TransferReturnsFalseWhenBusy) {
    auto spi = makeSPI();
    uint8_t tx[] = {1};
    uint8_t rx[1] = {};
    spi.transfer(tx, rx, 1);
    EXPECT_FALSE(spi.transfer(tx, rx, 1));
}

TEST_F(SPIInterfaceTest, TransferReturnsFalseAndSignalsErrorOnFailure) {
    auto spi = makeSPI();
    transfer_return = -1;
    uint8_t tx[] = {1};
    uint8_t rx[1] = {};
    EXPECT_FALSE(spi.transfer(tx, rx, 1));
    EXPECT_TRUE(spi.hasInterruptPending());
    EXPECT_EQ(spi.getPendingInterruptType(), CommunicationInterface::InterruptType::Error);
}

TEST_F(SPIInterfaceTest, RegisterConnectionAllocatesSequentialIds) {
    auto spi = makeSPI();
    auto id0 = spi.registerConnection([](bool) {});
    auto id1 = spi.registerConnection([](bool) {});
    EXPECT_EQ(id1, id0 + 1);
}

TEST_F(SPIInterfaceTest, SelectConnectionSwitchesActiveChipSelect) {
    auto spi = makeSPI();
    int cs0_calls = 0;
    int cs1_calls = 0;

    auto id0 = spi.registerConnection([&](bool) { ++cs0_calls; });
    auto id1 = spi.registerConnection([&](bool) { ++cs1_calls; });

    spi.selectConnection(id0);
    uint8_t data[] = {1};
    spi.transmit(data, 1);
    spi.notifyTransferComplete(true);

    spi.selectConnection(id1);
    spi.transmit(data, 1);

    EXPECT_EQ(cs0_calls, 1);
    EXPECT_EQ(cs1_calls, 1);
}
