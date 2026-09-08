# Quest-set initialization checks

Branch: `fix/quest-set-initialization`, based on `878b639dd2257924feb01252679634e7bdca4259`.

This change initializes a supported quest set when its first item is acquired and
the saved set value is zero. The value comes from the first `(value, itemIndex)`
entry, not the set block's ordering field and not a fixed `100`. Existing nonzero
values, including negative identifiers and `-1`, are preserved.

The item catalog carries the resolved account/character bank row and initial
value. Acquisition captures the old value, validates it again before commit,
and saves inventory plus quest state in one SQLite transaction. Family-4
preparation encodes the prospective state before commit, including an account
object when the account bank changes. Cache version 65 rebuilds old metadata,
including version-64 records that omitted the separate-root form below.

## Run

From this worktree in an **x64 Visual Studio Developer Command Prompt**, with
the toolset and Windows SDK required by `Sunrise.sln` installed:

```bat
tests\build_release.cmd
tests\run_quest_initialization.cmd
```

The test uses the actual Release object files, an in-memory SQLite database,
the repository's default schema, and a small synthetic item catalog. It does
not load the DLL into the game or open the installed save. Assertions remain
enabled in the test executable.
The scripts use that prompt's configured tools; no particular VS edition or
installation path is required. Rebuild after production changes before running
the checks, so the linked Release objects match the source.

Optional local content comparison (the extracted files must stay outside Git):

```bat
tests\run_quest_initialization.cmd "PATH\TO\LOCAL\content-02"
```

Checks cover signed identifiers; malformed blocks/arrays; duplicate membership,
initial identifiers and mappings; separate-parent links; unsupported scopes;
the bounded no-direct-flag form and its rejected neighbors; cache round trip;
both saved scopes; preservation of
existing state and objective progress; another character's isolation; stale
values/selection/contracts; full and invalid grants; rollback on either inventory
or unlock SQL failure; and the encoded account/character after-images.
Production queuez staging checks cover fresh account/character quests, existing
nonzero progress, an ordinary item, and the existing profile-change flag. A
source-level guard checks the actual world-reward caller's staging arguments and
rejects the old `profileChanged` argument. It is intentionally a wiring guard,
not an end-to-end test of compressed/encrypted world-reward delivery.

## Coverage and limits

The initial direct-flag-only build-86657 comparison recognized 182 structural
candidates: 50 account-scoped and 132 character-scoped first steps. The
separate-root extension recognizes 34 additional character-scoped first steps,
for 216 total (50 account + 166 character). It includes the confirmed contract
for item 13138: character row 162, initial value -1583618456; its five later
members remain excluded. The optional content check reports the current total.
This is **not** an
in-game pass count or proof of every vendor's eligibility policy. Technical
Knockout's gameplay regression has passed on the installed patch: the user
confirmed the vendor behavior, and the post-test save contains the acquired
item and initialized account value. Do not repeat that case without a relevant
regression or behavior change. The user also confirmed Sight, Shoot, Repeat's
quest-step acquisition behavior and persistence across a client restart after
the separate-root extension was installed. That gameplay report is accepted;
its post-test database was not independently inspected. These two completed
cases do not establish completion/turn-in or text-acknowledgement support.

The supported shape is an objective-bearing pursuit (bucket 40), linked to a
mode-1 set, with one unambiguous first-member identifier and one supported
value-bank mapping. It must have direct item-presence flags, or link to a
**separate, objective-free bucket-37 root with a character-scoped value**.
The latter form may have an absent/empty direct-flag list; malformed lists
still fail. This is a structural support boundary, not a claim to evaluate all
vendor eligibility rules. Unrecognized shapes keep the
existing grant behavior without a guessed quest-state write. No per-quest
exceptions, client hooks, schema migration or Lua changes are added.

Not implemented: later-step advancement, objective earning, completion rewards,
daily/weekly bounty rules, full vendor eligibility evaluation, or repair of
already inconsistent saves. Reopening a vendor does not initialize an already
owned item; a successful acquisition is required.

Vendor and world-item acquisition staging now both use `updates_account()`, so
a newly initialized account-scoped set requests an account update even without
a profile-material charge. Not every reward service uses the common commit
path: **season-pass quest initialization is excluded**. That service independently
writes account state without committing the quest value; no currently affected
season reward has been established. Do not claim universal reward-path support.
