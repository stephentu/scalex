#!/usr/bin/env python

import itertools
import platform
import subprocess
import sys

# config for tom
RUNTIME=30
THREADS = (1, 6, 12, 18, 24, 30, 36, 42, 48)
POLICIES = ('global_lock', 'per_node_lock', 'lock_free', 'lock_free_rcu')

GRIDS = [
  {'benchmarks' : ('readonly',),
   'policies' : POLICIES,
   'threads' : THREADS},
  {'benchmarks' : ('queue',),
   'policies' : POLICIES,
   'threads' : tuple(t for t in THREADS if t > 1)},
]

def run_configuration(bench, policy, nthreads):
  args = [
    './bench',
    '--bench', bench,
    '--policy', policy,
    '--num-threads', str(nthreads),
    '--runtime', str(RUNTIME)]
  p = subprocess.Popen(args, stdin=open('/dev/null', 'r'), stdout=subprocess.PIPE)
  r = p.stdout.read()
  p.wait()
  return float(r.strip())

if __name__ == '__main__':
  (_, outfile) = sys.argv
  results = []
  for grid in GRIDS:
    for (bench, policy, nthreads) in \
        itertools.product(grid['benchmarks'], grid['policies'], grid['threads']):
      config = { 'bench' : bench, 'policy' : policy, 'threads' : nthreads, }
      print >>sys.stderr, '[INFO] running config', config
      throughput = run_configuration(bench, policy, nthreads)
      results.append((config, throughput))
  with open(outfile, 'w') as f:
    print >>f, 'RESULTS = %s' % repr(results)
