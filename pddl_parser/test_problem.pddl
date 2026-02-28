(define (problem blocks-3)

  (:domain blocksworld)

  (:objects
    a - block
    b - block
    c - block
  )

  (:init
    (ontable a)
    (ontable b)
    (on c a)
    (clear b)
    (clear c)
    (arm-empty)
  )

  ;; Goal: stack a on b on c
  (:goal (and
    (on a b)
    (on b c)
  ))
)
