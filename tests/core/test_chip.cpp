// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include <chipz/core/chip.hpp>
#include <chipz/interfaces/i2c_interface.hpp>
#include <chipz/interfaces/spi_interface.hpp>
#include <string>

using namespace chipz;

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

static auto makeI2C()
{
    return chipz::interfaces::I2CInterface<32>(
        [](uint8_t, uint8_t, uint8_t*, uint16_t) -> int { return 0; },
        [](uint8_t, uint8_t, const uint8_t*, uint16_t) -> int { return 0; });
}

static auto makeSPI()
{
    return chipz::interfaces::SPIInterface<32>([](uint8_t*, uint8_t*, uint16_t) -> int { return 0; });
}

using TestI2C = decltype(makeI2C());
using TestSPI = decltype(makeSPI());

// ----------------------------------------------------------------------------
// Minimal concrete Chip for testing
// ----------------------------------------------------------------------------

class TestChip : public Chip<TestI2C> {
    public:
    explicit TestChip(TestI2C& i2c) : Chip<TestI2C>(i2c)
    {
    }

    bool initialize() override
    {
        return init_result;
    }
    bool reset() override
    {
        return reset_result;
    }
    bool isReady() const override
    {
        return ready;
    }
    ChipBase::Status getStatus() const override
    {
        return status;
    }
    std::string getDeviceId() const override
    {
        return "TestChip";
    }
    bool main() override
    {
        return main_result;
    }

    // Expose protected helpers for testing
    bool doTransmit(const uint8_t* data, size_t len)
    {
        return transmit<TestI2C>(data, len);
    }
    bool doReceive(uint8_t* buf, size_t len)
    {
        return receive<TestI2C>(buf, len);
    }
    void callDeferMs(uint32_t ms)
    {
        defer_ms_(ms);
    }
    void callDeferUs(uint32_t us)
    {
        defer_us_(us);
    }

    bool             init_result  = true;
    bool             reset_result = true;
    bool             ready        = true;
    bool             main_result  = true;
    ChipBase::Status status       = ChipBase::Status::Ready;

    // Interrupt dispatch tracking
    CommunicationInterface* last_interrupt_iface     = nullptr;
    bool                    transfer_complete_called = false;
    bool                    last_transfer_success    = false;
    bool                    rx_complete_called       = false;
    bool                    error_called             = false;
    bool                    arbitration_lost_called  = false;

    protected:
    void onTransferComplete(CommunicationInterface& which, bool success) override
    {
        last_interrupt_iface     = &which;
        last_transfer_success    = success;
        transfer_complete_called = true;
    }
    void onRxComplete(CommunicationInterface& which) override
    {
        last_interrupt_iface = &which;
        rx_complete_called   = true;
    }
    void onError(CommunicationInterface& which) override
    {
        last_interrupt_iface = &which;
        error_called         = true;
    }
    void onArbitrationLost(CommunicationInterface& which) override
    {
        last_interrupt_iface    = &which;
        arbitration_lost_called = true;
    }
};

// Dual-interface chip for testing get<T>() with multiple types
class DualChip : public Chip<TestI2C, TestSPI> {
    public:
    DualChip(TestI2C& i2c, TestSPI& spi) : Chip<TestI2C, TestSPI>(i2c, spi)
    {
    }

    bool initialize() override
    {
        return true;
    }
    bool reset() override
    {
        return true;
    }
    bool isReady() const override
    {
        return true;
    }
    ChipBase::Status getStatus() const override
    {
        return ChipBase::Status::Ready;
    }
    std::string getDeviceId() const override
    {
        return "DualChip";
    }
    bool main() override
    {
        return true;
    }
};

// ----------------------------------------------------------------------------
// ChipBase static registry tests
// ----------------------------------------------------------------------------

class ChipBaseRegistryTest : public ::testing::Test {
    protected:
    size_t count_before = ChipBase::getCount();
};

TEST_F(ChipBaseRegistryTest, ChipRegistersOnConstruction)
{
    auto     i2c = makeI2C();
    TestChip chip(i2c);
    EXPECT_EQ(ChipBase::getCount(), count_before + 1);
}

TEST_F(ChipBaseRegistryTest, ChipUnregistersOnDestruction)
{
    auto i2c = makeI2C();
    {
        TestChip chip(i2c);
    }
    EXPECT_EQ(ChipBase::getCount(), count_before);
}

