//Servor and LCD

#include <stdio.h>
#include <pigpio.h>
#include <timer.h>
#include <string.h>
#include <pthread.h>
#include "lcd.h"

#define START_BUTTON_PIN 22
#define INCREASE_BUTTON_PIN 23
#define DECREASE_BUTTON_PIN 24
#define SERVO_PIN 18 // Sử dụng số BCM
#define INTERVAL 0.1

#define LCD_WIDTH 16
#define PROGRESS_BAR_WIDTH (LCD_WIDTH - 2)

#define DEBOUNCE_DELAY 50 // 50 milliseconds debounce delay

int timer_minutes = 0;
int is_timer_running = 0;
int start_time = 0;

pthread_mutex_t lock;

void setupServo() {
    gpioSetMode(SERVO_PIN, PI_OUTPUT);
}

void rotateServo(int angle) {
    int pulseWidth = (int)(1000 + (angle / 180.0 * 1000)); // Calculate pulse width for servo
    gpioServo(SERVO_PIN, pulseWidth);
    gpioDelay(500000); // 500ms
}

void setupPins() {
    gpioSetMode(START_BUTTON_PIN, PI_INPUT);
    gpioSetMode(INCREASE_BUTTON_PIN, PI_INPUT);
    gpioSetMode(DECREASE_BUTTON_PIN, PI_INPUT);
    gpioSetPullUpDown(START_BUTTON_PIN, PI_PUD_UP);
    gpioSetPullUpDown(INCREASE_BUTTON_PIN, PI_PUD_UP);
    gpioSetPullUpDown(DECREASE_BUTTON_PIN, PI_PUD_UP);
}

void displayProgressBar(int percent) {
    int bar_length = (percent * PROGRESS_BAR_WIDTH) / 100;
    char progress_bar[LCD_WIDTH + 1];
    progress_bar[0] = '|';
    progress_bar[LCD_WIDTH - 1] = '|';
    for (int i = 1; i < LCD_WIDTH - 1; i++) {
        progress_bar[i] = (i <= bar_length) ? '#' : ' ';
    }
    progress_bar[LCD_WIDTH] = '\0';

    lcd_clear();
    lcd_print_at(0, 1, "!rehab!");
    lcd_print_at(1, 0, progress_bar);
}

void scrollText(const char* text) {
    char display_text[LCD_WIDTH + 1];
    int len = strlen(text);
    int pos = 0;

    while (1) {
        for (int i = 0; i < LCD_WIDTH; i++) {
            display_text[i] = (i + pos < len) ? text[i + pos] : ' ';
        }
        display_text[LCD_WIDTH] = '\0';
        lcd_print_at(1, 0, display_text);
        gpioDelay(300000); // 300ms

        pos = (pos + 1) % len;

        if (gpioRead(START_BUTTON_PIN) == PI_LOW || gpioRead(INCREASE_BUTTON_PIN) == PI_LOW || gpioRead(DECREASE_BUTTON_PIN) == PI_LOW) {
            break;
        }
    }
}

void updateLCD(const char* message) {
    lcd_clear();
    lcd_print_at(0, 0, message);
}

int debounceRead(int pin) {
    int state = gpioRead(pin);
    gpioDelay(DEBOUNCE_DELAY * 1000);
    return (state == gpioRead(pin)) ? state : PI_HIGH;
}

void* servoThread(void* arg) {
    while (1) {
        pthread_mutex_lock(&lock);
        if (is_timer_running) {
            int elapsed_time = (int)time(NULL) - start_time;
            int remaining_time = timer_minutes * 60 - elapsed_time;
            if (remaining_time <= 0) {
                rotateServo(0); // Đặt lại servo về 0 độ
                gpioDelay(1000000); // 1 giây
                printf("Timer done!\n");
                updateLCD("Timer done!");
                gpioDelay(5000000); // 5 giây
                is_timer_running = 0;
                timer_minutes = 0;

                updateLCD("!REHAB!");
                scrollText("~~~ Give me your phone ^^!");
            } else {
                int percent_remaining = (remaining_time * 100) / (timer_minutes * 60);
                displayProgressBar(percent_remaining);
                printf("Time remaining: %d:%02d\n", remaining_time / 60, remaining_time % 60);
            }
        }
        pthread_mutex_unlock(&lock);
        gpioDelay(100000); // 100ms
    }
    return NULL;
}

void* buttonThread(void* arg) {
    int last_start_button_state = PI_HIGH;
    int last_increase_button_state = PI_HIGH;
    int last_decrease_button_state = PI_HIGH;

    while (1) {
        int start_button_state = debounceRead(START_BUTTON_PIN);
        int increase_button_state = debounceRead(INCREASE_BUTTON_PIN);
        int decrease_button_state = debounceRead(DECREASE_BUTTON_PIN);

        pthread_mutex_lock(&lock);
        if (increase_button_state == PI_LOW && last_increase_button_state == PI_HIGH && !is_timer_running) {
            timer_minutes++;
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "Set: %d:00", timer_minutes);
            updateLCD(buffer);
            printf("Timer set to %d minutes\n", timer_minutes);
        }

        if (decrease_button_state == PI_LOW && last_decrease_button_state == PI_HIGH && !is_timer_running && timer_minutes > 0) {
            timer_minutes--;
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "Set: %d:00", timer_minutes);
            updateLCD(buffer);
            printf("Timer set to %d minutes\n", timer_minutes);
        }

        if (start_button_state == PI_LOW && last_start_button_state == PI_HIGH && !is_timer_running && timer_minutes > 0) {
            rotateServo(90);
            gpioDelay(1000000); // 1 giây
            printf("Starting timer for %d minutes\n", timer_minutes);
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "Start: %d:00", timer_minutes);
            updateLCD(buffer);
            start_time = (int)time(NULL);
            is_timer_running = 1;
        }
        pthread_mutex_unlock(&lock);

        last_start_button_state = start_button_state;
        last_increase_button_state = increase_button_state;
        last_decrease_button_state = decrease_button_state;

        gpioDelay(100000); // 100ms
    }
    return NULL;
}

int main(void) {
    if (gpioInitialise() < 0) {
        printf("Failed to setup pigpio!\n");
        return 1;
    }

    setupServo();
    lcd_init();
    setupPins();

    pthread_t servo_thread, button_thread;
    pthread_mutex_init(&lock, NULL);

    updateLCD("!!REHAB!!");
    scrollText("~~~ Give me your phone ^^!");

    if (pthread_create(&servo_thread, NULL, servoThread, NULL) != 0) {
        printf("Failed to create servo thread\n");
        return 1;
    }

    if (pthread_create(&button_thread, NULL, buttonThread, NULL) != 0) {
        printf("Failed to create button thread\n");
        return 1;
    }

    pthread_join(servo_thread, NULL);
    pthread_join(button_thread, NULL);

    pthread_mutex_destroy(&lock);
    gpioTerminate();

    return 0;
}
