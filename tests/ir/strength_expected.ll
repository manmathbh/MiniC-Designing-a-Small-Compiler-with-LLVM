; tests/ir/strength_expected.ll
; Expected IR AFTER StrengthReduction pass runs on strength_input.ll.
; All mul/udiv by constant powers-of-two become shifts.

define i64 @mul_by_4(i64 %x) {
entry:
  %result.shl = shl i64 %x, 2
  ret i64 %result.shl
}

define i64 @mul_by_8(i64 %x) {
entry:
  %result.shl = shl i64 %x, 3
  ret i64 %result.shl
}

define i64 @mul_by_1(i64 %x) {
entry:
  %result.shl = shl i64 %x, 0
  ret i64 %result.shl
}

define i64 @mul_not_pow2(i64 %x) {
entry:
  ; UNCHANGED — 6 is not a power of two
  %result = mul i64 %x, 6
  ret i64 %result
}

define i64 @udiv_by_4(i64 %x) {
entry:
  %result.lshr = lshr i64 %x, 2
  ret i64 %result.lshr
}

define i64 @commutative_mul(i64 %x) {
entry:
  %result.shl = shl i64 %x, 3
  ret i64 %result.shl
}
