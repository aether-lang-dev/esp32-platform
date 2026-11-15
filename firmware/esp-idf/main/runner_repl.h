#ifndef RUNNER_REPL_H
#define RUNNER_REPL_H
#include "esp_http_server.h"
#include "runner_host.h"
esp_err_t runner_repl_start(httpd_handle_t server, RunnerAppsCommand apps);
void runner_repl_poll(void);
#endif
