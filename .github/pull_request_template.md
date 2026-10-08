## Summary

What does this change and why?

## Linked issue

Closes #

## Checks run locally

- [ ] `python3 tools/hygiene/check_hygiene.py`
- [ ] `tools/ci/check-format.sh`
- [ ] `ctest --preset release`
- [ ] `tools/ci/run-tidy.sh build/clang`
- [ ] `ctest --preset asan`
- [ ] `ctest --preset tsan` (if engine, core, or DSP code changed)

## Measurements

For DSP or engine changes: before and after numbers from `tools/bench/`, with the command used.

## Manual testing

Anything that needs real hardware: which items of `docs/manual-test-checklist.md` you ran and on what devices.
