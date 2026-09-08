# Two-tape palindrome checker over the alphabet {a,b,c}.
# Copy the input to tape 2, rewind tape 1, then read tape 1 forwards and
# tape 2 backwards in lockstep. Halts in state `yes` or `no`.
# Head movement is linear in the input length: compare with pal1.tm.
# abba -> yes, abca -> no.
tapes: 2
blank: _
start: copy
input: abba
# copy tape 1 to tape 2
copy (a,_) -> (a,a) (R,R) copy
copy (b,_) -> (b,b) (R,R) copy
copy (c,_) -> (c,c) (R,R) copy
copy (_,_) -> (_,_) (L,L) rewind
# move tape 1's head back to the first symbol; tape 2 stays on the last one
rewind (a,a) -> (a,a) (L,S) rewind
rewind (a,b) -> (a,b) (L,S) rewind
rewind (a,c) -> (a,c) (L,S) rewind
rewind (b,a) -> (b,a) (L,S) rewind
rewind (b,b) -> (b,b) (L,S) rewind
rewind (b,c) -> (b,c) (L,S) rewind
rewind (c,a) -> (c,a) (L,S) rewind
rewind (c,b) -> (c,b) (L,S) rewind
rewind (c,c) -> (c,c) (L,S) rewind
rewind (_,a) -> (_,a) (R,S) cmp
rewind (_,b) -> (_,b) (R,S) cmp
rewind (_,c) -> (_,c) (R,S) cmp
rewind (_,_) -> (_,_) (R,S) cmp
# compare forwards against backwards
cmp (a,a) -> (a,a) (R,L) cmp
cmp (b,b) -> (b,b) (R,L) cmp
cmp (c,c) -> (c,c) (R,L) cmp
cmp (_,_) -> (_,_) (S,S) yes
cmp (a,b) -> (a,b) (S,S) no
cmp (a,c) -> (a,c) (S,S) no
cmp (b,a) -> (b,a) (S,S) no
cmp (b,c) -> (b,c) (S,S) no
cmp (c,a) -> (c,a) (S,S) no
cmp (c,b) -> (c,b) (S,S) no
