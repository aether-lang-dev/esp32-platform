/* Deploy to the signed CYD USB runner with the TOUCH_EVENT grant. */
tap = TOUCH_EVENT()
IF tap = JSON_PARSE('null') THEN DO
  SAY 'Tap app ready'
  EXIT 0
END
SAY 'Tap received' tap.x tap.y
EXIT 0
