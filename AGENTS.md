# AGENTS.md

## Worker (C++)

The rules for the C++ code of the mediasoup worker (its sources, headers, tests, mocks and fuzzers) live in [`worker/AGENTS.md`](worker/AGENTS.md). Read it before touching any file under `worker/`. They apply only to that C++ code, not to the Node.js or Rust code of this repository.

## Where to store new instructions

- Project rules and knowledge go in the `AGENTS.md` files, so that every agent gets them: the root `AGENTS.md` for the whole repository, `worker/AGENTS.md` for the C++ code of the worker.
