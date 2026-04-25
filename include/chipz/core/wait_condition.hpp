// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

#ifndef CHIPZ_WAIT_CONDITION_HPP
#define CHIPZ_WAIT_CONDITION_HPP

#include <cstdint>

namespace chipz {

class CommunicationInterface;

/**
 * @brief Returned from ChipBase::run() to tell Core when to call run() next.
 *
 * DelayMs / DelayUs are driver-facing. Core converts them to Deadline (absolute
 * ticks) on receipt — drivers never construct Deadline directly.
 */
class WaitCondition {
    public:
    enum class Type {
        Immediate,  ///< call run() next service() cycle
        DelayMs,    ///< call run() after N ms   — Core converts to Deadline
        DelayUs,    ///< call run() after N µs   — Core converts to Deadline
        Deadline,   ///< absolute tick — internal, set by Core only
        Comm,       ///< call run() when any interrupt fires on this interface
        IRQ,        ///< call run() when this hardware IRQ fires
        Demand      ///< call run() only when Core::wake() is invoked
    };

    static WaitCondition immediate()
    {
        return WaitCondition{Type::Immediate};
    }

    static WaitCondition delayMs(uint32_t ms)
    {
        WaitCondition w{Type::DelayMs};
        w.p_.ms = ms;
        return w;
    }

    static WaitCondition delayUs(uint32_t us)
    {
        WaitCondition w{Type::DelayUs};
        w.p_.us = us;
        return w;
    }

    // Internal — Core only.
    static WaitCondition deadline(uint64_t absolute_ticks)
    {
        WaitCondition w{Type::Deadline};
        w.p_.ticks = absolute_ticks;
        return w;
    }

    static WaitCondition comm(CommunicationInterface& c)
    {
        WaitCondition w{Type::Comm};
        w.p_.comm = &c;
        return w;
    }

    static WaitCondition irq(int16_t irqn)
    {
        WaitCondition w{Type::IRQ};
        w.p_.irqn = irqn;
        return w;
    }

    static WaitCondition demand()
    {
        return WaitCondition{Type::Demand};
    }

    Type type() const
    {
        return type_;
    }
    uint32_t ms() const
    {
        return p_.ms;
    }
    uint32_t us() const
    {
        return p_.us;
    }
    uint64_t ticks() const
    {
        return p_.ticks;
    }
    CommunicationInterface* commInterface() const
    {
        return p_.comm;
    }
    int16_t irqn() const
    {
        return p_.irqn;
    }

    private:
    explicit WaitCondition(Type t) : type_(t), p_{}
    {
    }

    Type type_;

    union Payload {
        uint32_t                ms;
        uint32_t                us;
        uint64_t                ticks;  // largest member — zero-inits the union
        CommunicationInterface* comm;
        int16_t                 irqn;
        Payload() : ticks(0)
        {
        }
    } p_;
};

}  // namespace chipz

#endif  // CHIPZ_WAIT_CONDITION_HPP
