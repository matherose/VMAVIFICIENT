# Phase 0: a single smoke test exercising the build+test wiring.
# Real Unity-based test suite arrives in Phase 1 once `vmav_result` exists.

vmav_add_unit_test(NAME test_smoke SOURCES test_smoke.c)
