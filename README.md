# Cost Governor

Cost Governor is an open-source, vendor-neutral C++20 runtime for governing execution cost
across accelerator time, energy, transfer, memory, retries, recovery, and per-request/per-token
budgets in heterogeneous AI infrastructure.

It answers the core systems question: *What will this execution actually cost under current
resource state and authority, which plan satisfies the budget and service constraints, and
when must execution be denied, deferred, replanned, or stopped because its economics are no
longer valid?*

The thesis is that cost is not a static price attached to a GPU. A workload may be cheap in
accelerator time but expensive in transfer; a retry may turn a profitable request into a loss;
a cheaper placement may violate topology, capacity, residency, or SLO constraints; and a plan
that was economical at admission may become uneconomical after failure, contention, throttling,
or resource-price changes. Cost Governor makes execution economics an explicit runtime
authority boundary.

## Systems boundary

Cost Governor **owns** execution-cost policy and budgets; typed monetary and non-monetary cost
units; cost evidence ingestion and freshness/provenance; projected cost; realized cost;
remaining budget; per-request, per-token, and per-operation cost; accelerator-time, energy,
transfer, storage/state, residency, retry/recovery, and wasted-attempt economics; candidate-plan
economics; feasibility against hard budgets; deterministic plan ranking; budget
reservation/commit/release; mid-flight budget revalidation; cost-overrun detection;
cost-governance intervention authorization; stale-evidence and stale-decision rejection;
post-action verification; durable cost-policy state; deterministic explanations; and
what-would-change analysis.

Cost Governor does **not** own raw utilization accounting, generic telemetry, generic resource
attribution, useful-vs-wasted-work attribution, generic SLO definition, tail-latency
governance, inference scheduling, admission mechanics, quota mechanics, resource arbitration,
capacity modeling, recovery mechanism execution, placement mechanics, or pricing-marketplace
logic. It emits typed cost-governance *intents* to adjacent runtimes rather than implementing
their mechanisms. Efficiency Ledger, SLO Fabric, Tail Governor, Recovery Planner, Resource
Broker, Capacity Fabric, Reservation Fabric, and Inference Scheduler remain separate layers.

## Cost model

Cost is represented with exact typed units; money is never an untyped double.

- **Money** — `MoneyMicros`: integer micro-units of a single configured currency. Addition and
  subtraction are exact; multiplication/division are checked and use an explicit rounding
  policy; overflow, NaN, and Inf are impossible by construction; serialization is
  deterministic. In v1.0.1 a single currency ("USD") is supported; currencies never mix
  silently.
- **Typed units** — `EnergyMicroJoules`, `AcceleratorNanoseconds`, `TransferBytes`,
  `MemoryByteNanoseconds`, `Bytes`, `Tokens`, `Requests`, `Operations`, and
  durations. Each has a distinct C++ type; amounts are never interchanged across semantic
  domains.
- **Strong identities** — `CoordinatorEpoch`, `WorkerId`, `WorkerBootId`,
  `WorkloadGeneration`, `RequestGeneration`, `AttemptGeneration`, `PlanId`,
  `PlanGeneration`, `CostPolicyId`/`CostPolicyGeneration`, `BudgetId`/`BudgetGeneration`,
  `PriceScheduleId`/`PriceScheduleGeneration`, `EvidenceId`/`EvidenceGeneration`,
  `ResourceId`/`ResourceGeneration`, `DeviceId`/`DeviceGeneration`,
  `PlacementId`/`PlacementGeneration`, `RecoveryGeneration`, `DispatchId`,
  `InterventionId`/`InterventionGeneration`, and `ReservationId`. Raw integers are never
  used interchangeably across authority domains.

## Price evidence

A `PriceObservation` records a price identity, resource/device scope, price kind
(ACCELERATOR_TIME, ENERGY, TRANSFER, MEMORY_HOLDING, STORAGE, RESIDENCY_HOLDING, REQUEST, TOKEN,
OPERATION, CUSTOM_TYPED_COMPONENT), unit amount, provenance (CONFIGURED_POLICY, MEASURED,
REPORTED, DERIVED, SYNTHETIC, UNKNOWN), freshness (observed timestamp and effective interval),
generation, and authority. A `PriceSchedule` is a generation-bound collection.

## Projected vs realized cost

Projected cost is derived from expected runtime/resource/price evidence; realized cost is
derived from completed measured work. The two models are kept separate and never silently
substituted. Component-level projection error is tracked; UNKNOWN components remain UNKNOWN and
never become zero.

## Budget lifecycle

`BudgetLifecycle`: CREATED, ACTIVE, RESERVED, PARTIALLY_CONSUMED, EXHAUSTED, EXPIRED, CANCELLED,
SUPERSEDED, CLOSED. Transitions are validated; terminal lifecycles remain terminal.

