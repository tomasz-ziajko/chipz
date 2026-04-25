// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>

#include <atomic>
#include <chipz/core/communication_interface.hpp>
#include <cstdint>

using namespace chipz;

// Minimal concrete subclass for testing the abstract base class
class ConcreteComm : public CommunicationInterface {
    public:
    bool transmit(const uint8_t* data, size_t length) override
    {
        ensureBufferSize(tx_buffer_, length);
        for (size_t i = 0; i < length; ++i) {
            tx_buffer_[i] = data[i];
        }
        transfer_in_progress_ = true;
        return true;
    }

    bool receive(uint8_t* buffer, size_t length) override
    {
        ensureBufferSize(rx_buffer_, length);
        transfer_in_progress_ = true;
        for (size_t i = 0; i < length; ++i) {
            buffer[i] = rx_buffer_[i];
        }
        return true;
    }

    // Expose nextId for connection management tests
    ConnectionId allocateId()
    {
        return nextId();
    }
};

class CommunicationInterfaceTest : public ::testing::Test {
    protected:
    void TearDown() override
    {
        // Ensure static core pending pointer is cleared between tests
        CommunicationInterface::registerCorePending(nullptr);
    }

    ConcreteComm comm;
};

TEST_F(CommunicationInterfaceTest, InitialStateIsReadyAndNoPendingInterrupt)
{
    EXPECT_TRUE(comm.isReady());
    EXPECT_FALSE(comm.hasInterruptPending());
}

TEST_F(CommunicationInterfaceTest, NotifyTransferCompleteSuccessSetsCorrectState)
{
    comm.notifyTransferComplete(true);

    EXPECT_TRUE(comm.hasInterruptPending());
    EXPECT_EQ(comm.getPendingInterruptType(), CommunicationInterface::InterruptType::TransferComplete);
    EXPECT_TRUE(comm.getInterruptSuccess());
    EXPECT_TRUE(comm.isReady());  // transfer_in_progress_ cleared
}

TEST_F(CommunicationInterfaceTest, NotifyTransferCompleteFailureSetsSuccessFalse)
{
    comm.notifyTransferComplete(false);

    EXPECT_TRUE(comm.hasInterruptPending());
    EXPECT_EQ(comm.getPendingInterruptType(), CommunicationInterface::InterruptType::TransferComplete);
    EXPECT_FALSE(comm.getInterruptSuccess());
}

TEST_F(CommunicationInterfaceTest, NotifyErrorSetsCorrectState)
{
    comm.notifyError();

    EXPECT_TRUE(comm.hasInterruptPending());
    EXPECT_EQ(comm.getPendingInterruptType(), CommunicationInterface::InterruptType::Error);
    EXPECT_FALSE(comm.getInterruptSuccess());
    EXPECT_TRUE(comm.isReady());  // transfer_in_progress_ cleared
}

TEST_F(CommunicationInterfaceTest, NotifyRxCompleteSetsCorrectState)
{
    comm.notifyRxComplete();

    EXPECT_TRUE(comm.hasInterruptPending());
    EXPECT_EQ(comm.getPendingInterruptType(), CommunicationInterface::InterruptType::RxComplete);
    EXPECT_TRUE(comm.getInterruptSuccess());
}

TEST_F(CommunicationInterfaceTest, NotifyRxCompleteDoesNotClearTransferInProgress)
{
    uint8_t data[] = {1};
    comm.transmit(data, 1);   // sets transfer_in_progress_
    ASSERT_FALSE(comm.isReady());

    comm.notifyRxComplete();  // should NOT clear transfer_in_progress_
    EXPECT_FALSE(comm.isReady());
}

TEST_F(CommunicationInterfaceTest, NotifyArbitrationLostSetsCorrectState)
{
    comm.notifyArbitrationLost();

    EXPECT_TRUE(comm.hasInterruptPending());
    EXPECT_EQ(comm.getPendingInterruptType(), CommunicationInterface::InterruptType::ArbitrationLost);
    EXPECT_FALSE(comm.getInterruptSuccess());
    EXPECT_TRUE(comm.isReady());  // transfer_in_progress_ cleared
}

