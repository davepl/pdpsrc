#!/usr/bin/env python3
"""Host regression checks: python3 bsd/sieve/tests/test_sieve.py.

Compile the actual source into a temporary directory.  Expected primes come
from trial division, independently of the program's bit-packed sieve.
Native PDP-11 opcode/undefined-symbol checks belong to the target build.
"""

import bisect
import math
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import time
import unittest


SOURCE = Path(__file__).resolve().parents[1] / "sieve.c"


def trial_division_primes(limit):
    primes = []
    for candidate in range(2, limit + 1):
        root = math.isqrt(candidate)
        for divisor in primes:
            if divisor > root:
                primes.append(candidate)
                break
            if candidate % divisor == 0:
                break
        else:
            primes.append(candidate)
    return primes


class SieveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.TemporaryDirectory(prefix="sieve-check-")
        cls.addClassCleanup(cls.scratch.cleanup)
        scratch = Path(cls.scratch.name)
        cls.executable = scratch / "sieve"
        cls.failed_allocation_executable = scratch / "sieve-no-memory"
        compiler = shlex.split(os.environ.get("CC", "cc"))
        flags = ["-std=c89", "-O2", "-Wall", "-Wextra", "-pedantic"]
        subprocess.run(compiler + flags + [str(SOURCE), "-o", str(cls.executable)],
                       check=True)

        # Keep the real main/argument/timing paths while replacing only the
        # allocator, so an allocation failure cannot be reported as a pass.
        wrapper = scratch / "sieve-no-memory.c"
        wrapper.write_text(
            '#include <stdlib.h>\n'
            'static void *fail_calloc(size_t count, size_t size)\n'
            '{ (void)count; (void)size; return NULL; }\n'
            '#define calloc fail_calloc\n'
            '#include "sieve.c"\n'
        )
        subprocess.run(compiler + flags + ["-I", str(SOURCE.parent), str(wrapper),
                                          "-o", str(cls.failed_allocation_executable)],
                       check=True)
        cls.reference = trial_division_primes(500000)
        if len(cls.reference) != 41538:
            raise AssertionError("Independent reference must find 41538 primes through 500000")

    def run_sieve(self, *arguments, executable=None, success=True, timeout=5):
        result = subprocess.run(
            [str(executable or self.executable), *map(str, arguments)],
            text=True, capture_output=True, timeout=timeout,
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stderr, "")
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def count_from_output(self, output):
        match = re.search(r"^Count of primes found\s*:\s*(\d+)\s*$", output, re.MULTILINE)
        self.assertIsNotNone(match, output)
        return int(match.group(1))

    def timing_from_output(self, output, label):
        # A decimal number printed from integer components, with no exponent,
        # nan or inf.  At least two fractional places must be retained.
        match = re.search(r"^" + re.escape(label) + r"\s*:\s*(\d+)\.(\d{2,6}) seconds\s*$",
                          output, re.MULTILINE)
        self.assertIsNotNone(match, output)
        return int(match.group(1)) + int(match.group(2)) / (10 ** len(match.group(2)))

    def test_counts_at_small_prime_squares_and_word_boundaries(self):
        limits = (
            2, 3, 4, 5, 7, 8, 9, 10, 11, 15, 16, 17, 31, 32, 33,
            24, 25, 26, 48, 49, 50, 120, 121, 122, 168, 169, 170,
            32767, 32768, 65535, 65536, 131071, 131072, 499999, 500000,
        )
        for limit in limits:
            with self.subTest(limit=limit):
                result = self.run_sieve("-1", "-l", limit)
                expected = bisect.bisect_right(self.reference, limit)
                self.assertEqual(self.count_from_output(result.stdout), expected)

    def test_dictionary_limits_validate(self):
        for limit, expected in ((10, 4), (100, 25), (1000, 168),
                                (10000, 1229), (100000, 9592), (500000, 41538)):
            with self.subTest(limit=limit):
                output = self.run_sieve("-1", "-l", limit).stdout
                self.assertEqual(self.count_from_output(output), expected)
                self.assertRegex(output, r"Prime validator\s*:\s*PASS\b")

    def test_undocumented_validation_limit_is_unknown(self):
        output = self.run_sieve("-1", "-l", "49").stdout
        self.assertRegex(output, r"Prime validator\s*:\s*UNKNOWN\b")
        self.assertNotRegex(output, r"Prime validator\s*:\s*PASS\b")

    def test_printed_primes_exactly(self):
        for limit in (2, 3, 9, 50, 121):
            with self.subTest(limit=limit):
                output = self.run_sieve("-1", "-q", "-p", "-l", limit).stdout
                self.assertRegex(output, r"^\s*\d+(?:\s+\d+)*\s*$")
                self.assertEqual(list(map(int, output.split())),
                                 self.reference[:bisect.bisect_right(self.reference, limit)])

    def test_quiet_oneshot_has_no_output(self):
        self.assertEqual(self.run_sieve("-1", "-q").stdout, "")

    def test_invalid_limits_are_rejected(self):
        for value in ("0", "1", "500001", "1000000", "-2", "", "abc", "2x",
                      "2.5", " 10", "+10", "999999999999999999999999"):
            with self.subTest(limit=value):
                self.run_sieve("-1", "-l", value, success=False)

    def test_invalid_seconds_are_rejected(self):
        for value in ("0", "-1", "32768", "", "abc", "1x", "1.5", " 1", "+1",
                      "999999999999999999999999"):
            with self.subTest(seconds=value):
                self.run_sieve("-1", "-s", value, success=False)

    def test_seconds_upper_bound_can_be_parsed_in_oneshot_mode(self):
        self.run_sieve("-1", "-q", "-s", "32767")

    def test_missing_and_unexpected_arguments_are_rejected(self):
        for arguments in (("-l",), ("-s",), ("-x",), ("-1", "unexpected")):
            with self.subTest(arguments=arguments):
                self.run_sieve(*arguments, success=False)

    def test_help(self):
        for help_option in ("-h", "-?"):
            with self.subTest(option=help_option):
                output = self.run_sieve(help_option).stdout
                self.assertIn("Usage:", output)
                for option in ("-l", "-s", "-1", "-p", "-q"):
                    self.assertIn(option, output)

    def test_allocation_failure_is_fatal(self):
        for arguments in (("-1",), ("-1", "-q")):
            with self.subTest(arguments=arguments):
                result = self.run_sieve(*arguments,
                                       executable=self.failed_allocation_executable,
                                       success=False)
                self.assertIn("allocation", (result.stdout + result.stderr).lower())
                self.assertNotIn("Prime validator", result.stdout)

    def test_timed_run_uses_elapsed_wall_time(self):
        start = time.monotonic()
        output = self.run_sieve("-l", "10000", "-s", "1", timeout=8).stdout
        wall_time = time.monotonic() - start
        self.assertGreaterEqual(wall_time, 0.9, output)
        self.assertLess(wall_time, 6, output)
        elapsed = self.timing_from_output(output, "Total time taken")
        per_pass = self.timing_from_output(output, "Time per pass")
        self.assertGreaterEqual(elapsed, 0.9, output)
        self.assertLessEqual(elapsed, wall_time + 0.1, output)
        self.assertGreaterEqual(per_pass, 0)
        passes = re.search(r"^Number of passes\s*:\s*(\d+)\s*$", output, re.MULTILINE)
        self.assertIsNotNone(passes, output)
        self.assertGreater(int(passes.group(1)), 1)
        self.assertEqual(self.count_from_output(output), 1229)
        self.assertRegex(output, r"Prime validator\s*:\s*PASS\b")


if __name__ == "__main__":
    unittest.main(verbosity=2)
