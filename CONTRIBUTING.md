# Contributing to Athena

Athena is under active development — contributions are welcome. If
something here catches your interest, grab an open
[issue](https://github.com/olympian-forge/athena/issues) (labels like
`good first issue` and `help wanted` are a good place to start, or
browse the open milestones for what's currently planned) and open a
PR. No need to ask permission first; if you're unsure an approach
fits, open the PR as a draft or start a discussion on the issue.

## Before contributing

1. Read [ai/AI_CONTEXT.md](ai/AI_CONTEXT.md) to understand the
   architecture — it covers implementation detail (the rules-core/AI-engine
   split, the ONNX bootstrap step, build-system quirks) that this file
   doesn't repeat.
2. Write tests first (TDD approach).
3. Ensure 100% coverage (`./scripts/build/generate_coverage.sh`).
4. Update documentation for any API changes.

## Branching model

- `dev` is the integration branch — every change lands here first via a
  feature branch + PR, never a direct push.
- `main` is the release branch — it only ever receives merges from
  `dev`, via PR, as a real merge commit (no rewriting).

## Code style

Athena is C++20, and the codebase leans toward **plain, C-style
implementations over idiomatic C++ cleverness**. Standard containers
(`std::string`, `std::vector`, `std::ifstream`, etc.) and normal error
handling (`try`/`catch`) are fine and expected — this is C++, not C, and
there's no need to drop to raw buffers or manual memory management. What
we're avoiding is syntax *sugar* that makes code harder to follow at a
glance: lambdas, STL algorithm-plus-lambda combinations where a plain
loop reads just as clearly (e.g. prefer a `for` loop over
`std::transform` with a lambda for something like lowercasing a
string), template metaprogramming, and other "because C++ allows it"
constructs that don't earn their complexity. If a modern C++ idiom
is clearly the simplest way to express something, use it — the bar is
readability, not avoiding the standard library.

Beyond that:

- snake_case for methods/functions, PascalCase for classes, matching
  the existing code.
- Python scripts under `tools/` use the full GPL boilerplate header
  (see existing files in that directory).
- Match whatever convention is already established in the file/module
  you're touching before introducing a new one.

## License

Athena is licensed under the GNU GPLv3 (or later) — see
[LICENSE](LICENSE). By contributing, you agree your contributions are
licensed under the same terms.
