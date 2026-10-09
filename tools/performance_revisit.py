"""Classify native cache revisits without treating mixed work as memory-only."""


def summarize_revisits(events):
    markers = [e for e in events if e['event'] == 'scenario.memory_revisit']
    result = []
    for index, marker in enumerate(markers):
        if not marker['shown']:
            continue
        end = markers[index + 1]['sinceStartMs'] if index + 1 < len(markers) else float('inf')
        interval = [e for e in events if marker['sinceStartMs'] <= e['sinceStartMs'] < end]
        demands = [e for e in interval if e['event'] == 'viewport.demand']
        keys = {(e['owner'], e['generation']) for e in demands}
        images = [e for e in interval if e['event'] == 'viewport.presented'
                  and (e['owner'], e['generation']) in keys]
        completed = [e for e in interval if e['event'] == 'loader.completed']
        sources = sum(bool(e.get('sourceDecode')) for e in completed)
        disks = sum(bool(e.get('diskHit')) for e in completed)
        dispatched = sum(e['event'] == 'loader.dispatched' for e in interval)
        panes = len({e['owner'] for e in images})
        stable = len(demands) == marker['panes'] and len(keys) == marker['panes']
        result.append({
            'iteration': marker['iteration'], 'expectedPanes': marker['panes'],
            'presentedPanes': panes, 'sourceDecodeCalls': sources, 'diskHits': disks,
            'memoryHits': sum(e['event'] == 'loader.memory_hit' for e in interval),
            'workerDispatches': dispatched, 'demands': len(demands), 'stableViewport': stable,
            'maxAnimationCycles': max([e.get('animationCycles', -1) for e in images], default=None),
            'maxFillMs': max([e['elapsedMs'] for e in images], default=None),
            'inProcessOnly': sources == 0 and disks == 0 and dispatched == 0
                             and marker['panes'] > 0 and panes == marker['panes'],
        })
    return result
