#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <chipz/peripheral.hpp>

using namespace chipz;

// Concrete implementation of Peripheral for testing
class TestPeripheral : public PeripheralBase {
public:
    TestPeripheral() : status_(Status::Uninitialized), initResult_(true) {}

    bool initialize() override {
        if (initResult_) {
            status_ = Status::Ready;
        } else {
            status_ = Status::Error;
        }
        return initResult_;
    }

    bool reset() override {
        status_ = Status::Ready;
        return true;
    }

    bool isReady() const override {
        return status_ == Status::Ready;
    }

    Status getStatus() const override {
        return status_;
    }

    std::string getDeviceId() const override {
        return "Test Peripheral";
    }

    bool main() override {
        return isReady();
    }

    // Test helpers
    void setInitResult(bool result) { initResult_ = result; }
    void setStatus(Status status) { status_ = status; }

private:
    Status status_;
    bool initResult_;
};

class PeripheralTest : public ::testing::Test {
protected:
    TestPeripheral peripheral;
};

TEST_F(PeripheralTest, InitialStateIsUninitialized) {
    EXPECT_EQ(peripheral.getStatus(), PeripheralBase::Status::Uninitialized);
    EXPECT_FALSE(peripheral.isReady());
}

TEST_F(PeripheralTest, SuccessfulInitializationSetsReady) {
    EXPECT_TRUE(peripheral.initialize());
    EXPECT_EQ(peripheral.getStatus(), PeripheralBase::Status::Ready);
    EXPECT_TRUE(peripheral.isReady());
}

TEST_F(PeripheralTest, FailedInitializationSetsError) {
    peripheral.setInitResult(false);
    EXPECT_FALSE(peripheral.initialize());
    EXPECT_EQ(peripheral.getStatus(), PeripheralBase::Status::Error);
    EXPECT_FALSE(peripheral.isReady());
}

TEST_F(PeripheralTest, ResetSetsDeviceToReady) {
    peripheral.setStatus(PeripheralBase::Status::Error);
    EXPECT_TRUE(peripheral.reset());
    EXPECT_EQ(peripheral.getStatus(), PeripheralBase::Status::Ready);
    EXPECT_TRUE(peripheral.isReady());
}

TEST_F(PeripheralTest, GetDeviceIdReturnsCorrectString) {
    EXPECT_EQ(peripheral.getDeviceId(), "Test Peripheral");
}

TEST_F(PeripheralTest, IsReadyReturnsFalseWhenBusy) {
    peripheral.setStatus(PeripheralBase::Status::Busy);
    EXPECT_FALSE(peripheral.isReady());
}

TEST_F(PeripheralTest, IsReadyReturnsFalseWhenDisconnected) {
    peripheral.setStatus(PeripheralBase::Status::Disconnected);
    EXPECT_FALSE(peripheral.isReady());
}
