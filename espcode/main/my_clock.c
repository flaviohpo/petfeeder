#include "my_clock.h"

void clock_update(CLOCK_ts* clock_ptr)
{
    clock_ptr->Seg++;
    if(clock_ptr->Seg >= 60)
    {
        clock_ptr->Seg = 0;
        clock_ptr->Min++;
        if(clock_ptr->Min >= 60)
        {
            clock_ptr->Min = 0;
            clock_ptr->Hora++;
            if(clock_ptr->Hora >= 24)
            {
                clock_ptr->Hora = 0;
            }
        }
    }
}

uint32_t clock_to_seconds(CLOCK_ts* clock_ptr)
{
    return (clock_ptr->Hora * 3600) + (clock_ptr->Min * 60) + clock_ptr->Seg;
}

/**
 * @brief 
 * @return -1 if first is before the second
 * @return 0 if is equal
 * @return 1 if first is after the second
 */
int8_t clock_compare(CLOCK_ts* first, CLOCK_ts* second)
{
    uint32_t seconds_first = clock_to_seconds(first);
    uint32_t seconds_second = clock_to_seconds(second);

    if(seconds_first > seconds_second)
    {
        return 1;
    }

    if(seconds_first < seconds_second)
    {
        return -1;
    }

    return 0;
}