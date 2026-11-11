import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('pruning', Path(__file__).resolve().parents[1] / 'scripts/prune_windows_qt_dlls.py')
pruning = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pruning)


class PackagePruningTest(unittest.TestCase):
    def test_dynamic_plugins_and_transitive_dependencies_survive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            graph = {
                'MVPImageViewer.exe': ['QT6CORE.DLL'],
                'Qt6Core.dll': [],
                'Qt6Quick.dll': ['Qt6Core.dll'],
                'Qt6Svg.dll': ['Qt6Core.dll'],
                'Qt6Unused.dll': ['Qt6UnusedDependency.dll'],
                'Qt6UnusedDependency.dll': [],
                'qml/kept/plugin.dll': ['Qt6Quick.dll'],
                'imageformats/qsvg.dll': ['Qt6Svg.dll'],
                'dynamic-backend.dll': [],
            }
            for name in graph:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            unused = pruning.unreachable_qt_dlls(root, lambda p: graph[p.relative_to(root).as_posix()])
            self.assertEqual({p.name for p in unused}, {'Qt6Unused.dll', 'Qt6UnusedDependency.dll'})

    def test_dependency_read_failure_does_not_delete_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('MVPImageViewer.exe', 'Qt6Core.dll'):
                (root / name).touch()
            def fail(_):
                raise RuntimeError('unreadable imports')
            with self.assertRaises(RuntimeError):
                pruning.unreachable_qt_dlls(root, fail)
            self.assertTrue((root / 'Qt6Core.dll').exists())


if __name__ == '__main__':
    unittest.main()
