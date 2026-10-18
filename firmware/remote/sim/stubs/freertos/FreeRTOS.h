#pragma once
/* FreeRTOS for the simulator: one thread, so critical sections do nothing. */
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(mux) ((void)(mux))
#define taskEXIT_CRITICAL(mux)  ((void)(mux))
