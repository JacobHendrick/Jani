--------------------------- MODULE Migration ---------------------------
EXTENDS TLC
CONSTANT Broken
VARIABLES source, target, fence, staged, published, acknowledged
vars == <<source, target, fence, staged, published, acknowledged>>
Init == /\ source = TRUE /\ target = FALSE /\ fence = FALSE
        /\ staged = FALSE /\ published = FALSE /\ acknowledged = FALSE
Stage == /\ ~published /\ staged' = TRUE
         /\ UNCHANGED <<source, target, fence, published, acknowledged>>
Fence == /\ ~fence /\ fence' = TRUE /\ source' = FALSE
         /\ UNCHANGED <<target, staged, published, acknowledged>>
Publish == /\ staged /\ ~published /\ (fence \/ Broken)
           /\ published' = TRUE /\ target' = TRUE
           /\ UNCHANGED <<source, fence, staged, acknowledged>>
Ack == /\ published /\ acknowledged' = TRUE
       /\ UNCHANGED <<source, target, fence, staged, published>>
CrashSource == /\ source' = ~fence
               /\ UNCHANGED <<target, fence, staged, published, acknowledged>>
CrashTarget == /\ target' = published /\ staged' = FALSE
               /\ UNCHANGED <<source, fence, published, acknowledged>>
Next == Stage \/ Fence \/ Publish \/ Ack \/ CrashSource \/ CrashTarget
NoDualOwner == ~(source /\ target)
DurableFence == fence => ~source
AuthorizedActivation == target => (published /\ fence)
Spec == Init /\ [][Next]_vars
=======================================================================
