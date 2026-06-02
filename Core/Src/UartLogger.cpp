#include "UartLogger.hpp"
#include <cstring>

UartLogger& UartLogger::getInstance() {
    static UartLogger instance;
    return instance;
}

UartLogger::UartLogger() {
    mutex_ = osMutexNew(NULL);
}

void UartLogger::log(const char* msg) {
    osMutexAcquire(mutex_, osWaitForever);
    HAL_UART_Transmit(&huart2,
        (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
    osMutexRelease(mutex_);
}
