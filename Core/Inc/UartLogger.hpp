#ifndef INC_UARTLOGGER_HPP_
#define INC_UARTLOGGER_HPP_

#include "cmsis_os.h"
#include "usart.h"
#include <cstdint>

class UartLogger {
public:
    static UartLogger& getInstance();
    void log(const char* msg);

private:
    UartLogger();
    osMutexId_t mutex_;
};

#endif /* INC_UARTLOGGER_HPP_ */
