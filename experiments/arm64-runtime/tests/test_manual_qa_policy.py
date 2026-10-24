"""No window, game or preference writes; regress manual-package input isolation."""
from pathlib import Path
import ast, sys, unittest
P = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(P / 'tools'))
from manual_qa_policy import FORBIDDEN, validate_manual_environment

class ManualPolicy(unittest.TestCase):
    def test_each_automation_variable_rejected_even_when_empty(self):
        for key in FORBIDDEN:
            for value in ('', '0', '1', '/private/test'):
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    validate_manual_environment({key:value})
    def test_original_manual_settings_allowed(self):
        environment={'BB_ARM_LAUNCHER_COMPAT':'1','BB_AUDIO':'none','BB_ARM_SESSION_NONCE':'authored'}
        self.assertIs(validate_manual_environment(environment), environment)
    def test_actual_package_extra_has_no_automation_keys(self):
        tree=ast.parse((P/'tools/prepare_launcher_gui_qa.py').read_text())
        definition=next(n.value for n in ast.walk(tree) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='extra' for t in n.targets))
        self.assertIsInstance(definition, ast.Call)
        keys={n.arg for n in definition.keywords}
        self.assertFalse(keys.intersection(FORBIDDEN))
        self.assertIn('BB_ARM_SESSION_NONCE', keys)
        self.assertTrue(any(isinstance(n,ast.Call) and isinstance(n.func,ast.Name) and n.func.id=='validate_manual_environment' for n in ast.walk(tree)))
if __name__=='__main__':unittest.main()
