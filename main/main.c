#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


void app_main(void)
{
    while(1){
        printf("Hello World\n");
        printf("FreeRTOS version: %s\n", tskKERNEL_VERSION_NUMBER);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}