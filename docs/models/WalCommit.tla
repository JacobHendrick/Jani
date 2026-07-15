------------------------------ MODULE WalCommit ------------------------------
(* Sector-level model of the Jani Phase 2 WAL commit/recovery protocol.      *)
(* Design doc: docs/superpowers/specs/2026-07-15-wal-tla-model-design.md     *)
(*                                                                           *)
(* Disk layout (one model "sector" holds one abstract value):                *)
(*   <<"sb", 0>>    superblock: WAL slot recovery starts scanning from       *)
(*   <<"wal", i>>   WAL sectors; record slot s occupies sectors 2s-1 and 2s  *)
(*   <<"data", i>>  version payload extents, one per op id, never reused     *)
(*   <<"home", o>>  object-table home entry for object o (an op id)          *)
(*                                                                           *)
(* Commit protocol for op id writing v to o (writes go through the volatile  *)
(* cache `pending`; only Flush makes them durable):                          *)
(*   Begin -> WritePayload -> AppendRecord -> Flush -> AckCommit             *)
(* Checkpoint (lazy, any time): ApplyHome* -> Flush -> WriteSB.              *)
(* Truncation IS the superblock advance: recovery never scans slots before   *)
(* the durable sb pointer, so those slots are logically free.                *)
(* Recovery replays the WAL strictly in slot order: the durable sb pointer   *)
(* may lag the homes, so replay can regress an object before re-advancing    *)
(* it - in-order application makes the final state correct.                  *)
EXTENDS Naturals, Sequences, FiniteSets

CONSTANTS Objects, Vals, MaxOps, MaxCrashes,
          BUGGY_NO_WAL_FLUSH,    \* ack without waiting for the WAL flush
          BUGGY_TRUNCATE_FIRST,  \* advance sb before homes are durable
          BUGGY_SKIP_CHECKSUM    \* recovery trusts part1 without validation

WalSlots == MaxOps           \* each op appends at most one record
Free == "free"

\* TLC forbids comparing a record with a plain value, so "no op in flight"
\* is a record too, discriminated by st = "idle"
IdleOp == [id |-> 0, obj |-> CHOOSE o \in Objects : TRUE,
           val |-> CHOOSE v \in Vals : TRUE, slot |-> 0, st |-> "idle"]

Sectors ==
      {<<"sb", 0>>}
  \cup {<<"wal", i>>  : i \in 1..(2 * WalSlots)}
  \cup {<<"data", i>> : i \in 1..MaxOps}
  \cup {<<"home", o>> : o \in Objects}

VARIABLES
  disk,     \* [Sectors -> content]: what is actually on the platter
  pending,  \* volatile write cache: partial function sector -> content
  phase,    \* "running" | "down"
  mtable,   \* in-memory object table: [Objects -> 0..MaxOps], 0 = no version
  dirty,    \* objects whose home sector is behind mtable
  walNext,  \* in-memory: next free WAL record slot
  op,       \* in-flight operation record; st = "idle" when none
  nextOp,   \* next op id
  crashes,  \* crash budget spent
  hstore,   \* history: committed values per object (refinement witness)
  acked     \* history: acknowledged op ids

vars == <<disk, pending, phase, mtable, dirty, walNext, op, nextOp,
          crashes, hstore, acked>>

(* ------------------------------- helpers ---------------------------------- *)

Merge(f, g) == [s \in DOMAIN f |-> IF s \in DOMAIN g THEN g[s] ELSE f[s]]

Put(f, s, c) == [x \in DOMAIN f \cup {s} |-> IF x = s THEN c ELSE f[x]]

EmptyCache == [s \in {} |-> Free]

\* what a read returns while running: cache first, then platter
Content(s) == IF s \in DOMAIN pending THEN pending[s] ELSE disk[s]

Last(seq) == seq[Len(seq)]

(* A WAL record for op id writing v to o:                                     *)
(*   part1 at <<"wal", 2s-1>> = <<"r1", id, o, v>>                            *)
(*   part2 at <<"wal", 2s>>   = <<"r2", id>>                                  *)
(* The payload lives at <<"data", id>> = <<id, v>>.                           *)
(* "Checksum valid" = both parts present, same op id, payload matches part1. *)

