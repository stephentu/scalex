#!/usr/bin/env python

import matplotlib
import pylab as plt
import sys
import math

BENCHMARKS=('readonly', 'queue')
POLICIES = ('global_lock', 'per_node_lock', 'lock_free', 'lock_free_rcu')

if __name__ == '__main__':
  (_, rfile, outprefix) = sys.argv
  execfile(rfile)
  for bench in BENCHMARKS:
    fig = plt.figure()
    ax = plt.subplot(111)
    #logscale = bench == 'readonly'
    logscale = False
    trfm = (lambda x: math.log(x, 10)) if logscale else (lambda x: x)
    for policy in POLICIES:
      configs = [(x, y) for (x, y) in RESULTS if x['bench'] == bench and x['policy'] == policy]
      configs = sorted([(x['threads'], y) for (x, y) in configs], key=lambda x: x[0])
      ax.plot([x[0] for x in configs], [trfm(x[1]/float(x[0])) for x in configs])

    ax.set_xlabel('num cores')
    ax.set_ylabel('throughput (ops/sec/core)')
    ax.set_title(bench)

    # hacky: see http://stackoverflow.com/questions/4700614/how-to-put-the-legend-out-of-the-plot
    box = ax.get_position()
    ax.set_position([box.x0, box.y0 + box.height * 0.1, box.width, box.height * 0.9])
    # shorten the names so they fit
    ax.legend(('g-lock', 'pn-lock', 'lock-f', 'lock-f-rcu',),
        loc='upper center', bbox_to_anchor=(0.5, -0.10),
        fancybox=True, shadow=True, ncol=len(POLICIES))

    fig.savefig(outprefix + '-' + bench + '.pdf')
