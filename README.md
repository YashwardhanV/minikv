# MiniKV

MiniKV is a small, Redis-inspired, in-memory key-value database written in modern C++17. You run it as an interactive terminal program (a REPL), type commands such as `SET name Alex` and `GET name`, and it answers in `redis-cli` style. Keys can expire after a time-to-live, any change can be undone, and the data is saved to a snapshot file on disk so it survives restarts. It has no dependencies beyond the standard library (GoogleTest is used for tests only) and is built to be easy to read: one class per file, every output string specified exactly, and full unit and end-to-end test coverage.

![demo](docs/demo.gif)

## Build and run

Requirements: CMake 3.16+, and GCC 9+ or Clang 10+. Linux or macOS. The first configure downloads GoogleTest v1.14.0, so it needs internet access once.

```bash
# Debug build (AddressSanitizer + UndefinedBehaviorSanitizer on by default)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run it
./build/minikv
```

Release build:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/minikv
```

Command-line flags:

```
minikv [--file <path>] [--no-save] [--help]
```

| Flag | Meaning |
|---|---|
| `--file <path>` | Snapshot file to load from and save to. Default: `minikv.db` in the current directory |
| `--no-save` | Pure in-memory mode: nothing is loaded or saved |
| `--help` | Print usage and exit with code 0 |

An unknown flag prints usage to stderr and exits with code 2.

CMake options:

| Option | Default | Effect |
|---|---|---|
| `MINIKV_SANITIZERS` | `ON` for Debug, else `OFF` | Adds `-fsanitize=address,undefined -fno-omit-frame-pointer` |
| `MINIKV_WERROR` | `OFF` | Treats warnings as errors (use in CI) |
| `MINIKV_BUILD_TESTS` | `ON` | Builds the unit and end-to-end tests |

When input is piped in (not a terminal), MiniKV prints no prompt and no welcome line, so the output is clean enough to diff:

```bash
printf 'SET a 1\nGET a\n' | ./build/minikv --no-save
```

## Commands

Command names are case-insensitive. Keys and values are case-sensitive. Wrap a value in double quotes to include spaces; inside quotes, `\"` is a literal quote and `\\` a literal backslash.

| Command | What it does | Example | Output |
|---|---|---|---|
| `SET key value` | Set a value. Removes any existing TTL. | `SET name Alex` | `OK` |
| `SET key value EX seconds` | Set a value that expires after `seconds` (positive integer). | `SET token abc EX 60` | `OK` |
| `GET key` | Get a value. | `GET name` | `"Alex"` or `(nil)` |
| `DEL key [key ...]` | Delete keys. Returns how many actually existed. | `DEL name nothere` | `(integer) 1` |
| `EXISTS key` | Does the key exist? | `EXISTS name` | `(integer) 1` or `(integer) 0` |
| `KEYS [pattern]` | Keys matching a glob (`*`, `?`), sorted. Default `*`. | `KEYS user:*` | `1) "user:1"` ... or `(empty list)` |
| `DBSIZE` | Number of keys. | `DBSIZE` | `(integer) 3` |
| `INCR key` | Add 1. A missing key counts as 0. Keeps the TTL. | `INCR visits` | `(integer) 1` |
| `DECR key` | Subtract 1. Same rules as `INCR`. | `DECR visits` | `(integer) 0` |
| `INCRBY key n` | Add `n` (may be negative). | `INCRBY score -5` | `(integer) -5` |
| `EXPIRE key seconds` | Set a TTL on an existing key. | `EXPIRE name 30` | `(integer) 1` (exists) or `(integer) 0` |
| `TTL key` | Seconds left, rounded up. `-1` = no TTL, `-2` = missing. | `TTL name` | `(integer) 30` |
| `PERSIST key` | Remove a TTL. | `PERSIST name` | `(integer) 1` if a TTL was removed, else `(integer) 0` |
| `UNDO` | Revert the most recent change. | `UNDO` | `OK` |
| `FLUSHALL` | Delete every key (undoable). | `FLUSHALL` | `OK` |
| `SAVE` | Write the snapshot to disk now. | `SAVE` | `OK` |
| `HELP` | Short help table. | `HELP` | help text |
| `EXIT` / `QUIT` | Save and quit. | `EXIT` | `Bye` |

Errors always look like `(error) ERR <message>`:

| Situation | Output |
|---|---|
| Unknown command | `(error) ERR unknown command 'FOO'` |
| Wrong argument count | `(error) ERR wrong number of arguments for 'set'` |
| Bad `SET` option | `(error) ERR syntax error` |
| Not an integer, or overflow, in `INCR`/`DECR`/`INCRBY` | `(error) ERR value is not an integer or out of range` |
| Seconds not a positive integer | `(error) ERR invalid expire time` |
| `UNDO` with nothing to undo | `(error) ERR nothing to undo` |
| `SAVE` under `--no-save` | `(error) ERR persistence disabled` |
| Unclosed quote | `(error) ERR unterminated quote` |
| Disk write failed | `(error) ERR failed to save: <reason>` |

Integers are parsed strictly: `12` and `-7` are accepted, while `+3`, ` 4`, `4a`, an empty string and anything outside the signed 64-bit range are rejected.

## Example session

