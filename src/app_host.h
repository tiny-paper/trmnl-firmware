#pragma once

// The running app's side of the firmware (apps.h, docs/apps.md): shared by the runner
// (app_runner.cpp: loads, starts, supervises) and the table of calls it hands the app
// (app_api_table.cpp). One app runs at a time.

#include <app_api.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#define APP_HOST_EVENT_QUEUE 32
#define APP_HOST_MAX_FILES 8

typedef struct {
  char name[32]; // from the ELF path, e.g. "hello"
  char dir[48]; // its files: /appdata/<name>
  QueueHandle_t events; // app_event_t, filled by the runner's input pump
  SemaphoreHandle_t done; // given once the app returned or called exit()
  volatile int code; // its result
  SemaphoreHandle_t panel; // the panel driver and its IO expander: an app's refresh, the USB port's charger reads
  volatile bool screen_held; // the firmware's Bluetooth pairing screen is up: the app's refreshes wait
} app_host_t;

extern app_host_t app_host;

// The table, filled once
const app_api_t *app_api_table(void);

// Ends the app's task with `code` (from its task only): signals the runner, deletes the task
void app_host_finish(int code);

// Closes the files the app left open; called by the runner once the app ended
void app_host_close_files(void);
