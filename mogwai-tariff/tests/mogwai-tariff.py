#!/usr/bin/python3
# -*- coding: utf-8 -*-
#
# Copyright © 2018 Endless Mobile, Inc.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
# MA  02110-1301  USA

"""Integration tests for the mogwai-tariff utility."""

import os
import shutil
import subprocess
import tempfile
import unittest

import taptestrunner


class TestMogwaiTariff(unittest.TestCase):
    """Integration test for running mogwai-tariff.

    This can be run when installed or uninstalled. When uninstalled, it
    requires G_TEST_BUILDDIR and G_TEST_SRCDIR to be set.

    The idea with this test harness is to test the mogwai-tariff utility, its
    handling of command line arguments and its exit statuses; rather than to
    test any of the core tariff code in depth. Unit tests exist for that.
    """

    def setUp(self):
        self.timeout_seconds = 10  # seconds per test
        self.tmpdir = tempfile.mkdtemp()
        os.chdir(self.tmpdir)
        print('tmpdir: ' + self.tmpdir)
        if 'G_TEST_BUILDDIR' in os.environ:
            self.__mogwai_tariff = \
                os.path.join(os.environ['G_TEST_BUILDDIR'], '..',
                             'mogwai-tariff-0')
        else:
            self.__mogwai_tariff = os.path.join('/', 'usr', 'bin',
                                                'mogwai-tariff-0')
        print('mogwai_tariff: ' + self.__mogwai_tariff)

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def runMogwaiTariff(self, args):
        argv = [self.__mogwai_tariff] + args
        print('Running: ' + ' '.join(argv))
        info = subprocess.run(argv, timeout=self.timeout_seconds,
                              stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT)
        print('Output: ' + info.stdout.decode('utf-8'))
        return info

    def test_single_period(self):
        """Test creating, dumping and looking up in a single period tariff."""
        info = self.runMogwaiTariff(['build', 'tariff0', 'tariff0',
                                     '2017-01-01T00:00:00Z',
                                     '2018-01-01T00:00:00Z',
                                     'year', '2', '15000000'])
        info.check_returncode()
        self.assertTrue(os.path.exists('tariff0'))

        # Dump the tariff.
        info = self.runMogwaiTariff(['dump', 'tariff0'])
        info.check_returncode()
        out = info.stdout.decode('utf-8').strip()
        self.assertIn(
            'Tariff ‘tariff0’\n'
            '----------------\n'
            '\n'
            'Period 2017-01-01T00:00:00+00 – 2018-01-01T00:00:00+00:\n'
            ' • Repeats every 2 years\n'
            ' • Capacity limit: 15.0 MB (15000000 bytes)', out)

        # Lookup a time in the tariff.
        info = self.runMogwaiTariff(['lookup', 'tariff0',
                                     '2017-01-01T01:00:00Z'])
        info.check_returncode()
        out = info.stdout.decode('utf-8').strip()
        self.assertIn(
            'Period 2017-01-01T00:00:00+00 – 2018-01-01T00:00:00+00:\n'
            ' • Repeats every 2 years\n'
            ' • Capacity limit: 15.0 MB (15000000 bytes)', out)

        # This lookup should fail because the time is outside a period in the
        # tariff.
        info = self.runMogwaiTariff(['lookup', 'tariff0',
                                     '2000-01-05T00:00:05Z'])
        out = info.stdout.decode('utf-8').strip()
        self.assertIn('No period matches the given date/time.', out)
        self.assertEqual(info.returncode, 2)  # EXIT_LOOKUP_FAILED

        os.remove('tariff0')

    def test_two_periods(self):
        """Test building and dumping a tariff with two periods."""
        info = self.runMogwaiTariff(['build', 'tariff1',
                                     'tariff1',
                                     '2017-01-01T00:00:00Z',
                                     '2018-01-01T00:00:00Z',
                                     'none', '0', 'unlimited',
                                     '2017-01-02T00:00:00Z',
                                     '2017-01-02T05:00:00Z',
                                     'day', '1', '2000000'])
        info.check_returncode()
        self.assertTrue(os.path.exists('tariff1'))

        info = self.runMogwaiTariff(['dump', 'tariff1'])
        info.check_returncode()
        out = info.stdout.decode('utf-8').strip()
        self.assertIn(
            'Tariff ‘tariff1’\n'
            '----------------\n'
            '\n'
            'Period 2017-01-01T00:00:00+00 – 2018-01-01T00:00:00+00:\n'
            ' • Never repeats\n'
            ' • Capacity limit: unlimited\n'
            'Period 2017-01-02T00:00:00+00 – 2017-01-02T05:00:00+00:\n'
            ' • Repeats every 1 day\n'
            ' • Capacity limit: 2.0 MB (2000000 bytes)', out)

        os.remove('tariff1')

    def test_timezones(self):
        """Test building and dumping a tariff with non-UTC timezones."""
        info = self.runMogwaiTariff(['build', 'tz', 'tz',
                                     '2017-01-01T00:00:00-01:00',
                                     '2018-01-01T00:00:00+00:30',
                                     'none', '0', 'unlimited'])
        info.check_returncode()
        self.assertTrue(os.path.exists('tz'))

        info = self.runMogwaiTariff(['dump', 'tz'])
        info.check_returncode()
        out = info.stdout.decode('utf-8').strip()
        self.assertIn(
            'Tariff ‘tz’\n'
            '-----------\n'
            '\n'
            'Period 2017-01-01T00:00:00-01 – 2018-01-01T00:00:00+00:30:\n'
            ' • Never repeats\n'
            ' • Capacity limit: unlimited', out)

        os.remove('tz')


if __name__ == '__main__':
    unittest.main(testRunner=taptestrunner.TAPTestRunner())
