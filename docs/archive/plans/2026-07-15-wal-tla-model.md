# WAL Commit/Recovery TLA+ Model Implementation Plan

**Goal:** A model-checked TLA+ specification proving the Phase 2 WAL commit/recovery protocol never loses or tears a committed object version across crashes, per blueprint rule R9.

## Execution status (2026-07-15)

- **Task 1 COMPLETE:** jar vendored, sha256 pinned
  (`936a262...e88`), download rule hardened after a security-review finding
  (verify on a `.tmp` path, then `mv` — a failed check must not leave a jar
  where make treats it as a valid target; pin is documented trust-on-first-use).
- **Task 2 COMPLETE:** abstract spec green, 712 states.
- **Task 3 COMPLETE:** concrete spec crash-free green,
  7,573 states, `TypeOK` + `HistoryMatch`. `make model-check` works.
- **Task 4 COMPLETE:** crash semantics use an arbitrary
  subset of pending writes, and `NoTornVersionVisible` checks every visible
  payload. TLC is green over 138,767 distinct crash/recovery states.
- **Tasks 5–7 COMPLETE (uncommitted)**: refinement and durability checks are
  green over 138,767 distinct states; all three protocol mutants produce the
  expected counterexamples; hosted PMM and heap tests remain green. Commits
  were intentionally left for a separate cleanup because the worktree mixes
  uncommitted Phase 1 code with the model changes.

**Deviation from the code below (already applied in the committed spec):**
TLC rejects comparing a record with a string, so `None == "none"` was
replaced by an `IdleOp` record (`st = "idle"`, other fields dummies) —
tests are `op.st = "idle"` / `op' = IdleOp`. The snippets in Tasks 4–5
below have been updated to match; trust the committed `WalCommit.tla`
over any remaining prose that says `None`.

**WARNING — mixed working tree:** the repo contains uncommitted Phase 1
heap work (untracked `kernel/mm/*`, `tools/hosted/*` files and Makefile
edits) that must NOT be swept into model commits. Never `git add -A`.
When a task modifies the Makefile's TLA+ section: save the working
Makefile aside, `git show HEAD:Makefile > Makefile`, apply only the TLA+
hunk, commit, then restore the saved copy (the TLA+ sections must end up
byte-identical so the residual diff stays purely Phase 1).

**Architecture:** Two specs in `docs/models/`: `WalCommitAbstract.tla` states the promise (atomic durable commits); `WalCommit.tla` models the protocol at sector level with a pending-write cache and subset-crash semantics, and refines the abstract spec via a history-variable mapping. TLC checks invariants plus the refinement; three flag-selected mutants prove the model can fail.

**Tech Stack:** TLA+ (raw actions, no PlusCal), TLC via vendored `tla2tools.jar` v1.7.4, system Java, GNU Make.

## Global Constraints

- Spec doc (authority): `docs/design/2026-07-15-wal-tla-model.md`
- Models live in `docs/models/` (blueprint R9); TLC module file name must equal module name
- Model constants for checking: 2 objects, 2 values, `MaxOps = 3`, `MaxCrashes = 2`
- Mutant flags are constants inside `WalCommit.tla` (`BUGGY_*`), never duplicated spec files
- `make model-check` must pass; `make model-check-negative` must find a violation for every mutant
- Task 4 defines the crash semantics and torn-version invariant.
- All commits from repo root `/home/jacobhendrick/Desktop/Jani`; commit only the files each task names (the working tree has unrelated uncommitted Phase 1 files — never `git add -A`)

---

### Task 1: Vendor TLC and Makefile plumbing

**Files:**
- Create: `third_party/tla2tools.jar` (downloaded, pinned)
- Modify: `Makefile` (append a TLA+ section at the end, after the `clean:` target)

**Interfaces:**
- Produces: `$(TLA_TOOLS)` make variable and download rule; later tasks add `model-check` / `model-check-negative` targets that depend on it.

- [x] **Step 1: Download the jar and record its hash**

```bash
mkdir -p third_party
curl -fL -o third_party/tla2tools.jar \
  https://github.com/tlaplus/tlaplus/releases/download/v1.7.4/tla2tools.jar
sha256sum third_party/tla2tools.jar
```

