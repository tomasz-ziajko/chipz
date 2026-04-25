// Copyright (c) 2026 Tomasz Ziajko
// SPDX-License-Identifier: GPL-3.0-only
// Commercial license available — see README

/**
 * @file chipz_isrs.cpp
 * @brief All interrupt handlers and HAL weak callback overrides for STM32H5xx
 *
 * This file is the single source of truth for every ISR on the STM32H5xx.
 * It replaces both stm32h5xx_it.c and any cmake-generated chipz_isr_handlers.cpp.
 *
 * Weak symbol override
 * --------------------
 * Every function defined here is an extern "C" symbol without __weak, which
 * causes the linker to prefer these definitions over the __weak stubs in the
 * ST startup / HAL files.
 *
 * Communication peripherals (I2C, SPI, UART)
 * -------------------------------------------
 * The HAL callback calls notify*() on the matching chipz interface via a
 * CommunicationInterface* pointer. The pointers below are defined __weak so
 * that they default to nullptr — an application defines them in app.cpp (or
 * wherever the bus objects live) to override the weak default.
 *
 *   // In app.cpp:
 *   chipz::interfaces::SPIInterface<2> g_spi2{...};
 *   chipz::CommunicationInterface* g_spi2_iface = &g_spi2;
 *
 * All callbacks guard each notify call with a null-check so that unused
 * peripherals are silent.
 *
 * SysTick
 * -------
 * SysTick_Handler calls HAL_IncTick() (required by HAL timeout machinery) and
 * chipz_systick_tick() (application-defined, calls SysTickTimer::onSysTick()).
 */

#include <chipz/core/communication_interface.hpp>
#include <chipz/core/core.hpp>
#include <chipz/network/can/can_interface.hpp>

#include "irq.hpp"
#include "stm32h5xx_hal.h"

using chipz::port::stm32h5xx::IRQn;
using chipz::port::stm32h5xx::kIRQnFirst;
using chipz::port::stm32h5xx::kIRQnLast;

// ---------------------------------------------------------------------------
// External declarations
// ---------------------------------------------------------------------------

extern chipz::Core<IRQn, kIRQnFirst, kIRQnLast> g_core;
extern "C" void                                 chipz_systick_tick();

// HAL handles — __weak so that undefined instances resolve to address 0
extern "C" {
#ifdef HAL_I2C_MODULE_ENABLED
__attribute__((weak)) extern I2C_HandleTypeDef hi2c1;
__attribute__((weak)) extern I2C_HandleTypeDef hi2c2;
__attribute__((weak)) extern I2C_HandleTypeDef hi2c3;
#endif

#ifdef HAL_SPI_MODULE_ENABLED
__attribute__((weak)) extern SPI_HandleTypeDef hspi1;
__attribute__((weak)) extern SPI_HandleTypeDef hspi2;
__attribute__((weak)) extern SPI_HandleTypeDef hspi3;
__attribute__((weak)) extern SPI_HandleTypeDef hspi4;
#endif

#ifdef HAL_UART_MODULE_ENABLED
__attribute__((weak)) extern UART_HandleTypeDef huart1;
__attribute__((weak)) extern UART_HandleTypeDef huart2;
__attribute__((weak)) extern UART_HandleTypeDef huart3;
__attribute__((weak)) extern UART_HandleTypeDef huart4;
#endif

#ifdef HAL_FDCAN_MODULE_ENABLED
__attribute__((weak)) extern FDCAN_HandleTypeDef hfdcan1;
__attribute__((weak)) extern FDCAN_HandleTypeDef hfdcan2;
#endif
}

// ---------------------------------------------------------------------------
// chipz interface pointers — default weak nullptr; app.cpp overrides used buses
// ---------------------------------------------------------------------------

#ifdef HAL_I2C_MODULE_ENABLED
__attribute__((weak)) chipz::CommunicationInterface* g_i2c1_iface = nullptr;
__attribute__((weak)) chipz::CommunicationInterface* g_i2c2_iface = nullptr;
__attribute__((weak)) chipz::CommunicationInterface* g_i2c3_iface = nullptr;
#endif

#ifdef HAL_SPI_MODULE_ENABLED
__attribute__((weak)) chipz::CommunicationInterface* g_spi1_iface = nullptr;
__attribute__((weak)) chipz::CommunicationInterface* g_spi2_iface = nullptr;
__attribute__((weak)) chipz::CommunicationInterface* g_spi3_iface = nullptr;
__attribute__((weak)) chipz::CommunicationInterface* g_spi4_iface = nullptr;
#endif

