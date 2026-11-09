/* Standalone smart-plug rehearsal using the configured LED as the load. */
holdMs = 1250
CALL LED_WRITE 1
started = UPTIME_MS()
deadline = started + holdMs
SAY 'Auto-off: load on'
DO WHILE UPTIME_MS() < deadline
  remaining = deadline - UPTIME_MS()
  IF remaining > 0 THEN CALL WAIT_MS MIN(remaining, 1000)
END
CALL LED_WRITE 0
elapsed = UPTIME_MS() - started
SAY 'Auto-off: load off after' elapsed 'ms'
IF elapsed < holdMs THEN EXIT 1
EXIT 0