TEST_F(ChipBaseRegistryTest, InitializeAllReturnsTrueWhenAllSucceed)
{
    auto     i2c = makeI2C();
    TestChip chip(i2c);
    chip.init_result = true;
    EXPECT_TRUE(ChipBase::initializeAll());
}

TEST_F(ChipBaseRegistryTest, InitializeAllReturnsFalseWhenAnyFails)
{
    auto     i2c = makeI2C();
    TestChip chip(i2c);
    chip.init_result = false;
    EXPECT_FALSE(ChipBase::initializeAll());
}

TEST_F(ChipBaseRegistryTest, ResetAllReturnsTrueWhenAllSucceed)
{
    auto     i2c = makeI2C();
    TestChip chip(i2c);
    chip.reset_result = true;
    EXPECT_TRUE(ChipBase::resetAll());
}

TEST_F(ChipBaseRegistryTest, ResetAllReturnsFalseWhenAnyFails)
{
    auto     i2c = makeI2C();
    TestChip chip(i2c);
    chip.reset_result = false;
    EXPECT_FALSE(ChipBase::resetAll());
}

TEST_F(ChipBaseRegistryTest, RunAllMainReturnsTrueWhenAllSucceed)
{
    auto     i2c = makeI2C();
    TestChip chip(i2c);
    chip.main_result = true;
    EXPECT_TRUE(ChipBase::runAllMain());
}

TEST_F(ChipBaseRegistryTest, RunAllMainReturnsFalseWhenAnyFails)
{
    auto     i2c = makeI2C();
    TestChip chip(i2c);
    chip.main_result = false;
    EXPECT_FALSE(ChipBase::runAllMain());
}

TEST_F(ChipBaseRegistryTest, AllReadyReturnsTrueWhenAllChipsReady)
{
    auto     i2c = makeI2C();
    TestChip chip(i2c);
    chip.ready = true;
    EXPECT_TRUE(ChipBase::allReady());
}

TEST_F(ChipBaseRegistryTest, AllReadyReturnsFalseWhenAnyChipNotReady)
{
    auto     i2c = makeI2C();
    TestChip chip(i2c);
    chip.ready = false;
    EXPECT_FALSE(ChipBase::allReady());
}

TEST_F(ChipBaseRegistryTest, GetStatusCountReturnsCorrectCount)
{
    auto     i2c = makeI2C();
    TestChip chip(i2c);
    chip.status = ChipBase::Status::Ready;

    size_t ready_count = ChipBase::getStatusCount(ChipBase::Status::Ready);
    EXPECT_GE(ready_count, 1u);
    EXPECT_EQ(ChipBase::getStatusCount(ChipBase::Status::Error), 0u);
}

TEST_F(ChipBaseRegistryTest, GetDefaultPriorityReturns128)
{
    auto     i2c = makeI2C();
    TestChip chip(i2c);
    EXPECT_EQ(chip.getDefaultPriority(), 128u);
}

// ----------------------------------------------------------------------------
// Chip<> template tests
// ----------------------------------------------------------------------------

class ChipTest : public ::testing::Test {
    protected:
    TestI2C  i2c = makeI2C();
    TestChip chip{i2c};
};

TEST_F(ChipTest, GetReturnsReferenceToCorrectInterface)
{
    EXPECT_EQ(&chip.get<TestI2C>(), &i2c);
}

TEST_F(ChipTest, GetCommInterfacesReturnsAllInterfaces)
{
    auto interfaces = chip.getCommInterfaces();
    ASSERT_EQ(interfaces.size(), 1u);
    EXPECT_EQ(interfaces[0], &i2c);
}

TEST_F(ChipTest, OnInterruptDispatchesToTransferComplete)
{
    chip.onInterrupt(i2c, CommunicationInterface::InterruptType::TransferComplete, true);
    EXPECT_TRUE(chip.transfer_complete_called);
    EXPECT_EQ(chip.last_interrupt_iface, &i2c);
    EXPECT_TRUE(chip.last_transfer_success);
}

TEST_F(ChipTest, OnInterruptDispatchesToTransferCompleteWithFailure)
{
    chip.onInterrupt(i2c, CommunicationInterface::InterruptType::TransferComplete, false);
    EXPECT_TRUE(chip.transfer_complete_called);
    EXPECT_FALSE(chip.last_transfer_success);
}

