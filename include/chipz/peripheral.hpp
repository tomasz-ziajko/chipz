#ifndef CHIPZ_PERIPHERAL_HPP
#define CHIPZ_PERIPHERAL_HPP

#include "concepts.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace chipz {

/**
 * @brief Non-template base class for all peripheral devices
 *
 * Defines the common virtual interface and owns the static registry.
 * Being non-template allows heterogeneous collections of peripherals
 * and enables the static management methods (initializeAll, runAllMain, etc.).
 *
 * AUTOMATIC REGISTRATION:
 * Each PeripheralBase instance automatically registers itself upon construction
 * and unregisters upon destruction.
 */
class PeripheralBase {
public:
    enum class Status {
        Uninitialized,
        Ready,
        Busy,
        Error,
        Disconnected
    };

    virtual ~PeripheralBase() {
        unregisterInstance(this);
    }

    virtual bool initialize() = 0;
    virtual bool reset() = 0;
    virtual bool isReady() const = 0;
    virtual Status getStatus() const = 0;
    virtual std::string getDeviceId() const = 0;
    virtual bool main() = 0;

    static bool initializeAll() {
        bool all_success = true;
        for (auto* peripheral : getRegistry()) {
            if (!peripheral->initialize()) {
                all_success = false;
            }
        }
        return all_success;
    }

    static bool resetAll() {
        bool all_success = true;
        for (auto* peripheral : getRegistry()) {
            if (!peripheral->reset()) {
                all_success = false;
            }
        }
        return all_success;
    }

    static bool runAllMain() {
        bool all_success = true;
        for (auto* peripheral : getRegistry()) {
            if (!peripheral->main()) {
                all_success = false;
            }
        }
        return all_success;
    }

    static bool allReady() {
        for (const auto* peripheral : getRegistry()) {
            if (!peripheral->isReady()) {
                return false;
            }
        }
        return true;
    }

    static size_t getCount() {
        return getRegistry().size();
    }

    static size_t getStatusCount(Status status) {
        size_t count = 0;
        for (const auto* peripheral : getRegistry()) {
            if (peripheral->getStatus() == status) {
                ++count;
            }
        }
        return count;
    }

protected:
    PeripheralBase() {
        registerInstance(this);
    }

    PeripheralBase(const PeripheralBase&) = delete;
    PeripheralBase& operator=(const PeripheralBase&) = delete;

    PeripheralBase(PeripheralBase&&) = default;
    PeripheralBase& operator=(PeripheralBase&&) = default;

private:
    static std::vector<PeripheralBase*>& getRegistry() {
        static std::vector<PeripheralBase*> registry;
        return registry;
    }

    static void registerInstance(PeripheralBase* instance) {
        getRegistry().push_back(instance);
    }

    static void unregisterInstance(PeripheralBase* instance) {
        auto& registry = getRegistry();
        for (auto it = registry.begin(); it != registry.end(); ++it) {
            if (*it == instance) {
                registry.erase(it);
                break;
            }
        }
    }
};

/**
 * @brief Template middle layer binding a peripheral to its communication interface
 *
 * Inherits PeripheralBase and owns a reference to the communication interface.
 * Device drivers inherit from this class with their specific interface type,
 * removing the need for a template parameter on each device class.
 *
 * @tparam CommInterface Communication interface type (must satisfy chipz::concepts::CommunicationInterface)
 */
template<chipz::concepts::CommunicationInterface CommInterface>
class Peripheral : public PeripheralBase {
protected:
    CommInterface& comm_;

    explicit Peripheral(CommInterface& comm) : comm_(comm) {}
};

} // namespace chipz

#endif // CHIPZ_PERIPHERAL_HPP
