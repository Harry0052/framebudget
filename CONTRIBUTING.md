# Contributing

FrameBudget is a small C++ command line tool. Changes should keep the staged demo runnable without a network call and without a paid service.

## Build and test

Install the packages in the README, then from a clean checkout:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/framebudget compare --dataset staged --budgets 1,2,5 --output out/demo
```

If `c++` is not g++, configure with `CXX=g++`.

Sanitizers:

```sh
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DFRAMEBUDGET_SANITIZERS=ON
cmake --build build-san -j
ctest --test-dir build-san --output-on-failure
```

## Fixtures

The MP4 clips under `fixtures/` are generated. Do not replace them with footage you do not have rights to. To regenerate the synthetic clips you need `ffmpeg` on `PATH`:

```sh
python3 tools/generate_fixtures.py
```

The generator checks that the decoded zone is fully red on labeled frames and fully not red elsewhere, and that an event onset is not weaker than its offset. Commit the generator output together with the script.

The color threshold in `fixtures/detector.json` is frozen from `session-tune`. Do not retune it on `session-eval`.

## Reports

Stdout is JSON. Human text goes to stderr. Keep those streams separate. New fields belong in `docs/schema.md` and in the tests.
