--------------------------- MODULE WalCommitAbstract ---------------------------
(* The promise the WAL protocol must keep: commits are atomic and durable.   *)
(* An operation writes one value to one object. Once acknowledged it is      *)
(* never lost. A crash may atomically commit or discard the in-flight        *)
(* operation - never anything in between, never anything older.              *)
EXTENDS Naturals, Sequences

CONSTANTS Objects, Vals, MaxOps

VARIABLES store,    \* [Objects -> Seq(Vals)]: committed version history
          acked,    \* SUBSET 1..MaxOps: ops whose commit was acknowledged
          inflight, \* <<id, obj, val>> while an op is in flight, else NoOp
          nextOp    \* next op id to hand out

NoOp == <<>>
vars == <<store, acked, inflight, nextOp>>

TypeOK ==
  /\ store \in [Objects -> Seq(Vals)]
  /\ acked \subseteq 1..MaxOps
  /\ (inflight = NoOp) \/ (inflight \in (1..MaxOps) \X Objects \X Vals)
  /\ nextOp \in 1..(MaxOps + 1)

Init ==
  /\ store = [o \in Objects |-> <<>>]
  /\ acked = {}
  /\ inflight = NoOp
  /\ nextOp = 1

Begin(o, v) ==
  /\ inflight = NoOp
  /\ nextOp <= MaxOps
  /\ inflight' = <<nextOp, o, v>>
  /\ nextOp' = nextOp + 1
  /\ UNCHANGED <<store, acked>>

Commit ==
  /\ inflight # NoOp
  /\ store' = [store EXCEPT ![inflight[2]] = Append(@, inflight[3])]
  /\ acked' = acked \cup {inflight[1]}
  /\ inflight' = NoOp
  /\ UNCHANGED nextOp

(* A crash discards the in-flight op ...                                      *)
CrashLose ==
  /\ inflight' = NoOp
  /\ UNCHANGED <<store, acked, nextOp>>

(* ... or commits it without an acknowledgement ever being sent.              *)
CrashKeep ==
  /\ inflight # NoOp
  /\ store' = [store EXCEPT ![inflight[2]] = Append(@, inflight[3])]
  /\ inflight' = NoOp
  /\ UNCHANGED <<acked, nextOp>>

Next ==
  \/ \E o \in Objects, v \in Vals : Begin(o, v)
  \/ Commit
  \/ CrashLose
  \/ CrashKeep

Spec == Init /\ [][Next]_vars
================================================================================
