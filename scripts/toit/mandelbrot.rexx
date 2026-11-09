/* Copyright (C) 2021 Toitware ApS.
 * Use of this source code is governed by the Zero-Clause BSD license
 * in LICENSE in this directory.
 * Adapted from Toit examples/mandelbrot.toit.
 */
expected = '.....##. ..#.#### #######. ..#.#### .....##.'
SAY 'Mandelbrot'
DO row = 0 TO 4
  line = ''
  DO col = 0 TO 7
    x = (col - 4) * 0.33 - 0.6
    y = (2 - row) * 0.36
    real = 0
    imag = 0
    pixel = '#'
    DO iteration = 1 TO 8
      IF real * real + imag * imag > 4 THEN DO
        pixel = '.'
        LEAVE
      END
      nextReal = real * real - imag * imag + x
      imag = 2 * real * imag + y
      real = nextReal
    END
    line = line || pixel
  END
  SAY line
  IF line <> WORD(expected, row + 1) THEN EXIT 1
END
EXIT 0