#ifdef HAL_UART_MODULE_ENABLED
__attribute__((weak)) chipz::CommunicationInterface* g_uart1_iface = nullptr;
__attribute__((weak)) chipz::CommunicationInterface* g_uart2_iface = nullptr;
__attribute__((weak)) chipz::CommunicationInterface* g_uart3_iface = nullptr;
__attribute__((weak)) chipz::CommunicationInterface* g_uart4_iface = nullptr;
#endif

#ifdef HAL_FDCAN_MODULE_ENABLED
__attribute__((weak)) chipz::network::CANInterfaceBase* g_can1 = nullptr;
__attribute__((weak)) chipz::network::CANInterfaceBase* g_can2 = nullptr;
#endif

extern "C" {

// ---------------------------------------------------------------------------
// Cortex-M exception handlers
// ---------------------------------------------------------------------------

void NMI_Handler()
{
    while (1) {
    }
}
// HardFault_Handler, MemManage_Handler, BusFault_Handler, and UsageFault_Handler
// are defined in fault_handlers.cpp — add that file to your project source list.
void SVC_Handler()
{
}
void DebugMon_Handler()
{
}
void PendSV_Handler()
{
}

void SysTick_Handler()
{
    HAL_IncTick();
    chipz_systick_tick();
}

// ---------------------------------------------------------------------------
// GPIO EXTI line handlers — each line has a dedicated vector on STM32H5xx
// ---------------------------------------------------------------------------

void EXTI0_IRQHandler()
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
    g_core.onIRQ(IRQn::EXTI0);
}
void EXTI1_IRQHandler()
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_1);
    g_core.onIRQ(IRQn::EXTI1);
}
void EXTI2_IRQHandler()
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_2);
    g_core.onIRQ(IRQn::EXTI2);
}
void EXTI3_IRQHandler()
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_3);
    g_core.onIRQ(IRQn::EXTI3);
}
void EXTI4_IRQHandler()
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4);
    g_core.onIRQ(IRQn::EXTI4);
}
void EXTI5_IRQHandler()
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_5);
    g_core.onIRQ(IRQn::EXTI5);
}
void EXTI6_IRQHandler()
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_6);
    g_core.onIRQ(IRQn::EXTI6);
}
void EXTI7_IRQHandler()
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_7);
    g_core.onIRQ(IRQn::EXTI7);
}
void EXTI8_IRQHandler()
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_8);
    g_core.onIRQ(IRQn::EXTI8);
}
void EXTI9_IRQHandler()
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_9);
    g_core.onIRQ(IRQn::EXTI9);
}
void EXTI10_IRQHandler()
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_10);
    g_core.onIRQ(IRQn::EXTI10);
}
void EXTI11_IRQHandler()
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_11);
    g_core.onIRQ(IRQn::EXTI11);
}
void EXTI12_IRQHandler()
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_12);
    g_core.onIRQ(IRQn::EXTI12);
}
void EXTI13_IRQHandler()
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_13);
    g_core.onIRQ(IRQn::EXTI13);
}
void EXTI14_IRQHandler()
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_14);
    g_core.onIRQ(IRQn::EXTI14);
}
void EXTI15_IRQHandler()
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_15);
    g_core.onIRQ(IRQn::EXTI15);
}

// ---------------------------------------------------------------------------
// I2C IRQ handlers
// ---------------------------------------------------------------------------

#ifdef HAL_I2C_MODULE_ENABLED
void I2C1_EV_IRQHandler()
{
    HAL_I2C_EV_IRQHandler(&hi2c1);
}
void I2C1_ER_IRQHandler()
{
    HAL_I2C_ER_IRQHandler(&hi2c1);
}
void I2C2_EV_IRQHandler()
{
    HAL_I2C_EV_IRQHandler(&hi2c2);
}
void I2C2_ER_IRQHandler()
{
    HAL_I2C_ER_IRQHandler(&hi2c2);
}
void I2C3_EV_IRQHandler()
{
    HAL_I2C_EV_IRQHandler(&hi2c3);
}
void I2C3_ER_IRQHandler()
{
    HAL_I2C_ER_IRQHandler(&hi2c3);
}
#endif