Expected: download succeeds; note the printed sha256 — it is pasted into the Makefile in Step 2.

- [x] **Step 2: Add the TLA+ section to the Makefile**

Append at the end of `Makefile` (replace `<PASTE-SHA256-HERE>` with Step 1's value):

```make
# --- TLA+ model checking (Phase 2, blueprint R9) -----------------------------
TLA_TOOLS := third_party/tla2tools.jar
TLA_TOOLS_URL := https://github.com/tlaplus/tlaplus/releases/download/v1.7.4/tla2tools.jar
TLA_TOOLS_SHA256 := <PASTE-SHA256-HERE>
TLC_FLAGS := -workers auto -deadlock -cleanup

$(TLA_TOOLS):
	mkdir -p third_party
	curl -fL -o $(TLA_TOOLS) $(TLA_TOOLS_URL)
	echo "$(TLA_TOOLS_SHA256)  $(TLA_TOOLS)" | sha256sum -c -
```

Note: `-deadlock` DISABLES deadlock reporting (finished behaviors where no action is enabled are expected, not errors); `-cleanup` removes TLC's states directory afterward.

- [x] **Step 3: Verify the hash check and that TLC runs**

```bash
echo "$(grep '^TLA_TOOLS_SHA256' Makefile | cut -d' ' -f3)  third_party/tla2tools.jar" | sha256sum -c -
java -cp third_party/tla2tools.jar tlc2.TLC -h | head -3
```

Expected: `third_party/tla2tools.jar: OK`, then TLC usage text (proves system Java can run it).

- [x] **Step 4: Commit (force-add if third_party is gitignored)**

```bash
git check-ignore third_party/tla2tools.jar && ADD="-f" || ADD=""
git add $ADD third_party/tla2tools.jar
git add Makefile
git commit -m "Vendor TLC (tla2tools 1.7.4) for R9 protocol models"
```

---

### Task 2: The abstract spec — the promise

**Files:**
- Create: `docs/models/WalCommitAbstract.tla`
- Create: `docs/models/WalCommitAbstract.cfg`

**Interfaces:**
- Produces: module `WalCommitAbstract` with constants `Objects, Vals, MaxOps` and variables `store, acked, inflight, nextOp`; actions `Begin(o,v)`, `Commit`, `CrashLose`, `CrashKeep`; `Spec`. Task 5 instantiates it with `INSTANCE WalCommitAbstract WITH store <- hstore, acked <- acked, inflight <- AbsInflight, nextOp <- nextOp`.

- [x] **Step 1: Write the spec**

`docs/models/WalCommitAbstract.tla`:

```tla
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

(* ... or commits it without an acknowledgement ever being sent.             *)
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
```

- [x] **Step 2: Write its TLC config**

`docs/models/WalCommitAbstract.cfg`:

```
CONSTANTS
  Objects = {o1, o2}
  Vals = {1, 2}
  MaxOps = 3
SPECIFICATION Spec
INVARIANT TypeOK
```

- [x] **Step 3: Run TLC — expect green**

```bash
cd docs/models && java -cp ../../third_party/tla2tools.jar tlc2.TLC \
  -workers auto -deadlock -cleanup -config WalCommitAbstract.cfg WalCommitAbstract.tla
```

Expected: `Model checking completed. No error has been found.` with a few hundred states at most. A parse error or invariant violation means a transcription mistake — fix before proceeding.

- [x] **Step 4: Commit**

```bash
git add docs/models/WalCommitAbstract.tla docs/models/WalCommitAbstract.cfg
git commit -m "R9: abstract WAL commit spec (the atomic-durable promise)"
```

---

### Task 3: The concrete spec — sectors, cache, commit path, checkpoint

**Files:**
- Create: `docs/models/WalCommit.tla` (everything except `Crash`, `NoTornVersionVisible`, and the Task 5 refinement/`DurableRecoverable` parts)
- Create: `docs/models/WalCommit.cfg`
- Modify: `Makefile` (add `model-check` target after the `$(TLA_TOOLS)` rule)

**Interfaces:**
- Consumes: `$(TLA_TOOLS)` from Task 1.
- Produces (used by Tasks 4–6): helpers `Merge(f,g)`, `Put(f,s,c)`, `Content(s)`, `RecordValid(d,s)`, `LooksValid(d,s)`, `ScanStart(d)`, `ValidLen(d)`, `HomeTable(d)`, `Replay(d)`, `ReplayNext(d)`, `Recovers(d,id,o)`, `Last(seq)`, `EmptyCache`, `None`, `Free`; variables exactly as declared below; constants `BUGGY_NO_WAL_FLUSH`, `BUGGY_TRUNCATE_FIRST`, `BUGGY_SKIP_CHECKSUM`.

- [x] **Step 1: Write the module**

`docs/models/WalCommit.tla` — note the two `TODO(Task N)` markers; they are placeholders the LATER tasks fill, and TLC is not asked to check them yet:

```tla
------------------------------ MODULE WalCommit ------------------------------
(* Sector-level model of the Jani Phase 2 WAL commit/recovery protocol.      *)
(* Design doc: docs/design/2026-07-15-wal-tla-model.md                    *)
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
None == "none"

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
  op,       \* in-flight operation record, or None
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
  /\ op = None
  /\ nextOp = 1
  /\ crashes = 0
  /\ hstore = [o \in Objects |-> <<>>]
  /\ acked = {}

Begin(o, v) ==
  /\ phase = "running" /\ op = None /\ nextOp <= MaxOps
  /\ op' = [id |-> nextOp, obj |-> o, val |-> v, slot |-> 0, st |-> "begun"]
  /\ nextOp' = nextOp + 1
  /\ UNCHANGED <<disk, pending, phase, mtable, dirty, walNext, crashes,
                 hstore, acked>>

WritePayload ==
  /\ phase = "running" /\ op # None /\ op.st = "begun"
  /\ pending' = Put(pending, <<"data", op.id>>, <<op.id, op.val>>)
  /\ op' = [op EXCEPT !.st = "payload"]
  /\ UNCHANGED <<disk, phase, mtable, dirty, walNext, nextOp, crashes,
                 hstore, acked>>

AppendRecord ==
  /\ phase = "running" /\ op # None /\ op.st = "payload"
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
  /\ phase = "running" /\ op # None /\ op.st = "logged"
  /\ IF BUGGY_NO_WAL_FLUSH THEN TRUE ELSE InFlightDurable
  /\ acked' = acked \cup {op.id}
  /\ hstore' = [hstore EXCEPT ![op.obj] = Append(@, op.val)]
  /\ mtable' = [mtable EXCEPT ![op.obj] = op.id]
  /\ dirty' = dirty \cup {op.obj}
  /\ op' = None
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
  /\ (op = None) \/ (op.st \in {"begun", "payload"})
  /\ pending' = Put(pending, <<"sb", 0>>, walNext)
  /\ UNCHANGED <<disk, phase, mtable, dirty, walNext, op, nextOp, crashes,
                 hstore, acked>>

\* TODO(Task 4, written by Jacob): Crash - subset of pending survives,
\* history updated iff recovery of the post-crash disk resurrects the
\* in-flight op. See the task for helper signatures and hints.

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
```

- [x] **Step 2: Write the TLC config (crash-free for now)**

`docs/models/WalCommit.cfg`:

```
CONSTANTS
  Objects = {o1, o2}
  Vals = {1, 2}
  MaxOps = 3
  MaxCrashes = 2
  BUGGY_NO_WAL_FLUSH = FALSE
  BUGGY_TRUNCATE_FIRST = FALSE
  BUGGY_SKIP_CHECKSUM = FALSE
SPECIFICATION Spec
INVARIANT TypeOK
INVARIANT HistoryMatch
```

- [x] **Step 3: Add the model-check target to the Makefile**

Append after the `$(TLA_TOOLS)` rule:

```make
model-check: $(TLA_TOOLS)
	cd docs/models && java -XX:+UseParallelGC -cp $(abspath $(TLA_TOOLS)) \
	  tlc2.TLC $(TLC_FLAGS) -config WalCommit.cfg WalCommit.tla
```

- [x] **Step 4: Run it — expect green (no crashes yet, so trivially safe)**

```bash
make model-check
```

Expected: `Model checking completed. No error has been found.` (roughly a few thousand states). A violation here means a transcription error in the happy path — fix before committing.

- [x] **Step 5: Commit**

```bash
git add docs/models/WalCommit.tla docs/models/WalCommit.cfg Makefile
git commit -m "R9: sector-level WAL commit model, crash-free happy path"
```

---

### Task 4: the Crash action and NoTornVersionVisible

These lines define what "crash" and "torn" mean, so verify that Task 3 is
complete and the TODO markers are present before changing them.

**Files:**
- Modify: `docs/models/WalCommit.tla` (replace the two `TODO(Task 4)` markers; add `Crash` to `Next`)
- Modify: `docs/models/WalCommit.cfg` (add `INVARIANT NoTornVersionVisible`)

**Interfaces:**
- Consumes: `Merge`, `Put`, `EmptyCache`, `Content`, `Recovers(d, id, o)`, `MaxCrashes`, all variables from Task 3.
- Produces: action `Crash` (referenced in `Next` and by Task 6's mutant traces); invariant `NoTornVersionVisible` (referenced by `WalCommit.cfg` and Task 6's `BUGGY_SKIP_CHECKSUM` expectation).

**Guidance for Crash** (~8 lines): guard on `crashes < MaxCrashes` and `phase = "running"`. Then: there exists a subset `keep` of `DOMAIN pending` — those writes made it to the platter, the rest evaporate. Compute the post-crash disk with `Merge(disk, [s \in keep |-> pending[s]])`. The history subtlety: if the in-flight op had reached `st = "logged"` AND `Recovers(newDisk, op.id, op.obj)` holds, the crash *committed* the op (append `op.val` to `hstore[op.obj]`) even though no ack was ever sent; otherwise the op vanishes and `hstore` is unchanged. Either way: `pending' = EmptyCache`, `phase' = "down"`, `op' = IdleOp`, `crashes' = crashes + 1`, and zero the volatile state (`mtable'` all 0, `dirty' = {}`, `walNext' = 1`) so equivalent post-crash states collapse. `acked` and `nextOp` are unchanged — an acked op stays acked forever; that is the whole point.

**Guidance for NoTornVersionVisible** (~4 lines): when `phase = "running"`, every object with a table entry (`mtable[o] # 0`) must have an intact payload: `Content(<<"data", mtable[o]>>)` equals `<<mtable[o], v>>` for some `v \in Vals`. This is the "or torn" half of the blueprint invariant — a table entry pointing at garbage is a torn version made visible.

- [x] **Step 1: Write `Crash`, add it to `Next`, write `NoTornVersionVisible`, add the invariant line to `WalCommit.cfg`**

- [x] **Step 2: Run the checker**

```bash
make model-check
```

Expected: `Model checking completed. No error has been found.` over the full
crash-recovery state space. If TLC reports a violation, read the
counterexample trace bottom-up and determine whether the model or protocol is
wrong.

- [ ] **Step 3: Commit**

```bash
git add docs/models/WalCommit.tla docs/models/WalCommit.cfg
git commit -m "R9: crash semantics and torn-version invariant (subset-crash model)"
```

---

### Task 5: Refinement mapping and the durability invariant

**Files:**
- Modify: `docs/models/WalCommit.tla` (replace the `TODO(Task 5)` marker)
- Modify: `docs/models/WalCommit.cfg` (add `DurableRecoverable` and `PROPERTY AbsSpec`)

**Interfaces:**
- Consumes: module `WalCommitAbstract` (Task 2), `Replay`, `Last`, `hstore`, `acked`, `op`, `nextOp` (Tasks 3–4).
- Produces: `AbsSpec` (TLC PROPERTY), `DurableRecoverable` (invariant).

- [x] **Step 1: Replace the Task 5 TODO with the refinement and invariant**

```tla
(* ------------------------- refinement + durability ------------------------ *)

AbsInflight == IF op.st = "idle" THEN <<>> ELSE <<op.id, op.obj, op.val>>

Abs == INSTANCE WalCommitAbstract
       WITH store <- hstore, acked <- acked,
            inflight <- AbsInflight, nextOp <- nextOp

\* every sector-level behavior is a behavior of the atomic-commit machine
AbsSpec == Abs!Spec

(* If we lost all volatile state right now, recovery from the platter alone   *)
(* must reproduce the committed history. The third disjunct is the            *)
(* commit-point straddle: the in-flight record is already durable but the     *)
(* ack has not fired yet, so recovery would surface the op "early" - which    *)
(* the abstract spec permits via CrashKeep.                                   *)
DurableRecoverable ==
  phase = "running" =>
    LET t == Replay(disk)
    IN \A o \in Objects :
         \/ /\ hstore[o] = <<>>
            /\ t[o] = 0
         \/ /\ hstore[o] # <<>>
            /\ t[o] # 0
            /\ disk[<<"data", t[o]>>] = <<t[o], Last(hstore[o])>>
         \/ /\ op.st = "logged" /\ o = op.obj
            /\ t[o] = op.id
            /\ disk[<<"data", op.id>>] = <<op.id, op.val>>
```

- [x] **Step 2: Extend the config**

Add to `docs/models/WalCommit.cfg`:

```
INVARIANT DurableRecoverable
PROPERTY AbsSpec
```

- [x] **Step 3: Run the checker — expect green**

```bash
make model-check
```

Expected: `Model checking completed. No error has been found.` Property checking makes TLC slower; still minutes at these constants. A `Temporal properties were violated` error means the history bookkeeping (`hstore`/`acked`/`AbsInflight`) departs from the abstract machine somewhere — the trace shows the offending step.

- [ ] **Step 4: Commit**

```bash
git add docs/models/WalCommit.tla docs/models/WalCommit.cfg
git commit -m "R9: refinement of abstract commit machine + durability invariant"
```

---

### Task 6: Negative validation — three mutants TLC must break

**Files:**
- Create: `docs/models/WalCommitBugNoFlush.cfg`
- Create: `docs/models/WalCommitBugTruncateFirst.cfg`
- Create: `docs/models/WalCommitBugSkipChecksum.cfg`
- Modify: `Makefile` (add `model-check-negative`)

**Interfaces:**
- Consumes: the `BUGGY_*` branches already inside `WalCommit.tla` (Task 3) and the full invariant set (Tasks 4–5).

- [x] **Step 1: Write the three mutant configs**

Each is `WalCommit.cfg` with exactly one flag flipped. `docs/models/WalCommitBugNoFlush.cfg`:

```
CONSTANTS
  Objects = {o1, o2}
  Vals = {1, 2}
  MaxOps = 3
  MaxCrashes = 2
  BUGGY_NO_WAL_FLUSH = TRUE
  BUGGY_TRUNCATE_FIRST = FALSE
  BUGGY_SKIP_CHECKSUM = FALSE
SPECIFICATION Spec
INVARIANT TypeOK
INVARIANT HistoryMatch
INVARIANT NoTornVersionVisible
INVARIANT DurableRecoverable
PROPERTY AbsSpec
```

`WalCommitBugTruncateFirst.cfg`: same, but `BUGGY_NO_WAL_FLUSH = FALSE` and `BUGGY_TRUNCATE_FIRST = TRUE`.
`WalCommitBugSkipChecksum.cfg`: same, but only `BUGGY_SKIP_CHECKSUM = TRUE`.

- [x] **Step 2: Run each mutant by hand and read one trace**

```bash
cd docs/models && java -cp ../../third_party/tla2tools.jar tlc2.TLC \
  -workers auto -deadlock -cleanup -config WalCommitBugNoFlush.cfg WalCommit.tla
```

Expected: `Error: Invariant ... is violated` (nonzero exit) with a counterexample trace. Repeat for the other two configs. For at least one mutant, read the trace end-to-end and confirm it tells the expected story (e.g., NoFlush: ack fires, crash drops the record, recovery forgets an acked op). If any mutant comes back green, THE MODEL IS TOO WEAK — stop and strengthen invariants before proceeding (do not weaken the mutant).

- [x] **Step 3: Add the Makefile target**

```make
BUG_CFGS := WalCommitBugNoFlush WalCommitBugTruncateFirst WalCommitBugSkipChecksum

model-check-negative: $(TLA_TOOLS)
	@set -e; for cfg in $(BUG_CFGS); do \
	  echo "== $$cfg: expecting TLC to find a violation =="; \
	  out=$$(cd docs/models && java -cp $(abspath $(TLA_TOOLS)) tlc2.TLC \
	    $(TLC_FLAGS) -config $$cfg.cfg WalCommit.tla 2>&1); \
	  if echo "$$out" | grep -Eq "is violated|Temporal properties were violated"; then \
	    echo "   ok: violation found"; \
	  else \
	    echo "$$out" | tail -20; \
	    echo "MODEL TOO WEAK: $$cfg produced no violation"; exit 1; \
	  fi; \
	done
```

- [x] **Step 4: Run both targets — positive green, negative all-caught**

```bash
make model-check && make model-check-negative
```

Expected: positive run clean; negative run prints `ok: violation found` three times, exit 0.

- [ ] **Step 5: Commit**

```bash
git add docs/models/WalCommitBugNoFlush.cfg docs/models/WalCommitBugTruncateFirst.cfg \
        docs/models/WalCommitBugSkipChecksum.cfg Makefile
git commit -m "R9: negative validation - three protocol mutants TLC must break"
```

---

### Task 7: Documentation trueing-up

**Files:**
- Modify: `docs/design/2026-07-15-wal-tla-model.md` (mutant #3 rename)
- Modify: `docs/development/devlog.md` (new entry above the `<!-- Next entry goes here -->` marker)
- Modify: `README.md` (add the two make targets wherever existing targets are listed)

- [x] **Step 1: Amend the spec's mutant list**

In the design doc's "Negative validation" section, replace the `BUGGY_REPLAY_PAST_TEAR` bullet with:

```markdown
- `BUGGY_SKIP_CHECKSUM` — recovery trusts a record without validating its
  parts and payload, so it replays through a tear instead of stopping.
  (Renamed from `BUGGY_REPLAY_PAST_TEAR` during planning: with a linear,
  non-reused WAL and serialized commits, literally continuing past a torn
  record is benign — the dangerous form of the bug is trusting the torn
  record itself.)
```

Also in the spec's "Invariants" list, replace the `CheckpointNeverOrphansAckedOp` bullet with:

```markdown
- `DurableRecoverable` — recovery from durable state alone reproduces the
  committed history (subsumes checkpoint-orphan safety: a superblock
  advance that skips an un-homed record makes this fail at the next state)
```

- [x] **Step 2: Devlog entry**

Add above the `<!-- Next entry goes here -->` marker in `docs/development/devlog.md` (adjust the date and state to reality — especially if TLC ever went red and taught something; that belongs in the log):

```markdown
## 2026-07-15 — Phase 2 begins: WAL protocol modeled and checked

Per R9, modeled the WAL commit/recovery protocol in TLA+ before writing any
store code. Two specs in docs/models/: WalCommitAbstract (atomic durable
commits — the promise) and WalCommit (sector-level: pending-write cache,
subset crashes, torn records, checkpoint truncation as superblock advance).
TLC checks four invariants plus refinement of the abstract machine, and
`make model-check-negative` proves the model has teeth: three seeded
protocol bugs (ack without flush, truncate before homes durable, recovery
without checksums) each produce a counterexample. Design findings already
banked: checkpoint must not advance past an in-flight logged record, and
recovery must replay strictly in slot order because the superblock pointer
can lag the homes.
```

- [x] **Step 3: README target list**

Add `make model-check` / `make model-check-negative` lines next to where `make test` and `make fuzz-heap` are documented in README.md, phrased to match the surrounding style.

- [x] **Step 4: Final full verification**

```bash
make test && make model-check && make model-check-negative
```

Expected: all green (hosted tests still pass — proves the Makefile edits broke nothing).

- [ ] **Step 5: Commit**

```bash
git add docs/design/2026-07-15-wal-tla-model.md docs/development/devlog.md README.md
git commit -m "Docs: R9 WAL model devlog entry, spec mutant rename, README targets"
```
