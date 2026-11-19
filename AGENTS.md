# drone_tracker repository instructions

Before development or builds, read [AGENTS.local.md](AGENTS.local.md) if present and check parent directories for additional `AGENTS.local.md` files.

## Branches and OpenSpec

- Develop on `rk3576` unless the task selects another branch. Investigate behavior differences with `git show jetson:<path>` or a branch diff.
- For an RK3576 feature, architecture, interface, or observable behavior change, inspect [active OpenSpec changes](openspec/changes/) before editing code. Continue a matching change or complete a new proposal, design, specifications, and `tasks.md` before implementation. Require an approved change to alter external behavior or tracking algorithm semantics. Verify the implementation against all planning artifacts before synchronizing specifications or archiving the change.

## Change review

- Read the [RK3576 architecture guide](docs/architecture/RK3576_REFACTORING_PLAN.md) before changing platform boundaries. Check that each build discovers only its own hardware SDKs.
- For changes to asynchronous detection, queues, frame read views, or device buffers, document which thread or device owns and uses each resource. Run the relevant concurrency, stress, or replay checks and review cleanup after partial initialization.
- Update all affected [parameter templates](parameters/README.md) and [launch overrides](launch/) when a default or unit changes.
- Treat bundled dependencies such as `readerwriterqueue.h` and `threadpool.h` as third-party code. Keep edits to them within explicit task scope. Require an explicit request before adding large model binaries to Git.

## Validation evidence

- Report the source revision, actual platform, relevant model and input identities, commands, and exit statuses. Name any checks skipped because hardware, SDKs, models, or recordings were unavailable.
- For production board validation, record the board identity, OS, kernel, toolchain, memory, storage, and device topology. Recheck deployment state and relevant NPU and display device permissions on the selected board. Follow the [deployment guide](deployment/README.md).
- Resolve the board's system `librknnrt.so` path. Compare its version and SHA-256 checksum with the baseline pinned in the [RK3576 SDK image recipe](dockerfiles/Dockerfile.rk3576-humble-amd64) and record the results. Check the shared library version independently of the `rknn_server` service.
