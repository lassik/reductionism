;; Scheme interpreter / closure compiler

(false 0)
(true 1)
(flag! 0 <> drop)
(ftrue & true)
(flag ftrue || false)

(nth-cell  cells + @)
(nth-cell! cells + !)

(variables tmp)
(cell-nth tmp! cells tmp + @)

(newline 10 show-byte drop)
(space 32 show-byte drop)

;;;

(variables rows rows-cap rows-len)

(objects/row 4096)

(.bytes  @)
(.bytes! !)
(.cap    1 nth-cell)
(.cap!   1 nth-cell!)
(.len    2 nth-cell)
(.len!   2 nth-cell!)
(cells/buf      3)
(buf-size       cells/buf cells)
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

(cells/object 1 cells/buf +)
(obj-tag @)
(obj-tag! !)

(obj-type-null 0 = & drop t-null)
(obj-type-nonnull obj-tag t-mask and-bits)
(obj-type obj-type-null || obj-type-nonnull)

(obj-null? obj-type t-null = drop)

(string-buf 1 cells +)
(port-buf 1 nth-cell)
(port-name 2 nth-cell)
(port-os-handle 3 nth-cell)
(port-position 4 nth-cell)

;;; Allocate more objects

(bytes/row objects/row cells/object * cells)
(ensure-free-row rows-len > || 2 * ...)
(nth-row! cells rows + !)
(alloc-row  (new-rows r)
            rows-cap ensure-free-row rows-cap!
            rows-cap cells rows reallocate rows!
            rows-len cells rows + new-rows!
            new-rows rows-cap rows-len - zero-cells
            rows-len r! r 1 + rows-len!
            bytes/row allocate r nth-row!
            r)

(variables row-base row col)
(nth-row cells rows + @)
(row-nth-obj cells/object cells * row-base +)
(empty-cell? row-nth-obj obj-null?)
(loop-cols col objects/row < drop & col empty-cell? ||
           col 1 + col! ...)
(loop-rows row rows-len < drop &
           row nth-row row-base! 0 col! loop-cols || row 1 + row! ...)
(find-free-obj 0 row! loop-rows & row col)

(gc-verbose? true flag!)
(announce-new-row gc-verbose? & "Allocating new row" show-bytes newline)
(maybe-new-row || announce-new-row alloc-row 0)

(row-col->addr (row col) col! row!
               cells/object cells col * row nth-row +)
(reserve-obj (tag obj)
             tag!
             find-free-obj maybe-new-row row-col->addr obj!
             tag obj obj-tag!
             obj)

(bytes->buf (bytes len buf)
            len! bytes! buf-allocate buf!
            bytes buf .bytes!
            len buf .len!
            buf)

(mk-string (len bytes obj buf)
           len! bytes!
           t-string reserve-obj obj!
           obj string-buf buf!
           len buf .len!
           bytes buf .bytes!
           obj)

;;;

(variables bu)

(iota-loop 1 -s 0 >=s & dup #x30 + bu byte! bu 1 + bu! ...)
(iota! bu! iota-loop drop)

(read-back-loop 0 > & 1 - bu 1 - bu! bu byte@ show drop ...)
(read-back bu! dup bu + bu! read-back-loop drop)

(variables proc)
(do-times-loop 0 > & proc call 1 - ...)
(do-times proc! do-times-loop drop)

(os-stdout 1)
(os-check || os-error-message 2 os-write 2 os-exit)
(dump-bytes os-stdout os-write os-check drop)
(hallo "Whee" dump-bytes newline)

(buf->bytes-len (buf) buf! buf .bytes buf .len)
(dump-buf buf->bytes-len dump-bytes)

(d-string t-string = & drop string-buf dump-buf)
(d-bad-obj drop "#<bad object>" dump-bytes)
(display dup obj-type d-string || d-bad-obj)

(main 16 rows-cap!
      10 'hallo do-times
      "foo bar" mk-string display newline
      "baz qux" mk-string display newline
      "foo bar" mk-string display newline
      "baz qux" mk-string display newline)
