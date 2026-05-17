// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include <chipz/interfaces/i2c_interface.hpp>
#include <vector>

using chipz::CommunicationInterface;

class I2CInterfaceTest : public ::testing::Test {
    protected:
    struct WriteCall {
        uint8_t              dev;
        uint8_t              mem;
        std::vector<uint8_t> data;
    };
    struct ReadCall {
        uint8_t  dev;
        uint8_t  mem;
        uint16_t size;
        uint8_t* out_buf;  // pointer HAL would fill
    };

    std::vector<WriteCall> write_calls;
    std::vector<ReadCall>  read_calls;
    int                    write_return = 0;
    int                    read_return  = 0;

    auto makeI2C()
    {
        return chipz::interfaces::I2CInterface<32>(
            [this](uint8_t dev, uint8_t mem, uint8_t* data, uint16_t size) -> int {
                read_calls.push_back({dev, mem, size, data});
                return read_return;
            },
            [this](uint8_t dev, uint8_t mem, const uint8_t* data, uint16_t size) -> int {
                write_calls.push_back({dev, mem, {data, data + size}});
                return write_return;
            });
    }
};

TEST_F(I2CInterfaceTest, TransmitCallsWriteWithCorrectDeviceAndMemAddress)
{
    auto i2c = makeI2C();
    i2c.setDeviceAddress(0x48);
    i2c.setMemoryAddress(0x10);

    uint8_t data[] = {0xAA, 0xBB};
    EXPECT_TRUE(i2c.transmit(data, 2));

    ASSERT_EQ(write_calls.size(), 1u);
    EXPECT_EQ(write_calls[0].dev, 0x48);
    EXPECT_EQ(write_calls[0].mem, 0x10);
    EXPECT_EQ(write_calls[0].data, (std::vector<uint8_t>{0xAA, 0xBB}));
}

TEST_F(I2CInterfaceTest, TransmitReturnsFalseWhenBusy)
{
    auto    i2c    = makeI2C();
    uint8_t data[] = {1};
    i2c.transmit(data, 1);
    EXPECT_FALSE(i2c.transmit(data, 1));
}

TEST_F(I2CInterfaceTest, TransmitSetsTransferInProgress)
{
    auto    i2c    = makeI2C();
    uint8_t data[] = {1};
    i2c.transmit(data, 1);
    EXPECT_FALSE(i2c.isReady());
}

TEST_F(I2CInterfaceTest, TransmitReturnsFalseAndSignalsErrorOnWriteFailure)
{
    auto i2c       = makeI2C();
    write_return   = -1;
    uint8_t data[] = {1};
    EXPECT_FALSE(i2c.transmit(data, 1));
    EXPECT_TRUE(i2c.hasInterruptPending());
    EXPECT_EQ(i2c.getPendingInterruptType(), CommunicationInterface::InterruptType::Error);
}

TEST_F(I2CInterfaceTest, TransmitCopiesDataToInternalBuffer)
{
    auto    i2c    = makeI2C();
    uint8_t data[] = {0x01, 0x02, 0x03};
    i2c.transmit(data, 3);
    EXPECT_EQ(i2c.getTxBuffer()[0], 0x01);
    EXPECT_EQ(i2c.getTxBuffer()[2], 0x03);
}

TEST_F(I2CInterfaceTest, ReceiveCallsReadWithCorrectDeviceAndMemAddress)
{
    auto i2c = makeI2C();
    i2c.setDeviceAddress(0x68);
    i2c.setMemoryAddress(0x20);

    uint8_t buf[3] = {};
    EXPECT_TRUE(i2c.receive(buf, 3));

    ASSERT_EQ(read_calls.size(), 1u);
    EXPECT_EQ(read_calls[0].dev, 0x68);
    EXPECT_EQ(read_calls[0].mem, 0x20);
    EXPECT_EQ(read_calls[0].size, 3u);
}

TEST_F(I2CInterfaceTest, ReceivePassesInternalRxBufferToHAL)
{
    auto    i2c    = makeI2C();
    uint8_t buf[2] = {};
    i2c.receive(buf, 2);

    ASSERT_EQ(read_calls.size(), 1u);
    EXPECT_EQ(read_calls[0].out_buf, i2c.getRxBuffer());
}

TEST_F(I2CInterfaceTest, ReceiveCopiesRxBufferToCallerBuffer)
{
    auto i2c = chipz::interfaces::I2CInterface<32>(
        [](uint8_t, uint8_t, uint8_t* data, uint16_t size) -> int {
            for (uint16_t i = 0; i < size; ++i) {
                data[i] = static_cast<uint8_t>(i + 1);
            }
            return 0;
        },
        [](uint8_t, uint8_t, const uint8_t*, uint16_t) -> int { return 0; });

    uint8_t buf[3] = {};
    i2c.receive(buf, 3);
    EXPECT_EQ(buf[0], 1);
    EXPECT_EQ(buf[1], 2);
    EXPECT_EQ(buf[2], 3);
}

TEST_F(I2CInterfaceTest, ReceiveReturnsFalseWhenBusy)
{
    auto    i2c    = makeI2C();
    uint8_t buf[1] = {};
    i2c.receive(buf, 1);
    EXPECT_FALSE(i2c.receive(buf, 1));
}

TEST_F(I2CInterfaceTest, ReceiveReturnsFalseAndSignalsErrorOnReadFailure)
{
    auto i2c       = makeI2C();
    read_return    = -1;
    uint8_t buf[1] = {};
    EXPECT_FALSE(i2c.receive(buf, 1));
    EXPECT_TRUE(i2c.hasInterruptPending());
    EXPECT_EQ(i2c.getPendingInterruptType(), CommunicationInterface::InterruptType::Error);
}

TEST_F(I2CInterfaceTest, RegisterConnectionAllocatesSequentialIds)
{
    auto i2c = makeI2C();
    auto id0 = i2c.registerConnection(0x48);
    auto id1 = i2c.registerConnection(0x68);
    EXPECT_EQ(id1, id0 + 1);
}

TEST_F(I2CInterfaceTest, SelectConnectionSwitchesDeviceAddress)
{
    auto i2c = makeI2C();
    auto id1 = i2c.registerConnection(0x48);
    auto id2 = i2c.registerConnection(0x68);

    i2c.selectConnection(id2);
    EXPECT_EQ(i2c.getDeviceAddress(), 0x68);

    i2c.selectConnection(id1);
    EXPECT_EQ(i2c.getDeviceAddress(), 0x48);
}

TEST_F(I2CInterfaceTest, SetAndGetDeviceAddress)
{
    auto i2c = makeI2C();
    i2c.setDeviceAddress(0x55);
    EXPECT_EQ(i2c.getDeviceAddress(), 0x55);
}

TEST_F(I2CInterfaceTest, SetAndGetMemoryAddress)
{
    auto i2c = makeI2C();
    i2c.setMemoryAddress(0x0F);
    EXPECT_EQ(i2c.getMemoryAddress(), 0x0F);
}

TEST_F(I2CInterfaceTest, TransmitUsesCurrentMemoryAddress)
{
    auto i2c = makeI2C();
    i2c.setMemoryAddress(0xAB);
    uint8_t data[] = {0};
    i2c.transmit(data, 1);

    ASSERT_EQ(write_calls.size(), 1u);
    EXPECT_EQ(write_calls[0].mem, 0xAB);
}
