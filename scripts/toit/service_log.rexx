/* Copyright (C) 2022 Toitware ApS.
 * Use of this source code is governed by the Zero-Clause BSD license
 * in LICENSE in this directory.
 * Adapted from Toit examples/service/log.toit.
 */
CALL write_log 'Hello'
CALL write_log 'World'
EXIT 0

write_log: PROCEDURE
  PARSE ARG message
  uptime = UPTIME_MS()
  CALL LOG_INFO (uptime || ' ms: ' || message)
  RETURN
