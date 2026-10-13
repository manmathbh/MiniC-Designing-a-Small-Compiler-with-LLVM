; tests/ir/strength_input.ll
; Input IR for testing the StrengthReduction pass.
; Run with:
;   opt -load-pass-plugin=./StrengthReduction.so \
;       -passes="strength-reduction" -S \
;       strength_input.ll -o strength_output.ll
;   diff strength_output.ll strength_expected.ll

define i64 @mul_by_4(i64 %x) {
entry:
  ; x * 4 → should become x << 2
  %result = mul i64 %x, 4
  ret i64 %result
}

define i64 @mul_by_8(i64 %x) {
entry:
  ; x * 8 → should become x << 3
  %result = mul i64 %x, 8
  ret i64 %result
}

define i64 @mul_by_1(i64 %x) {
entry:
  ; x * 1 — power of two but shift by 0; instcombine handles this,
  ; our pass also transforms it (shl by 0 is legal, instcombine removes it)
  %result = mul i64 %x, 1
  ret i64 %result
}

define i64 @mul_not_pow2(i64 %x) {
entry:
  ; x * 6 — NOT a power of two, must NOT be transformed
  %result = mul i64 %x, 6
  ret i64 %result
}

define i64 @udiv_by_4(i64 %x) {
entry:
  ; udiv x, 4 → should become lshr x, 2
  %result = udiv i64 %x, 4
  ret i64 %result
}

define i64 @commutative_mul(i64 %x) {
entry:
  ; 8 * x (constant on left) → should become x << 3
  %result = mul i64 8, %x
  ret i64 %result
}
