/* RexxAE on an ESP32-S3: SAY, a host FUNCTION, a DO loop, budgeted steps. */
SAY 'RexxAE says hello from the ESP32-S3'
total = 0
DO i = 1 TO 10
  total = total + i
END
SAY 'sum 1..10 =' total
x = DOUBLE(21)
SAY 'DOUBLE(21) =' x
SAY 'DOUBLE(total) =' DOUBLE(total)
EXIT 0