```
$ ./build/minikv
MiniKV 1.0 — type HELP for commands, EXIT to quit
minikv> SET name Alex
OK
minikv> SET greeting "hello world"
OK
minikv> INCR visits
(integer) 1
minikv> INCR visits
(integer) 2
minikv> KEYS *
1) "greeting"
2) "name"
3) "visits"
minikv> SET session xyz EX 10
OK
minikv> TTL session
(integer) 10
minikv> DEL name
(integer) 1
minikv> UNDO
OK
minikv> GET name
"Alex"
minikv> INCR name
(error) ERR value is not an integer or out of range
minikv> EXIT
Bye
$ ./build/minikv
MiniKV 1.0 — type HELP for commands, EXIT to quit
minikv> GET greeting
"hello world"
```

## Design decisions

**Why `std::unordered_map`.** Every command looks up keys by exact name, and a hash map gives average O(1) lookup, insert and delete. The only operation that wants order is `KEYS`, which sorts its (usually small) result at the end. A `std::map` would keep keys sorted all the time but would make every single lookup O(log n) to speed up one rare command.

**Lazy expiry plus a sweep, and how Redis differs.** Expiry is enforced two ways. Lazily: every read or write of a key first checks its expiry and deletes it if the time has passed, so an expired key can never be returned. Actively: before each command, `sweep_expired()` walks the whole map and removes every expired key, so `DBSIZE` and `KEYS` are always exact and expired keys never pile up. The sweep is O(n) per command, which is fine for a learning project but would not scale. Real Redis instead samples a small random set of keys that have a TTL about ten times a second and repeats only if many of the sample were expired. That bounds the work per cycle at the cost of letting some expired keys linger in memory a little longer (they are still invisible thanks to lazy expiry).

**Why the Clock is injected.** `Store` never asks the operating system for the time; it asks a `Clock` passed into its constructor. The real program uses `SystemClock` (the only code that touches `std::chrono`), while tests use `FakeClock`, which only moves when the test calls `advance()`. That makes TTL tests exact and instant: a test can check that a key is alive 1 ms before its deadline and gone exactly at it, without sleeping and without flakiness.

**Atomic save via rename.** A snapshot is never written over the real file directly. It is written in full to `<path>.tmp`, flushed, closed and checked for errors, and only then renamed onto `<path>`. On POSIX systems rename replaces the destination atomically, so the file on disk is always either the complete old snapshot or the complete new one. A crash in the middle of a save leaves at worst a stray `.tmp` file. If a save fails, the data stays in memory and the command reports `ERR failed to save`. `EXIT` refuses to quit when its save fails, so a full disk cannot silently lose your data.

**Length-prefixed file format.** Each record starts with a header line `<key_len> <value_len> <expires_at_ms or -1>` followed by exactly that many raw bytes. Because the reader counts bytes instead of looking for separators, keys and values can contain spaces, quotes, newlines or any other byte with no escaping rules to get wrong. It also makes corruption easy to detect: a wrong header, a non-numeric length or a file that ends early is reported, the bad file is moved to `<path>.corrupt` (never overwritten) and MiniKV starts empty. Records are written in sorted key order so files are deterministic and diff-able.

**Undo stores previous state, not inverse commands.** Before a mutating command runs, the processor records the exact previous `Entry` (value and expiry) of every key it will touch, or "did not exist". `UNDO` simply puts those entries back. The alternative, computing an inverse command (the inverse of `INCR` is `DECR`, the inverse of `DEL` is a `SET` with the old value and TTL...), needs custom and error-prone logic per command, and it breaks down for things like `FLUSHALL` or a `SET` that wiped a TTL. Storing previous state makes every command undoable with one generic mechanism. After running, the processor compares the keys with their recorded state and drops the record if nothing changed, so `DEL` on a missing key or a failed `INCR` never takes up an undo slot. History holds the last 100 changes, is not saved to disk, and an entry restored with an expiry that has already passed is simply removed by the next sweep.

## Running tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

That runs the GoogleTest unit tests (tokenizer, glob matching, store, TTL, undo, every command's output and error strings, persistence including corrupt files) plus `e2e_session1`, which pipes `tests/e2e/session1.txt` into the real binary and diffs the output against `tests/e2e/session1.expected`. You can also run it by hand:

```bash
bash tests/e2e/run_e2e.sh ./build/minikv tests/e2e
```

In Debug builds the tests run under AddressSanitizer and UndefinedBehaviorSanitizer, and any sanitizer report fails the test. To check that warnings stay at zero, configure with `-DMINIKV_WERROR=ON`.

Formatting follows `.clang-format` (LLVM based, 4-space indent, 100 columns):

```bash
clang-format -i include/minikv/*.hpp src/*.cpp tests/*.cpp
```

## Possible improvements

1. **Append-only file (AOF):** log each mutating command instead of rewriting the full snapshot, plus a `REWRITE` command to compact the log. Today every change rewrites the whole file, which is O(total data) per command.
2. **Lists:** `LPUSH`, `RPUSH`, `LPOP`, `LRANGE`. This needs a type tag on `Entry` and a `WRONGTYPE` error.
3. **TCP server mode:** listen on a port so several terminals (or `nc`) can connect at once.
4. **LRU eviction:** a `--max-keys N` flag that evicts the least recently used key when the store is full.
5. **Benchmark tool:** time one million `SET`/`GET` operations and report operations per second.
6. **GitHub Actions CI:** build and test on Ubuntu with both GCC and Clang, with `MINIKV_WERROR=ON`.

Smaller ideas: call `fsync` before the rename for crash durability across power loss (today the rename is atomic, but the OS may still hold the data in its cache), and replace the O(n) sweep with Redis-style sampling.

## License

MIT, see [LICENSE](LICENSE).