Reservation is distinct from consumption: `reserve`, `commit` (reserved -> consumed),
`release` (reserved -> released). Reservations are generation-bound, exact, idempotent,
releasable, and expiry-aware. Committing twice is a no-op; a stale reservation is rejected;
release never exceeds an active reservation; reserved amount is never negative; and hard
budgets never silently exceed their limit.

## Plan feasibility and ranking

`Feasibility`: FEASIBLE, INFEASIBLE, DEFER, REVALIDATION_REQUIRED, INSUFFICIENT_EVIDENCE,
UNKNOWN. Reasons include MISSING_PRICE, STALE_PRICE, HARD_BUDGET_EXCEEDED, SLO_CONFLICT,
TRANSFER_TOO_EXPENSIVE, ENERGY_TOO_EXPENSIVE, RECOVERY_TOO_EXPENSIVE, and UNKNOWN_COST_COMPONENT.
UNKNOWN never becomes FEASIBLE.

After hard feasibility, candidates are ranked deterministically with a lexicographic order over
projected total cost, cost/request, cost/token, energy, transfer, accelerator-time, confidence,
and identity/generation tie-break. Hard constraints dominate preference: a more expensive
plan that satisfies a hard SLO is selected over a cheaper one that violates it. The binding
constraint is exposed.

## Retry and recovery economics

Retry cost includes prior failed attempts. The request-level realized cost is the ordered sum
across attempts; the retry component summarizes the failed-attempt portion without
double-counting. Duplicate or stale completion never double-charges (attempt generation and
worker boot are fenced). Recovery plans (RESTORE, RESTART, MIGRATE, REHYDRATE, SHADOW_PROMOTE,
FAILOVER, RECOMPUTE) are evaluated for cost but never executed by Cost Governor.

## Authority and generation fencing

Decision and intervention authority is bound to a snapshot of generations: CoordinatorEpoch,
CostPolicyGeneration, PriceScheduleGeneration, EvidenceGeneration, PlanGeneration,
WorkloadGeneration, WorkerId + WorkerBootId (per-worker incarnation), ResourceGeneration,
PlacementGeneration, RecoveryGeneration, and BudgetGeneration. Stale actions are rejected
without mutating state. Worker boot authority is **per worker**: each worker (identified by
`WorkerId`) holds its own current `WorkerBootId`, so a worker restart fences only that worker's
prior price/evidence/attempt/dispatch/completion traffic and never invalidates another worker's
authority. Pre-dispatch revalidation re-checks authority, freshness, candidate existence,
feasibility, and hard constraints; a stale plan is never dispatched.

## Persistence and restart

Durable state (policies, budgets, completed accounting, historical realized cost, price
schedules, intervention history, stable identities) is written to a versioned, checksummed,
bounded snapshot with atomic save. Decoding rejects corruption, truncation, trailing garbage,
unknown/future versions, and out-of-bounds counts. The on-disk protocol is format-versioned
(`SnapshotCodec::kVersion`); the current format is v2 (a persisted `PriceObservation` carries its
`WorkerId`). A v1 (Cost Governor 1.0.0) snapshot, where a persisted `PriceObservation` did not
carry its `WorkerId`, is never silently reinterpreted as the current format: it is rejected
explicitly with `Status::PERSISTENCE_UNSUPPORTED_VERSION` (regenerate the state under the current
format). Live process authority (sockets, in-flight reservations) is intentionally not
persisted. On restart the CoordinatorEpoch advances, and recovered dynamic evidence requires
revalidation before any current cost decision resumes.

## Distributed proof

A real multiprocess proof (`distributed/`) uses framed, versioned, bounded, checksummed TCP
loopback between a coordinator process, Worker A, and Worker B. The transport handles partial
read/writes, concurrent writes, malformed frames, oversized frames, version mismatch, and
checksum failure. The proof drives the scenarios below and all pass: (A) two workers publishing
different device-scoped cost evidence and the governor selecting the legal cheapest; (A2) both
workers publish/record under their own boot without clobbering one another; (B) hard-budget
rejection with no feasible plan; (C) real worker B process kill/restart to a fresh boot where
B's old-boot price/evidence/attempt/completion traffic is fenced stale while A's authority
remains current and continues publishing/participating; (C2) real worker A process kill/restart
where A's old-boot traffic is fenced stale while B's current boot remains valid (no
cross-worker invalidation); (D) a boot-only incarnation change makes a prior dispatch stale
(dispatch fenced by the worker identity + boot pair) and a real coordinator process kill/restart
advances the epoch and rejects old-epoch traffic; (E) retry economics with failed-attempt cost
and duplicate-completion protection; (F) price-generation advance invalidating a previously
authorized plan at pre-dispatch revalidation.

## CUDA proof

A real CUDA proof (`cuda/`, enabled with `-DCG_BUILD_CUDA=ON`) performs actual `cudaMalloc`,
H2D, kernel execution, synchronization, and D2H on an NVIDIA device, measures real device
runtime with `cudaEvent` (never enqueue latency) and real transferred bytes, then computes
projected/realized cost from measured usage times configured POLICY rates, shows deterministic
plan selection, exercises a retry/re-execution path, and verifies device memory returns to
baseline. The configured rates are POLICY, never real market prices. This was run against an
NVIDIA GeForce RTX 5090 (compute capability 12.0) with CUDA 13.1.