TEST_F(CommunicationInterfaceTest, ClearInterruptClearsPendingFlag)
{
    comm.notifyTransferComplete(true);
    ASSERT_TRUE(comm.hasInterruptPending());

    comm.clearInterrupt();
    EXPECT_FALSE(comm.hasInterruptPending());
}

TEST_F(CommunicationInterfaceTest, ClearInterruptDoesNotAffectInterruptType)
{
    comm.notifyError();
    comm.clearInterrupt();

    EXPECT_EQ(comm.getPendingInterruptType(), CommunicationInterface::InterruptType::Error);
}

TEST_F(CommunicationInterfaceTest, RegisterCorePendingIsSetOnNotifyTransferComplete)
{
    std::atomic<bool> core_pending{false};
    CommunicationInterface::registerCorePending(&core_pending);

    comm.notifyTransferComplete(true);
    EXPECT_TRUE(core_pending.load());
}

TEST_F(CommunicationInterfaceTest, RegisterCorePendingIsSetOnNotifyError)
{
    std::atomic<bool> core_pending{false};
    CommunicationInterface::registerCorePending(&core_pending);

    comm.notifyError();
    EXPECT_TRUE(core_pending.load());
}

TEST_F(CommunicationInterfaceTest, RegisterCorePendingIsSetOnNotifyRxComplete)
{
    std::atomic<bool> core_pending{false};
    CommunicationInterface::registerCorePending(&core_pending);

    comm.notifyRxComplete();
    EXPECT_TRUE(core_pending.load());
}

TEST_F(CommunicationInterfaceTest, RegisterCorePendingIsSetOnNotifyArbitrationLost)
{
    std::atomic<bool> core_pending{false};
    CommunicationInterface::registerCorePending(&core_pending);

    comm.notifyArbitrationLost();
    EXPECT_TRUE(core_pending.load());
}

TEST_F(CommunicationInterfaceTest, NullCorePendingDoesNotCrashOnNotify)
{
    CommunicationInterface::registerCorePending(nullptr);
    EXPECT_NO_FATAL_FAILURE(comm.notifyTransferComplete(true));
    EXPECT_NO_FATAL_FAILURE(comm.notifyError());
}

TEST_F(CommunicationInterfaceTest, TxBufferContainsTransmittedData)
{
    uint8_t data[] = {0x11, 0x22, 0x33};
    comm.transmit(data, 3);

    EXPECT_EQ(comm.getTxBuffer()[0], 0x11);
    EXPECT_EQ(comm.getTxBuffer()[1], 0x22);
    EXPECT_EQ(comm.getTxBuffer()[2], 0x33);
}

TEST_F(CommunicationInterfaceTest, BufferGrowsWhenLargerDataTransmitted)
{
    uint8_t small[4] = {};
    comm.transmit(small, 4);
    EXPECT_GE(comm.getBufferSize(), 4u);

    comm.notifyTransferComplete(true);  // clear transfer_in_progress_

    uint8_t large[16] = {};
    comm.transmit(large, 16);
    EXPECT_GE(comm.getBufferSize(), 16u);
}

TEST_F(CommunicationInterfaceTest, TransmitSetsTransferInProgressMakingInterfaceBusy)
{
    uint8_t data[] = {1};
    comm.transmit(data, 1);
    EXPECT_FALSE(comm.isReady());
}

TEST_F(CommunicationInterfaceTest, NextIdAllocatesMonotonicallyIncreasingIds)
{
    auto id0 = comm.allocateId();
    auto id1 = comm.allocateId();
    EXPECT_EQ(id1, id0 + 1);
}

TEST_F(CommunicationInterfaceTest, SelectConnectionIsNoOpByDefault)
{
    EXPECT_NO_FATAL_FAILURE(comm.selectConnection(0));
    EXPECT_NO_FATAL_FAILURE(comm.selectConnection(CommunicationInterface::kInvalidConnection));
}
