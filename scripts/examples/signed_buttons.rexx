/* FNK0114B signed USB app. Grant TOUCH_EVENT and DISPLAY_BUTTONS. */
tap = TOUCH_EVENT()
message = 'Tap a button'
IF tap <> JSON_PARSE('null') THEN DO
  /* Portrait calibration from the FNK0114B vendor reference.
     Raw x=683 and 3464 map to screen x=210 and 30.
     Raw y=527 and 3552 map to screen y=30 and 290. */
  px = 210 - ((tap.x - 683) * 180 / 2781)
  py = 30 + ((tap.y - 527) * 260 / 3025)
  IF px < 16 | px >= 224 THEN EXIT 0
  SELECT
    WHEN py >= 64 & py < 128 THEN message = 'Hello, CYD!'
    WHEN py >= 160 & py < 224 THEN message = 'Rexx on ESP32'
    OTHERWISE EXIT 0
  END
  SAY 'CYD button' message
END
CALL DISPLAY_BUTTONS 'REXXAE TOUCH', 'HELLO', 'ABOUT', message
EXIT 0
