#ifndef __MY_CLOCK_H_
#define __MY_CLOCK_H_

#include "stdint.h"

typedef struct
{
    uint32_t Hora;
    uint32_t Min;
    uint32_t Seg;
}CLOCK_ts;

int8_t clock_compare(CLOCK_ts* first, CLOCK_ts* second);
uint32_t clock_to_seconds(CLOCK_ts* clock_ptr);
void clock_update(CLOCK_ts* clock_ptr);

#endif //__MY_CLOCK_H_
