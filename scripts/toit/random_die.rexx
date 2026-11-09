/* Copyright (C) 2023 Toitware ApS.
 * Use of this source code is governed by the Zero-Clause BSD license
 * in LICENSE in this directory.
 * Adapted from Toit examples/service/random.toit.
 */
DO i = 1 TO 10
  value = RANDOM_BELOW(100)
  IF value < 0 | value >= 100 THEN EXIT 1
  SAY 'random =' value
END
DO i = 1 TO 10
  value = RANDOM_BELOW(6)
  IF value < 0 | value >= 6 THEN EXIT 1
  SAY 'die roll =' value
END
EXIT 0
