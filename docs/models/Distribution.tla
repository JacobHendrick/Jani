------------------------- MODULE Distribution -------------------------
EXTENDS Naturals, FiniteSets, TLC
CONSTANT Broken
Nodes == {1, 2}
Empty == [stamp |-> 0, history |-> {}, added |-> {}, removed |-> {}]
VARIABLES winner, seen, adds, removes, pending
vars == <<winner, seen, adds, removes, pending>>
Other(n) == 3 - n
Maximum(s) == CHOOSE x \in s : \A y \in s : x >= y
Init ==
    /\ winner = [n \in Nodes |-> n]
    /\ seen = [n \in Nodes |-> {n}]
    /\ adds = [n \in Nodes |-> {n}]
    /\ removes = [n \in Nodes |-> {}]
    /\ pending = [n \in Nodes |-> Empty]
Send(n) ==
    /\ pending[n] = Empty
    /\ pending' = [pending EXCEPT ![n] =
        [stamp |-> winner[n], history |-> seen[n],
         added |-> adds[n], removed |-> removes[n]]]
    /\ UNCHANGED <<winner, seen, adds, removes>>
Deliver(n) ==
    /\ pending[n] # Empty
    /\ LET d == Other(n)
           m == pending[n]
       IN /\ seen' = [seen EXCEPT ![d] = @ \cup m.history]
          /\ winner' = [winner EXCEPT ![d] =
              IF Broken THEN m.stamp ELSE Maximum(seen[d] \cup m.history)]
          /\ adds' = [adds EXCEPT ![d] = @ \cup m.added]
          /\ removes' = [removes EXCEPT ![d] = @ \cup m.removed]
    /\ pending' = [pending EXCEPT ![n] = Empty]
Remove(n) ==
    /\ adds[n] \ removes[n] # {}
    /\ \E tags \in SUBSET (adds[n] \ removes[n]) :
        /\ tags # {}
        /\ removes' = [removes EXCEPT ![n] = @ \cup tags]
    /\ UNCHANGED <<winner, seen, adds, pending>>
Next == \E n \in Nodes : Send(n) \/ Deliver(n) \/ Remove(n)
NoRollback == \A n \in Nodes : winner[n] = Maximum(seen[n])
ValidRemoval == \A n \in Nodes : removes[n] \subseteq adds[n]
Equal == /\ winner[1] = winner[2]
         /\ adds[1] = adds[2]
         /\ removes[1] = removes[2]
Convergence == <>[]Equal
Spec == Init /\ [][Next]_vars /\
    \A n \in Nodes : WF_vars(Send(n)) /\ WF_vars(Deliver(n))
=======================================================================
