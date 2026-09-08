# 3-state busy beaver, most-ones champion (Lin & Rado 1965): 6 ones in 14 steps.
# (The separate 21-step champion writes only 5 ones.)
blank: 0
start: A
A 0 -> 1 R B
A 1 -> 1 R halt
B 0 -> 0 R C
B 1 -> 1 R B
C 0 -> 1 L C
C 1 -> 1 L A