// ---------------------------------------------------------------------------
// SPI IRQ handlers
// ---------------------------------------------------------------------------

#ifdef HAL_SPI_MODULE_ENABLED
void SPI1_IRQHandler()
{
    HAL_SPI_IRQHandler(&hspi1);
}
void SPI2_IRQHandler()
{
    HAL_SPI_IRQHandler(&hspi2);
}
void SPI3_IRQHandler()
{
    HAL_SPI_IRQHandler(&hspi3);
}
void SPI4_IRQHandler()
{
    HAL_SPI_IRQHandler(&hspi4);
}
#endif

// ---------------------------------------------------------------------------
// UART IRQ handlers
// ---------------------------------------------------------------------------

#ifdef HAL_UART_MODULE_ENABLED
void USART1_IRQHandler()
{
    HAL_UART_IRQHandler(&huart1);
}
void USART2_IRQHandler()
{
    HAL_UART_IRQHandler(&huart2);
}
void USART3_IRQHandler()
{
    HAL_UART_IRQHandler(&huart3);
}
void UART4_IRQHandler()
{
    HAL_UART_IRQHandler(&huart4);
}
#endif

// ---------------------------------------------------------------------------
// FDCAN IRQ handlers
// ---------------------------------------------------------------------------

#ifdef HAL_FDCAN_MODULE_ENABLED
void FDCAN1_IT0_IRQHandler()
{
    HAL_FDCAN_IRQHandler(&hfdcan1);
}
void FDCAN1_IT1_IRQHandler()
{
    HAL_FDCAN_IRQHandler(&hfdcan1);
}
void FDCAN2_IT0_IRQHandler()
{
    HAL_FDCAN_IRQHandler(&hfdcan2);
}
void FDCAN2_IT1_IRQHandler()
{
    HAL_FDCAN_IRQHandler(&hfdcan2);
}
#endif

}  // extern "C"

// ---------------------------------------------------------------------------
// HAL I2C weak callback overrides
// ---------------------------------------------------------------------------

#ifdef HAL_I2C_MODULE_ENABLED

extern "C" void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef* h)
{
    if (&hi2c1 && h == &hi2c1 && g_i2c1_iface) { g_i2c1_iface->notifyTransferComplete(true); return; }
    if (&hi2c2 && h == &hi2c2 && g_i2c2_iface) { g_i2c2_iface->notifyTransferComplete(true); return; }
    if (&hi2c3 && h == &hi2c3 && g_i2c3_iface) { g_i2c3_iface->notifyTransferComplete(true); return; }
}

extern "C" void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef* h)
{
    if (&hi2c1 && h == &hi2c1 && g_i2c1_iface) { g_i2c1_iface->notifyTransferComplete(true); return; }
    if (&hi2c2 && h == &hi2c2 && g_i2c2_iface) { g_i2c2_iface->notifyTransferComplete(true); return; }
    if (&hi2c3 && h == &hi2c3 && g_i2c3_iface) { g_i2c3_iface->notifyTransferComplete(true); return; }
}

extern "C" void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef* h)
{
    if (&hi2c1 && h == &hi2c1 && g_i2c1_iface) { g_i2c1_iface->notifyTransferComplete(true); return; }
    if (&hi2c2 && h == &hi2c2 && g_i2c2_iface) { g_i2c2_iface->notifyTransferComplete(true); return; }
    if (&hi2c3 && h == &hi2c3 && g_i2c3_iface) { g_i2c3_iface->notifyTransferComplete(true); return; }
}

extern "C" void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef* h)
{
    if (&hi2c1 && h == &hi2c1 && g_i2c1_iface) { g_i2c1_iface->notifyTransferComplete(true); return; }
    if (&hi2c2 && h == &hi2c2 && g_i2c2_iface) { g_i2c2_iface->notifyTransferComplete(true); return; }
    if (&hi2c3 && h == &hi2c3 && g_i2c3_iface) { g_i2c3_iface->notifyTransferComplete(true); return; }
}

extern "C" void HAL_I2C_ErrorCallback(I2C_HandleTypeDef* h)
{
    if (&hi2c1 && h == &hi2c1 && g_i2c1_iface) { g_i2c1_iface->notifyError(); return; }
    if (&hi2c2 && h == &hi2c2 && g_i2c2_iface) { g_i2c2_iface->notifyError(); return; }
    if (&hi2c3 && h == &hi2c3 && g_i2c3_iface) { g_i2c3_iface->notifyError(); return; }
}

