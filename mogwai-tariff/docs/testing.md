Testing mogwai-tariff with afl
===

You can fuzz-test `mogwai-tariff`’s handling of input files using afl (American Fuzzy Lop):

```
CC=afl-gcc jhbuild make -ac
mogwai-tariff-0 build /tmp/fuzz/input/tariff1 "A more complex tariff"          2017-01-01T00:00:00Z 2018-01-01T00:00:00Z none 0 unlimited          2017-01-02T00:00:00Z 2017-01-02T05:00:00Z day 1 2000000
mogwai-tariff-0 build /tmp/fuzz/input/tariff0 "My first tariff"          2017-01-01T00:00:00Z 2018-01-01T00:00:00Z year 2 15000000
AFL_SKIP_CPUFREQ=1 afl-fuzz -m 100 -i /tmp/fuzz/input/ -o /tmp/fuzz/output/ -f /tmp/fuzz/in -- mogwai-tariff-0 dump /tmp/fuzz/in
```

Let this run for a day or two.