TEST_F(ChipTest, OnInterruptDispatchesToRxComplete)
{
    chip.onInterrupt(i2c, CommunicationInterface::InterruptType::RxComplete, false);
    EXPECT_TRUE(chip.rx_complete_called);
    EXPECT_EQ(chip.last_interrupt_iface, &i2c);
}

TEST_F(ChipTest, OnInterruptDispatchesToError)
{
    chip.onInterrupt(i2c, CommunicationInterface::InterruptType::Error, false);
    EXPECT_TRUE(chip.error_called);
    EXPECT_EQ(chip.last_interrupt_iface, &i2c);
}

TEST_F(ChipTest, OnInterruptDispatchesToArbitrationLost)
{
    chip.onInterrupt(i2c, CommunicationInterface::InterruptType::ArbitrationLost, false);
    EXPECT_TRUE(chip.arbitration_lost_called);
    EXPECT_EQ(chip.last_interrupt_iface, &i2c);
}

TEST_F(ChipTest, SetClaimBusCallbackIsInvokedBeforeTransmit)
{
    bool claim_called = false;
    chip.setClaimBusCallback(&i2c, [&] { claim_called = true; });

    uint8_t data[] = {1};
    chip.doTransmit(data, 1);
    EXPECT_TRUE(claim_called);
}

TEST_F(ChipTest, SetClaimBusCallbackIsInvokedBeforeReceive)
{
    bool claim_called = false;
    chip.setClaimBusCallback(&i2c, [&] { claim_called = true; });

    uint8_t buf[1] = {};
    chip.doReceive(buf, 1);
    EXPECT_TRUE(claim_called);
}

TEST_F(ChipTest, TransmitReturnsFalseWhenInterfaceBusy)
{
    uint8_t data[] = {1};
    chip.doTransmit(data, 1);
    EXPECT_FALSE(chip.doTransmit(data, 1));
}

TEST_F(ChipTest, ReceiveReturnsFalseWhenInterfaceBusy)
{
    uint8_t buf[1] = {};
    chip.doReceive(buf, 1);
    EXPECT_FALSE(chip.doReceive(buf, 1));
}

TEST_F(ChipTest, SetDeferCallbacksAreStoredAndCallable)
{
    bool defer_ms_called = false;
    bool defer_us_called = false;

    chip.setDeferCallbacks([&](uint32_t) { defer_ms_called = true; }, [&](uint32_t) { defer_us_called = true; });

    chip.callDeferMs(100);
    chip.callDeferUs(500);

    EXPECT_TRUE(defer_ms_called);
    EXPECT_TRUE(defer_us_called);
}

// ----------------------------------------------------------------------------
// Chip<> with multiple interface types
// ----------------------------------------------------------------------------

class DualChipTest : public ::testing::Test {
    protected:
    TestI2C  i2c = makeI2C();
    TestSPI  spi = makeSPI();
    DualChip chip{i2c, spi};
};

TEST_F(DualChipTest, GetReturnsCorrectInterfaceForEachType)
{
    EXPECT_EQ(&chip.get<TestI2C>(), &i2c);
    EXPECT_EQ(&chip.get<TestSPI>(), &spi);
}

TEST_F(DualChipTest, GetCommInterfacesReturnsAllTwoInterfaces)
{
    auto interfaces = chip.getCommInterfaces();
    ASSERT_EQ(interfaces.size(), 2u);

    bool has_i2c = false;
    bool has_spi = false;
    for (auto* iface : interfaces) {
        if (iface == &i2c) {
            has_i2c = true;
        }
        if (iface == &spi) {
            has_spi = true;
        }
    }
    EXPECT_TRUE(has_i2c);
    EXPECT_TRUE(has_spi);
}

TEST_F(DualChipTest, SetClaimBusCallbackMatchesCorrectInterface)
{
    bool i2c_claimed = false;
    bool spi_claimed = false;

    chip.setClaimBusCallback(&i2c, [&] { i2c_claimed = true; });
    chip.setClaimBusCallback(&spi, [&] { spi_claimed = true; });

    EXPECT_FALSE(i2c_claimed);
    EXPECT_FALSE(spi_claimed);
}
