# 4-state busy beaver (Brady 1983): 107 steps, 13 ones.
blank: 0
start: A
A 0 -> 1 R B
A 1 -> 1 L B
B 0 -> 1 L A
B 1 -> 0 L C
C 0 -> 1 R halt
C 1 -> 1 L D
D 0 -> 1 R D
D 1 -> 0 R A
