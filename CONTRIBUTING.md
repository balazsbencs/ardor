# Contributing to Ardor

Thank you for helping improve Ardor. Bug reports, documentation, tests, hardware
validation, and focused code changes are welcome.

## Before you start

- Search existing issues and pull requests before opening a new one.
- Discuss large changes before investing significant implementation time.
- Report vulnerabilities privately according to [SECURITY.md](SECURITY.md).
- Do not commit credentials, local `.env` files, Wi-Fi configuration, licensed
  NAM models, impulse responses, or captured audio without redistribution rights.

## Development workflow

1. Fork the repository and create a narrowly scoped branch.
2. Follow the setup and build instructions in [README.md](README.md) and
   [BUILD.md](BUILD.md).
3. Add or update tests for behavior changes.
4. Run the relevant checks locally:

   ```sh
   cmake -S . -B build -DARDOR_UI_BACKEND=none -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j
   ctest --test-dir build --output-on-failure

   (cd services/managerd && go test ./...)
   (cd apps/manager && npm ci && npm run typecheck && npm test)
   ```

5. Use clear commits. Conventional Commit subjects such as `fix:`, `feat:`,
   `docs:`, and `test:` are preferred because releases derive their changelog
   from commit messages.
6. Open a pull request that explains the motivation, security impact, tests,
   and any hardware validation performed.

## Code and review expectations

Keep changes focused, preserve realtime-audio constraints, validate all external
paths and inputs, and fail safely when hardware or assets are unavailable. New
dependencies should be necessary, actively maintained, and pinned through the
appropriate lock file.

All changes require maintainer review. By contributing, you confirm that you
have the right to submit the work and any included assets.

## Contributor recognition

Human contributors are listed in [CONTRIBUTORS.md](CONTRIBUTORS.md). Add your
name or handle in the same pull request if you would like to be recognized.