## REAL / SYNTHETIC / POLICY / UNSUPPORTED labels

- **REAL** — measured CUDA duration, measured transfer bytes, real process kill/restart, real TCP.
- **POLICY** — configured `$/GPU-second`, `$/GB transferred`, configured energy price.
- **SYNTHETIC** — multi-node pricing scenarios, cross-region transfer price, unavailable
  hardware topology, estimated power consumption.
- **UNSUPPORTED** — real cloud billing integration, multi-GPU hardware pricing, NVLink/RDMA
  pricing, MIG pricing, provider-specific marketplace billing.

These are never blurred.

## Persistence integrity

Save/load round-trips preserve state; corruption, truncation, trailing garbage, and unknown
version all reject. Deterministic replay of historical decisions is supported; historical state
is never current authority.

## Concurrency and deadlock audit

The governor uses a single mutex for state; no nested locks and no external waits (I/O,
network, adapters) are performed while holding the state lock. Persistence copies a snapshot
under the lock and writes outside it. The design is audited for read-to-write upgrade, public
method reentry, callback reentry, event emission under locks, network wait under locks, adapter
wait under locks, file I/O under long-held locks, join under locks, and shutdown inversion.
Concurrency tests cover concurrent evidence ingestion, budget reserve/release, plan evaluation,
and persistence during mutation.

## Benchmarks

`bench/cg_bench` measures completed work honestly (no artificial timeouts / no forced
pass) for cost-evidence ingest, budget reserve/release, plan evaluation, persistence, and
concurrent evaluation at scales 100, 1k, 10k, 100k, and 1M where reasonable. The engineering
goal is correctness first, with a documented awareness of candidate rescans, full-history
scans, repeated recomputation, budget-map contention, persistence amplification, and string-heavy
hot paths.

## Build and install

Requirements: CMake 3.20+, a C++20 compiler (MSVC 2022 / `/W4 /WX`; GCC/Clang with
`-Wall -Wextra -Werror -Wpedantic`) and, optionally, CUDA 13.x + an NVIDIA device for the CUDA
proof. No CUDA is required for the core.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCG_BUILD_CUDA=OFF -DCG_BUILD_DISTRIBUTED=ON
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix <prefix>
```

Options: `CG_BUILD_TESTS`, `CG_BUILD_EXAMPLES`, `CG_BUILD_TOOLS`, `CG_BUILD_BENCH`,
`CG_BUILD_DISTRIBUTED`, `CG_BUILD_CUDA` (default ON for all except CUDA), and
`CG_BUILD_ASAN` (OFF). With `CMake 3.20+`, `Ninja`, and the MSVC `cl` driver, `-DCG_BUILD_ASAN=ON`
builds every target with real `/fsanitize=address` while preserving `/W4 /WX`; it adds `/Zi` so the
MSVC C5072 "ASAN enabled without debug information" warning is satisfied under `/WX` (the x64
`clang_rt.asan_dynamic-x86_64` runtime ships with the MSVC toolset).

## Downstream use

The installed package exports `CostGovernor::CostGovernor`:

```cmake
find_package(CostGovernor CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE CostGovernor::CostGovernor)
```

## Examples

Runnable examples are under `examples/`: basic budget, plan economics (two-plan comparison and
deterministic selection), hard-budget rejection, retry cost, stale-plan pre-dispatch
revalidation, and post-action intervention verification.

## CLI

`tools/cg` provides: `evaluate`, `compare-plans`, `show-budget`, `show-breakdown`,
`show-policy`, `show-what-would-change`, `inspect-history`, `validate-state`, and
`replay-decision`.

## Limitations

- Single configured currency in v1.0.2 (USD). Multiple currencies are not summed.
- The suite is validated in Release and Debug with `/W4 /WX` and zero first-party warnings, and
  in a genuine x64 AddressSanitizer build (`-DCG_BUILD_ASAN=ON`, `/fsanitize=address`, MSVC x64
  `clang_rt.asan_dynamic-x86_64` runtime). `/RTC` is not substituted for ASan.
- The on-disk snapshot protocol is format v2. A v1.0.0 (format v1) snapshot is rejected
  explicitly as unsupported (`Status::PERSISTENCE_UNSUPPORTED_VERSION`) rather than silently
  reinterpreted under the current layout; regenerate state under the current version.
- No real cloud-provider pricing is fabricated; all configured rates are labeled POLICY.
- Energy telemetry is not measured on the proof hardware; energy usage is estimated at a
  configured power label (SYNTHETIC) and priced with a POLICY rate.
- Recovery/placement/admission mechanisms are not executed by Cost Governor; only cost
  intents and evaluation are produced.
- Real multi-GPU, NVLink/RDMA, MIG, and provider-specific marketplace billing are UNSUPPORTED.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