extern "C" void HAL_I2C_AbortCpltCallback(I2C_HandleTypeDef* h)
{
    if (&hi2c1 && h == &hi2c1 && g_i2c1_iface) { g_i2c1_iface->notifyError(); return; }
    if (&hi2c2 && h == &hi2c2 && g_i2c2_iface) { g_i2c2_iface->notifyError(); return; }
    if (&hi2c3 && h == &hi2c3 && g_i2c3_iface) { g_i2c3_iface->notifyError(); return; }
}

#endif  // HAL_I2C_MODULE_ENABLED

// ---------------------------------------------------------------------------
// HAL SPI weak callback overrides
// ---------------------------------------------------------------------------

#ifdef HAL_SPI_MODULE_ENABLED

extern "C" void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef* h)
{
    if (&hspi1 && h == &hspi1 && g_spi1_iface) { g_spi1_iface->notifyTransferComplete(true); return; }
    if (&hspi2 && h == &hspi2 && g_spi2_iface) { g_spi2_iface->notifyTransferComplete(true); return; }
    if (&hspi3 && h == &hspi3 && g_spi3_iface) { g_spi3_iface->notifyTransferComplete(true); return; }
    if (&hspi4 && h == &hspi4 && g_spi4_iface) { g_spi4_iface->notifyTransferComplete(true); return; }
}

extern "C" void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef* h)
{
    if (&hspi1 && h == &hspi1 && g_spi1_iface) { g_spi1_iface->notifyTransferComplete(true); return; }
    if (&hspi2 && h == &hspi2 && g_spi2_iface) { g_spi2_iface->notifyTransferComplete(true); return; }
    if (&hspi3 && h == &hspi3 && g_spi3_iface) { g_spi3_iface->notifyTransferComplete(true); return; }
    if (&hspi4 && h == &hspi4 && g_spi4_iface) { g_spi4_iface->notifyTransferComplete(true); return; }
}

extern "C" void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef* h)
{
    if (&hspi1 && h == &hspi1 && g_spi1_iface) { g_spi1_iface->notifyTransferComplete(true); return; }
    if (&hspi2 && h == &hspi2 && g_spi2_iface) { g_spi2_iface->notifyTransferComplete(true); return; }
    if (&hspi3 && h == &hspi3 && g_spi3_iface) { g_spi3_iface->notifyTransferComplete(true); return; }
    if (&hspi4 && h == &hspi4 && g_spi4_iface) { g_spi4_iface->notifyTransferComplete(true); return; }
}

extern "C" void HAL_SPI_ErrorCallback(SPI_HandleTypeDef* h)
{
    if (&hspi1 && h == &hspi1 && g_spi1_iface) { g_spi1_iface->notifyError(); return; }
    if (&hspi2 && h == &hspi2 && g_spi2_iface) { g_spi2_iface->notifyError(); return; }
    if (&hspi3 && h == &hspi3 && g_spi3_iface) { g_spi3_iface->notifyError(); return; }
    if (&hspi4 && h == &hspi4 && g_spi4_iface) { g_spi4_iface->notifyError(); return; }
}

#endif  // HAL_SPI_MODULE_ENABLED

// ---------------------------------------------------------------------------
// HAL UART weak callback overrides
// ---------------------------------------------------------------------------

#ifdef HAL_UART_MODULE_ENABLED

extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef* h)
{
    if (&huart1 && h == &huart1 && g_uart1_iface) { g_uart1_iface->notifyTransferComplete(true); return; }
    if (&huart2 && h == &huart2 && g_uart2_iface) { g_uart2_iface->notifyTransferComplete(true); return; }
    if (&huart3 && h == &huart3 && g_uart3_iface) { g_uart3_iface->notifyTransferComplete(true); return; }
    if (&huart4 && h == &huart4 && g_uart4_iface) { g_uart4_iface->notifyTransferComplete(true); return; }
}

extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef* h)
{
    // notifyRxComplete() is virtual — dispatches to UARTInterface::notifyRxComplete()
    // which clears rx_in_progress_ before delegating to the base class
    if (&huart1 && h == &huart1 && g_uart1_iface) { g_uart1_iface->notifyRxComplete(); return; }
    if (&huart2 && h == &huart2 && g_uart2_iface) { g_uart2_iface->notifyRxComplete(); return; }
    if (&huart3 && h == &huart3 && g_uart3_iface) { g_uart3_iface->notifyRxComplete(); return; }
    if (&huart4 && h == &huart4 && g_uart4_iface) { g_uart4_iface->notifyRxComplete(); return; }
}

extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef* h)
{
    if (&huart1 && h == &huart1 && g_uart1_iface) { g_uart1_iface->notifyError(); return; }
    if (&huart2 && h == &huart2 && g_uart2_iface) { g_uart2_iface->notifyError(); return; }
    if (&huart3 && h == &huart3 && g_uart3_iface) { g_uart3_iface->notifyError(); return; }
    if (&huart4 && h == &huart4 && g_uart4_iface) { g_uart4_iface->notifyError(); return; }
}

extern "C" void HAL_UART_AbortCpltCallback(UART_HandleTypeDef* h)
{
    if (&huart1 && h == &huart1 && g_uart1_iface) { g_uart1_iface->notifyError(); return; }
    if (&huart2 && h == &huart2 && g_uart2_iface) { g_uart2_iface->notifyError(); return; }
    if (&huart3 && h == &huart3 && g_uart3_iface) { g_uart3_iface->notifyError(); return; }
    if (&huart4 && h == &huart4 && g_uart4_iface) { g_uart4_iface->notifyError(); return; }
}

#endif  // HAL_UART_MODULE_ENABLED

// ---------------------------------------------------------------------------
// HAL FDCAN weak callback overrides
// ---------------------------------------------------------------------------

#ifdef HAL_FDCAN_MODULE_ENABLED

extern "C" void HAL_FDCAN_TxBufferCompleteCallback(FDCAN_HandleTypeDef* h, uint32_t)
{
    if (&hfdcan1 && h == &hfdcan1 && g_can1) { g_can1->notifyTxComplete(); return; }
    if (&hfdcan2 && h == &hfdcan2 && g_can2) { g_can2->notifyTxComplete(); return; }
}

static void fdcan_rx_dispatch(FDCAN_HandleTypeDef* h, uint32_t fifo, chipz::network::CANInterfaceBase* iface)
{
    FDCAN_RxHeaderTypeDef hdr{};
    uint8_t               buf[64]{};
    if (HAL_FDCAN_GetRxMessage(h, fifo, &hdr, buf) != HAL_OK) {
        return;
    }
    uint8_t dlc    = static_cast<uint8_t>((hdr.DataLength >> 16U) & 0x0FU);
    uint8_t length = chipz::network::dlcToLength(dlc);
    iface->notifyRxComplete(hdr.Identifier, buf, length);
}

extern "C" void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef* h, uint32_t)
{
    if (&hfdcan1 && h == &hfdcan1 && g_can1) { fdcan_rx_dispatch(h, FDCAN_RX_FIFO0, g_can1); return; }
    if (&hfdcan2 && h == &hfdcan2 && g_can2) { fdcan_rx_dispatch(h, FDCAN_RX_FIFO0, g_can2); return; }
}

extern "C" void HAL_FDCAN_RxFifo1Callback(FDCAN_HandleTypeDef* h, uint32_t)
{
    if (&hfdcan1 && h == &hfdcan1 && g_can1) { fdcan_rx_dispatch(h, FDCAN_RX_FIFO1, g_can1); return; }
    if (&hfdcan2 && h == &hfdcan2 && g_can2) { fdcan_rx_dispatch(h, FDCAN_RX_FIFO1, g_can2); return; }
}

extern "C" void HAL_FDCAN_ErrorCallback(FDCAN_HandleTypeDef* h)
{
    if (&hfdcan1 && h == &hfdcan1 && g_can1) { g_can1->notifyError(); return; }
    if (&hfdcan2 && h == &hfdcan2 && g_can2) { g_can2->notifyError(); return; }
}

extern "C" void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef* h, uint32_t)
{
    if (&hfdcan1 && h == &hfdcan1 && g_can1) { g_can1->notifyError(); return; }
    if (&hfdcan2 && h == &hfdcan2 && g_can2) { g_can2->notifyError(); return; }
}

#endif  // HAL_FDCAN_MODULE_ENABLED
