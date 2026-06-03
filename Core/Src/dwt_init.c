/*
 * dwt_init.c
 *
 *  Created on: Jun 3, 2026
 *      Author: embershine
 */

#include "dwt_init.h"
#include "stm32f4xx.h"

void DWT_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}
