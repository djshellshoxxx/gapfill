import subprocess
import unittest
from unittest.mock import Mock

from tools.qa import x11_test_session as session


class X11TestSessionTests(unittest.TestCase):
    def library(self):
        library = Mock()
        library.XOpenDisplay.return_value = 123
        library.XInternAtom.return_value = 456
        return library

    def test_native_child_runs_with_atoms_established_and_connection_held_and_keeps_exit_code(self):
        library = self.library()

        def run(command, check):
            self.assertEqual(command, ["test binary", "argument with spaces"])
            self.assertFalse(check)
            self.assertEqual(library.XInternAtom.call_count, len(session.DESKTOP_ATOMS))
            library.XSync.assert_called_once_with(123, 0)
            library.XCloseDisplay.assert_not_called()
            return subprocess.CompletedProcess(command, 7)

        self.assertEqual(session.run_in_session(["test binary", "argument with spaces"], platform="linux",
                                               library_loader=lambda _: library, runner=run), 7)
        library.XCloseDisplay.assert_called_once_with(123)

    def test_unavailable_display_never_launches_the_command(self):
        library = self.library()
        library.XOpenDisplay.return_value = None
        run = Mock()
        with self.assertRaisesRegex(RuntimeError, "Cannot open DISPLAY"):
            session.run_in_session(["tests"], platform="linux", library_loader=lambda _: library, runner=run)
        run.assert_not_called()
        library.XCloseDisplay.assert_not_called()

    def test_connection_is_closed_when_child_launch_raises(self):
        library = self.library()
        with self.assertRaises(OSError):
            session.run_in_session(["missing"], platform="linux", library_loader=lambda _: library,
                                   runner=Mock(side_effect=OSError("missing executable")))
        library.XCloseDisplay.assert_called_once_with(123)

    def test_non_x11_platform_preserves_the_command_without_loading_x11(self):
        loader = Mock()
        run = Mock(return_value=subprocess.CompletedProcess(["tests"], 0))
        self.assertEqual(session.run_in_session(["tests"], platform="darwin", library_loader=loader, runner=run), 0)
        loader.assert_not_called()
        run.assert_called_once_with(["tests"], check=False)


if __name__ == "__main__":
    unittest.main()
