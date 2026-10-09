import unittest

from performance_revisit import summarize_revisits


class CacheRevisitClassification(unittest.TestCase):
    def fixture(self):
        return [
            {'event': 'scenario.memory_revisit', 'sinceStartMs': 100, 'shown': True,
             'iteration': 1, 'panes': 1},
            {'event': 'viewport.demand', 'sinceStartMs': 101, 'owner': 'pane', 'generation': 3},
            {'event': 'loader.memory_hit', 'sinceStartMs': 102},
            {'event': 'viewport.presented', 'sinceStartMs': 120, 'owner': 'pane',
             'generation': 3, 'elapsedMs': 19, 'animationCycles': 2},
            {'event': 'scenario.memory_revisit', 'sinceStartMs': 200, 'shown': False,
             'iteration': 2, 'panes': 1},
        ]

    def test_ready_memory_revisit(self):
        sample = summarize_revisits(self.fixture())[0]
        self.assertTrue(sample['inProcessOnly'])
        self.assertTrue(sample['stableViewport'])
        self.assertEqual((sample['maxFillMs'], sample['maxAnimationCycles']), (19, 2))

    def test_dispatched_work_cannot_pass_even_if_completion_is_late(self):
        events = self.fixture()
        events.append({'event': 'loader.dispatched', 'sinceStartMs': 130})
        events.append({'event': 'loader.completed', 'sinceStartMs': 210, 'sourceDecode': True})
        self.assertFalse(summarize_revisits(events)[0]['inProcessOnly'])

    def test_disk_and_source_work_are_excluded(self):
        for field in ('diskHit', 'sourceDecode'):
            events = self.fixture()
            events.append({'event': 'loader.completed', 'sinceStartMs': 130, field: True})
            self.assertFalse(summarize_revisits(events)[0]['inProcessOnly'])

    def test_old_generation_or_missing_pane_cannot_pass(self):
        events = self.fixture()
        events[3]['generation'] = 2
        self.assertFalse(summarize_revisits(events)[0]['inProcessOnly'])
        events = self.fixture()
        events[0]['panes'] = 2
        self.assertFalse(summarize_revisits(events)[0]['inProcessOnly'])
        events[0]['panes'] = 0
        self.assertFalse(summarize_revisits(events)[0]['inProcessOnly'])

    def test_changed_viewport_is_reported_separately(self):
        events = self.fixture()
        events.append({'event': 'viewport.demand', 'sinceStartMs': 140,
                       'owner': 'pane', 'generation': 4})
        self.assertFalse(summarize_revisits(events)[0]['stableViewport'])


if __name__ == '__main__':
    unittest.main()
