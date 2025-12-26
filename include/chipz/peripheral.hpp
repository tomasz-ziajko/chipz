#ifndef CHIPZ_PERIPHERAL_HPP
#define CHIPZ_PERIPHERAL_HPP

#include <cstdint>
#include <string>
#include <vector>
#include "communication_interface.hpp"

namespace chipz {

/**
 * @brief Base class for all external peripheral devices
 *
 * This abstract base class defines the common interface that all
 * peripheral drivers must implement. It provides basic lifecycle
 * management and status reporting.
 *
 * This is a non-templated base class to allow polymorphic usage.
 * Derived classes use templates for zero-cost abstraction over
 * communication interfaces.
 *
 * AUTOMATIC REGISTRATION:
 * Each Peripheral instance automatically registers itself upon construction
 * and unregisters upon destruction. This allows centralized management
 * through static methods without manual registration.
 */
class Peripheral {
public:
    enum class Status {
        Uninitialized,
        Ready,
        Busy,
        Error,
        Disconnected
    };

    virtual ~Peripheral() {
        // Auto-unregister on destruction
        unregisterInstance(this);
    }

    /**
     * @brief Initialize the peripheral device
     * @return true if initialization successful, false otherwise
     */
    virtual bool initialize() = 0;

    /**
     * @brief Reset the peripheral to its default state
     * @return true if reset successful, false otherwise
     */
    virtual bool reset() = 0;

    /**
     * @brief Check if the peripheral is ready for communication
     * @return true if ready, false otherwise
     */
    virtual bool isReady() const = 0;

    /**
     * @brief Get current status of the peripheral
     * @return Current status
     */
    virtual Status getStatus() const = 0;

    /**
     * @brief Get human-readable device identifier
     * @return Device name/identifier
     */
    virtual std::string getDeviceId() const = 0;

    /**
     * @brief Main operational function for the peripheral
     *
     * This function should be called periodically to handle the peripheral's
     * main operations, such as reading sensors, updating displays, processing
     * data, etc. The specific behavior is defined by each peripheral implementation.
     *
     * @return true if operation successful, false otherwise
     */
    virtual bool main() = 0;

    // Static methods for managing all registered peripherals

    /**
     * @brief Initialize all registered peripherals
     * Calls initialize() on each registered peripheral
     * @return true if all peripherals initialized successfully, false if any failed
     */
    static bool initializeAll() {
        bool all_success = true;
        for (auto* peripheral : getRegistry()) {
            if (!peripheral->initialize()) {
                all_success = false;
            }
        }
        return all_success;
    }

    /**
     * @brief Reset all registered peripherals
     * Calls reset() on each registered peripheral
     * @return true if all peripherals reset successfully, false if any failed
     */
    static bool resetAll() {
        bool all_success = true;
        for (auto* peripheral : getRegistry()) {
            if (!peripheral->reset()) {
                all_success = false;
            }
        }
        return all_success;
    }

    /**
     * @brief Run main() function on all registered peripherals
     * This should be called periodically (e.g., in main loop) to allow
     * each peripheral to perform its operational tasks
     * @return true if all peripherals executed successfully, false if any failed
     */
    static bool runAllMain() {
        bool all_success = true;
        for (auto* peripheral : getRegistry()) {
            if (!peripheral->main()) {
                all_success = false;
            }
        }
        return all_success;
    }

    /**
     * @brief Check if all registered peripherals are ready
     * @return true if all peripherals are ready, false if any are not ready
     */
    static bool allReady() {
        for (const auto* peripheral : getRegistry()) {
            if (!peripheral->isReady()) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Get the number of registered peripherals
     * @return Number of registered peripherals
     */
    static size_t getCount() {
        return getRegistry().size();
    }

    /**
     * @brief Get count of peripherals in each status state
     * @param status The status to count
     * @return Number of peripherals in the specified status
     */
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
    Peripheral() {
        // Auto-register on construction
        registerInstance(this);
    }

    // Prevent copying
    Peripheral(const Peripheral&) = delete;
    Peripheral& operator=(const Peripheral&) = delete;

    // Allow moving
    Peripheral(Peripheral&&) = default;
    Peripheral& operator=(Peripheral&&) = default;

private:
    /**
     * @brief Get the static registry of all peripheral instances
     * Uses "construct on first use" idiom to avoid static initialization order issues
     * @return Reference to static vector of peripheral pointers
     */
    static std::vector<Peripheral*>& getRegistry() {
        static std::vector<Peripheral*> registry;
        return registry;
    }

    /**
     * @brief Register a peripheral instance
     * @param instance Pointer to peripheral to register
     */
    static void registerInstance(Peripheral* instance) {
        getRegistry().push_back(instance);
    }

    /**
     * @brief Unregister a peripheral instance
     * @param instance Pointer to peripheral to unregister
     */
    static void unregisterInstance(Peripheral* instance) {
        auto& registry = getRegistry();
        for (auto it = registry.begin(); it != registry.end(); ++it) {
            if (*it == instance) {
                registry.erase(it);
                break;
            }
        }
    }
};

} // namespace chipz

#endif // CHIPZ_PERIPHERAL_HPP