RecordValid(d, s) ==
  LET p1 == d[<<"wal", 2*s - 1>>]
      p2 == d[<<"wal", 2*s>>]
  IN /\ p1 # Free /\ p1[1] = "r1"
     /\ p2 # Free /\ p2[1] = "r2"
     /\ p1[2] = p2[2]
     /\ d[<<"data", p1[2]>>] = <<p1[2], p1[4]>>

\* the mutant "checksum" trusts part1 alone
LooksValid(d, s) ==
  IF BUGGY_SKIP_CHECKSUM
    THEN LET p1 == d[<<"wal", 2*s - 1>>] IN p1 # Free /\ p1[1] = "r1"
    ELSE RecordValid(d, s)

ScanStart(d) == d[<<"sb", 0>>]

\* length of the valid record prefix recovery will replay
ValidLen(d) ==
  LET start == ScanStart(d)
      Bound == WalSlots - start + 1
  IN CHOOSE n \in 0..Bound :
       /\ \A k \in 0..(n - 1) : LooksValid(d, start + k)
       /\ (n = Bound) \/ ~LooksValid(d, start + n)

HomeTable(d) ==
  [o \in Objects |-> IF d[<<"home", o>>] = Free THEN 0 ELSE d[<<"home", o>>]]

RECURSIVE ApplyFrom(_, _, _, _)
ApplyFrom(d, t, s, stop) ==
  IF s = stop THEN t
  ELSE LET p1 == d[<<"wal", 2*s - 1>>]
       IN ApplyFrom(d, [t EXCEPT ![p1[3]] = p1[2]], s + 1, stop)

\* the pure recovery function: table reconstructed from durable state alone
Replay(d) ==
  LET start == ScanStart(d)
  IN ApplyFrom(d, HomeTable(d), start, start + ValidLen(d))

ReplayNext(d) == ScanStart(d) + ValidLen(d)

\* would recovery from durable state d resurrect op id as current for o?
Recovers(d, id, o) == Replay(d)[o] = id

(* -------------------------------- actions --------------------------------- *)

Init ==
  /\ disk = [s \in Sectors |-> IF s = <<"sb", 0>> THEN 1 ELSE Free]
  /\ pending = EmptyCache
  /\ phase = "running"
  /\ mtable = [o \in Objects |-> 0]
  /\ dirty = {}
  /\ walNext = 1
  /\ op = IdleOp
  /\ nextOp = 1
  /\ crashes = 0
  /\ hstore = [o \in Objects |-> <<>>]
  /\ acked = {}

Begin(o, v) ==
  /\ phase = "running" /\ op.st = "idle" /\ nextOp <= MaxOps
  /\ op' = [id |-> nextOp, obj |-> o, val |-> v, slot |-> 0, st |-> "begun"]
  /\ nextOp' = nextOp + 1
  /\ UNCHANGED <<disk, pending, phase, mtable, dirty, walNext, crashes,
                 hstore, acked>>

WritePayload ==
  /\ phase = "running" /\ op.st = "begun"
  /\ pending' = Put(pending, <<"data", op.id>>, <<op.id, op.val>>)
  /\ op' = [op EXCEPT !.st = "payload"]
  /\ UNCHANGED <<disk, phase, mtable, dirty, walNext, nextOp, crashes,
                 hstore, acked>>

AppendRecord ==
  /\ phase = "running" /\ op.st = "payload"
  /\ walNext <= WalSlots
  /\ pending' = Put(Put(pending,
                    <<"wal", 2*walNext - 1>>, <<"r1", op.id, op.obj, op.val>>),
                    <<"wal", 2*walNext>>, <<"r2", op.id>>)
  /\ op' = [op EXCEPT !.slot = walNext, !.st = "logged"]
  /\ walNext' = walNext + 1
  /\ UNCHANGED <<disk, phase, mtable, dirty, nextOp, crashes, hstore, acked>>

Flush ==
  /\ phase = "running" /\ DOMAIN pending # {}
  /\ disk' = Merge(disk, pending)
  /\ pending' = EmptyCache
  /\ UNCHANGED <<phase, mtable, dirty, walNext, op, nextOp, crashes,
                 hstore, acked>>

