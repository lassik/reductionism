;; Scheme interpreter / closure compiler

(variables buf)
(cubes dup dup *s *s)
;;(init -1200 x!)

(nth-word  words + @)
(nth-word! words + !)

(.bytes  @)
(.bytes! !)
(.cap    1 nth-word)
(.cap!   1 nth-word!)
(.len    2 nth-word)
(.len!   2 nth-word!)
(buf-size       3 words)
(buf-allocate   buf-size allocate)
(buf-deallocate (bu) bu! bu .bytes deallocate bu deallocate)
(buf-resize!    (bu) bu! bu .bytes reallocate bu .bytes!)

(t-null 0)
(t-syntax 1)
(t-builtin 2)
(t-closure 3)
(t-integer 4)
(t-character 5)
(t-string 6)
(t-symbol 7)
(t-pair 8)
(t-unique 9)
(t-file-port 10)
(t-string-in-port 11)
(t-string-out-port 12)

(grab buf-allocate buf!
      16 buf buf-resize!)
(put 10 buf byte!)
(get buf byte@)

(show |.|)

(variables bu)

(iota-loop 0 > & dup bu byte! bu 1 + bu! 1 - ...)
(iota! bu! iota-loop drop)

(read-back-loop 0 > & 1 - bu 1 - bu! bu byte@ show drop ...)
(read-back bu! dup bu + bu! read-back-loop drop)

(main grab
      10 buf .bytes iota!
      10 buf .bytes read-back)
