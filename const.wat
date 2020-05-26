(module
  (memory (export "mem") 1 1)

  ;; This generates a constant that is easy to synthesize a number of
  ;; times.  Currently ion does not synthesize it but loads it
  ;; rip-relative.  We want to evaluate whether synthesizing it is
  ;; worthwhile.

  (func $ffoo (param $count i32) (result v128)
    (local $sum v128)
    (loop $L
      (local.set $sum
        (i16x8.add
          (i16x8.add
            (i16x8.add
              (i16x8.add
                (i16x8.add
                (i16x8.add
                  (i16x8.add
                    (i16x8.add
                      (i16x8.add
                        (i16x8.add
                          (v128.const i16x8 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00)
                          (v128.const i16x8 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00))
                        (v128.const i16x8 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00))
                      (v128.const i16x8 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00))
                    (v128.const i16x8 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00))
                  (v128.const i16x8 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00))
                (v128.const i16x8 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00))
              (v128.const i16x8 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00))
            (v128.const i16x8 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00))
          (v128.const i16x8 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00 0xff00))
	(local.get $sum)))
      (br_if $L (i32.eqz (i32.eqz (local.tee $count (i32.sub (local.get $count) (i32.const 1)))))))
    (local.get $sum))

  (func (export "run_ffoo") (param $count i32)
    (v128.store (i32.const 0) (call $ffoo (local.get $count)))))

		

