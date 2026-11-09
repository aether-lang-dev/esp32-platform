/* Measure scheduler overhead on the board's monotonic clock. */
shortest = 0
longest = 0
total = 0
DO sample = 1 TO 5
  before = UPTIME_MS()
  CALL WAIT_MS 100
  elapsed = UPTIME_MS() - before
  IF elapsed < 100 THEN EXIT 1
  IF sample = 1 | elapsed < shortest THEN shortest = elapsed
  IF elapsed > longest THEN longest = elapsed
  total = total + elapsed
  SAY 'Timer sample' sample elapsed 'ms'
END
mean = total / 5
SAY 'Timer summary (ms): min' shortest 'mean' mean 'max' longest
EXIT 0
