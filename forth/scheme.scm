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

(t-mask 12 max->bitmask)

(obj-tag @)
(obj-tag! !)
(port-buf 1 nth-word)
(port-name 2 nth-word)
(port-os-handle 3 nth-word)
(port-position 4 nth-word)

(grab buf-allocate buf!
      16 buf buf-resize!)
(put 10 buf byte!)
(get buf byte@)

(variables bu)

(iota-loop 1 -s 0 >=s & dup #x30 + bu byte! bu 1 + bu! ...)
(iota! bu! iota-loop drop)

(read-back-loop 0 > & 1 - bu 1 - bu! bu byte@ show drop ...)
(read-back bu! dup bu + bu! read-back-loop drop)

(newline 10 show-byte drop)
(space 32 show-byte drop)

(variables proc)
(do-times-loop 0 > & proc call 1 - ...)
(do-times proc! do-times-loop)

(os-stdout 1)
(os-check || os-error-message 2 os-write 2 os-exit)
(dump-bytes os-stdout os-write os-check drop)
(hallo "Whee" dump-bytes newline)
(main grab
      10 buf .bytes iota!
      10 buf .bytes read-back
      buf .bytes 10 show-bytes newline
      10 'hallo do-times
      t-mask show drop)
