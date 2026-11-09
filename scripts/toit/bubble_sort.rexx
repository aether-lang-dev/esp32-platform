/* Copyright (C) 2021 Toitware ApS.
 * Use of this source code is governed by the Zero-Clause BSD license
 * in LICENSE in this directory.
 * Adapted from Toit examples/bubble_sort.toit.
 */
input = '0 -1 2 -3 4 -4 6 -2 1 -9 10 0 -12 13 2'
size = WORDS(input)
SAY 'Bubble sort'
SAY '  before:' input

DO i = 1 TO size
  item.i = WORD(input, i) + 0
END
DO i = 1 TO size - 1
  DO j = 1 TO size - i
    next = j + 1
    IF item.j > item.next THEN DO
      saved = item.j
      item.j = item.next
      item.next = saved
    END
  END
END

sorted = item.1
DO i = 2 TO size
  sorted = sorted item.i
END
SAY '  after: ' sorted
IF sorted <> '-12 -9 -4 -3 -2 -1 0 0 1 2 2 4 6 10 13' THEN EXIT 1
EXIT 0
