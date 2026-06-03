#include "UartLogger.hpp"
#include <cstring>

UartLogger& UartLogger::getInstance() {
    static UartLogger instance;
    return instance;
}

UartLogger::UartLogger() {
    // NULL attribute: uses default settings (priority inheritance not explicitly enabled)
    // mutex_ = osMutexNew(NULL);

    // Explicitly enable priority inheritance to prevent priority inversion on UART access
    static const osMutexAttr_t uart_mutex_attr = {
        "UartLoggerMutex",
        osMutexPrioInherit,
        NULL,
        0
    };
    mutex_ = osMutexNew(&uart_mutex_attr);
}

void UartLogger::log(const char* msg) {
    osMutexAcquire(mutex_, osWaitForever);
    HAL_UART_Transmit(&huart2,
        (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
    osMutexRelease(mutex_);
}
