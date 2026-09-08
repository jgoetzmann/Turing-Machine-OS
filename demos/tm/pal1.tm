# Single-tape palindrome checker over the alphabet {a,b,c}.
# Erase the leftmost symbol, run to the right end, check that the rightmost
# symbol matches, erase it, run back, repeat. Halts in state `yes` or `no`
# (the program prints the final state's name when it is not `halt`).
# Head movement is quadratic in the input length: compare with pal2.tm.
# abba -> yes (tape ends empty), abca -> no.
blank: _
start: q0
input: abba
# pick up the leftmost symbol
q0 _ -> _ S yes
q0 a -> _ R ra
q0 b -> _ R rb
q0 c -> _ R rc
# run right carrying a / b / c
ra a -> a R ra
ra b -> b R ra
ra c -> c R ra
ra _ -> _ L ca
rb a -> a R rb
rb b -> b R rb
rb c -> c R rb
rb _ -> _ L cb
rc a -> a R rc
rc b -> b R rc
rc c -> c R rc
rc _ -> _ L cc
# compare with the rightmost symbol
ca a -> _ L back
ca b -> b S no
ca c -> c S no
ca _ -> _ S yes
cb b -> _ L back
cb a -> a S no
cb c -> c S no
cb _ -> _ S yes
cc c -> _ L back
cc a -> a S no
cc b -> b S no
cc _ -> _ S yes
# run back to the left end
back a -> a L back
back b -> b L back
back c -> c L back
back _ -> _ R q0
