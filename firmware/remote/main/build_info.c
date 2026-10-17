/* The build stamp; the only file that recompiles when it changes. */
#include "build_info.h"

#include "build_stamp.h"

const char *build_date(void)
{
    return HM_BUILD_DATE;
}

const char *build_time(void)
{
    return HM_BUILD_TIME;
}
