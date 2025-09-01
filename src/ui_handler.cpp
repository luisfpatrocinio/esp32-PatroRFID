/**
 * @file ui_handler.cpp
 * @author Luis Felipe Patrocinio
 * @brief Implementation of user interface feedback tasks (LED and Buzzer).
 * @date 2025-08-26
 */

#include "ui_handler.h"
#include "rtos_comm.h"

//==============================================================================
// BUZZER TASK
//==============================================================================
void buzzerTask(void *parameter)
{
    for (;;)
    {
        // Wait indefinitely for a signal to beep
        if (xSemaphoreTake(buzzerSemaphore, portMAX_DELAY) == pdTRUE)
        {
            if (soundEnabled)
            {
                digitalWrite(BUZZER_PIN, HIGH);
                vTaskDelay(pdMS_TO_TICKS(100));
                digitalWrite(BUZZER_PIN, LOW);
            }
        }
    }
}

//==============================================================================
// LED STATUS TASK
//==============================================================================
void ledTask(void *parameter)
{
    for (;;)
    {
        if (!bluetoothConnected)
        {
            // Slow blink when disconnected
            digitalWrite(LED_PIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(250));
            digitalWrite(LED_PIN, LOW);
            vTaskDelay(pdMS_TO_TICKS(250));
        }
        else
        {
            // LED reflects read button state when connected
            digitalWrite(LED_PIN, (digitalRead(READ_BUTTON_PIN) == LOW) ? HIGH : LOW);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}