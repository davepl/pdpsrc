"""Host regressions for the segmented PDP-11 sieve."""
import math
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

BSD = Path(__file__).resolve().parents[2]


class VMSieveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        scratch = tempfile.TemporaryDirectory()
        cls.addClassCleanup(scratch.cleanup)
        cls.exe = Path(scratch.name) / "vmsieve"
        subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c89", "-O2", "-Wall", "-Wextra", "-Werror", "-DVMSIEVE",
            str(BSD / "sieve/sieve.c"), str(BSD / "vmsieve/vmsieve.c"),
            "-o", str(cls.exe)
        ], check=True)

    def run_sieve(self, *args):
        return subprocess.check_output([str(self.exe), *map(str, args)], text=True)

    def test_counts_and_validation(self):
        for limit, count in [(500000, 41538), (1000000, 78498), (5000000, 348513),
                             (10000000, 664579), (25000000, 1565927), (50000000, 3001134)]:
            with self.subTest(limit=limit):
                out = self.run_sieve("-1", "-l", limit)
                self.assertRegex(out, rf"Count of primes found\s*:\s*{count}\b")
                self.assertRegex(out, r"Prime validator\s*:\s*PASS")
        self.assertRegex(self.run_sieve("-1", "-l", 66049), r"Prime validator\s*:\s*UNKNOWN")
        self.assertNotEqual(subprocess.run([self.exe, "-1", "-l", "50000001"],
                                           capture_output=True).returncode, 0)

    def test_print_across_segments_and_new_prime_squares(self):
        # Independent dense reference indexed by the actual integer.
        limit = 131101
        primes = bytearray([1]) * (limit + 1)
        primes[:2] = bytes(2)
        for p in range(2, math.isqrt(limit) + 1):
            if primes[p]:
                primes[p*p::p] = bytes((limit - p*p)//p + 1)
        for n in (2, 3, 65535, 65536, 65537, 66048, 66049, 66050, limit):
            with self.subTest(limit=n):
                values = map(int, self.run_sieve("-1", "-q", "-p", "-l", n).split())
                self.assertEqual(list(values), [i for i in range(n + 1) if primes[i]])

    def test_repeated_passes_reset_segment_state(self):
        out = self.run_sieve("-s", 1, "-l", 1000000)
        self.assertGreater(int(re.search(r"Number of passes\s*:\s*(\d+)", out)[1]), 1)
        self.assertRegex(out, r"Total time taken\s*:\s*[1-9]\d*\.\d{2} seconds")
        self.assertRegex(out, r"Time per pass\s*:\s*\d+\.\d{3} seconds")
        self.assertRegex(out, r"Prime validator\s*:\s*PASS")


if __name__ == "__main__":
    unittest.main()
