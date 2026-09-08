# 2-state busy beaver (Rado 1962): 6 steps, 4 ones.
# The 2-symbol tape uses '0' as the blank so the dump shows every visited cell.
blank: 0
start: A
A 0 -> 1 R B
A 1 -> 1 L B
B 0 -> 1 L A
B 1 -> 1 R halt
