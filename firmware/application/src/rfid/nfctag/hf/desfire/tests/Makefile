# The engine's suites live with the engine and run against the conformance
# corpus. This delegates to them so the firmware and the engine cannot drift
# apart, and so a suite is not maintained twice.
#
#   make test    the engine's full suite, including every reduced profile

.PHONY: test clean

test:
	@$(MAKE) -C ../core/tests test

clean:
	@$(MAKE) -C ../core/tests clean