\* record AND payload are on the platter - the commit point
InFlightDurable ==
  /\ RecordValid(disk, op.slot)
  /\ disk[<<"wal", 2*op.slot - 1>>][2] = op.id

AckCommit ==
  /\ phase = "running" /\ op.st = "logged"
  /\ IF BUGGY_NO_WAL_FLUSH THEN TRUE ELSE InFlightDurable
  /\ acked' = acked \cup {op.id}
  /\ hstore' = [hstore EXCEPT ![op.obj] = Append(@, op.val)]
  /\ mtable' = [mtable EXCEPT ![op.obj] = op.id]
  /\ dirty' = dirty \cup {op.obj}
  /\ op' = IdleOp
  /\ UNCHANGED <<disk, pending, phase, walNext, nextOp, crashes>>

ApplyHome(o) ==
  /\ phase = "running" /\ o \in dirty
  /\ pending' = Put(pending, <<"home", o>>, mtable[o])
  /\ dirty' = dirty \ {o}
  /\ UNCHANGED <<disk, phase, mtable, walNext, op, nextOp, crashes,
                 hstore, acked>>

HomesDurable ==
  /\ dirty = {}
  /\ \A o \in Objects : <<"home", o>> \notin DOMAIN pending

(* Checkpoint truncation = advancing the superblock scan pointer. Two rules:  *)
(* 1. Homes must be durable first, or the skipped records' effects are lost. *)
(* 2. Never advance past an in-flight logged record - it may still commit.   *)
WriteSB ==
  /\ phase = "running"
  /\ <<"sb", 0>> \notin DOMAIN pending
  /\ walNext > ScanStart(disk)
  /\ IF BUGGY_TRUNCATE_FIRST THEN TRUE ELSE HomesDurable
  /\ op.st \in {"idle", "begun", "payload"}
  /\ pending' = Put(pending, <<"sb", 0>>, walNext)
  /\ UNCHANGED <<disk, phase, mtable, dirty, walNext, op, nextOp, crashes,
                 hstore, acked>>

\* TODO(Task 4, written by Jacob): Crash - subset of pending survives,
\* history updated iff recovery of the post-crash disk resurrects the
\* in-flight op.

Recover ==
  /\ phase = "down"
  /\ mtable' = Replay(disk)
  /\ walNext' = ReplayNext(disk)
  /\ dirty' = {o \in Objects : HomeTable(disk)[o] # Replay(disk)[o]}
  /\ phase' = "running"
  /\ UNCHANGED <<disk, pending, op, nextOp, crashes, hstore, acked>>

Next ==
  \/ \E o \in Objects, v \in Vals : Begin(o, v)
  \/ WritePayload \/ AppendRecord \/ Flush \/ AckCommit
  \/ \E o \in Objects : ApplyHome(o)
  \/ WriteSB
  \/ Recover
  \* TODO(Task 4): add Crash here

Spec == Init /\ [][Next]_vars

(* ------------------------------ invariants -------------------------------- *)

TypeOK ==
  /\ phase \in {"running", "down"}
  /\ mtable \in [Objects -> 0..MaxOps]
  /\ dirty \subseteq Objects
  /\ walNext \in 1..(WalSlots + 1)
  /\ nextOp \in 1..(MaxOps + 1)
  /\ crashes \in 0..MaxCrashes
  /\ acked \subseteq 1..MaxOps
  /\ hstore \in [Objects -> Seq(Vals)]
  /\ DOMAIN pending \subseteq Sectors

\* the current version every reader sees is exactly the last committed value
HistoryMatch ==
  phase = "running" =>
    \A o \in Objects :
      IF hstore[o] = <<>>
        THEN mtable[o] = 0
        ELSE /\ mtable[o] # 0
             /\ Content(<<"data", mtable[o]>>) = <<mtable[o], Last(hstore[o])>>

\* TODO(Task 4, written by Jacob): NoTornVersionVisible

\* TODO(Task 5): refinement instance (AbsSpec) and DurableRecoverable
================================================================================
