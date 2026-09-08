# Binary increment. The head starts on the most significant bit, walks to the
# right end, then adds one with carry from the right: 1011 -> 1100.
# The blank visited past the last bit is trimmed from the dump.
blank: _
start: right
input: 1011
right 0 -> 0 R right
right 1 -> 1 R right
right _ -> _ L carry
carry 1 -> 0 L carry
carry 0 -> 1 S halt
carry _ -> 1 S halt
