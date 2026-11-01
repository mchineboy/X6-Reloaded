"""Image selection must never prune the emulator's supported driver pool."""
import argparse
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import reloaded


class BuildSelectionTests(unittest.TestCase):
    def exercise_selection(self, selected):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'config').mkdir()
            (root / 'config/mame2000-arcade.json').write_text(json.dumps({'drivers': ['rygar', 'galaga']}))
            boot = root / 'boot-output'
            boot.mkdir()
            for name in ['x6menu.axf', 'stage.bin', 'payload.bin']:
                (boot / name).write_bytes(b'boot fixture')
            firmware = root / 'firmware'
            firmware.write_bytes(b'private firmware fixture')
            config = {'games': selected, 'roms': str(root / 'roms'), 'firmware': str(firmware), 'toolchain': str(root / 'toolchain')}
            compiled = []
            image_files = {}

            def compile_(command, **kwargs):
                profile = Path(command[command.index('--manifest') + 1])
                compiled.extend(json.loads(profile.read_text())['drivers'])
                target = root / 'build/module/x6-dkong.experimental.elf'
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(b'compiled pool fixture')

            def audit_(archive, driver, name, output):
                output.write_bytes(b'selected ROM fixture')
                return {'game': name, 'output_verified': True}

            def image_(target, files, stage, payload):
                image_files.update(files)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(b'image fixture')
                return {}

            args = argparse.Namespace(stock_backup=None, core_source=None, cc='clang', ld=None)
            with patch.object(reloaded, 'ROOT', root), patch.object(reloaded, 'load_config', return_value=config), \
                    patch.object(reloaded, 'prepare'), patch.object(reloaded, 'inspect_game', return_value={'available': True}), \
                    patch.object(reloaded.subprocess, 'run', side_effect=compile_), \
                    patch.object(reloaded.firmware, 'build', return_value=boot), \
                    patch.object(reloaded, 'locate_driver', return_value='fixture.c'), \
                    patch.object(reloaded, 'audit', side_effect=audit_), \
                    patch.object(reloaded.sdimage, 'build', side_effect=image_):
                reloaded.build(args)
            self.assertEqual(compiled, ['rygar', 'galaga'])
            self.assertEqual('roms/mame/rygar.zip' in image_files, 'rygar' in selected)
            self.assertNotIn('roms/mame/galaga.zip', image_files)
            self.assertIn('roms/mame/README.TXT', image_files)
            self.assertIn('roms/original/README.TXT', image_files)
            report = json.loads((root / 'build/image-report.json').read_text())
            self.assertEqual(report['selected_games'], selected)
            self.assertEqual(report['compiled_drivers'], ['rygar', 'galaga'])

    def test_one_rom_keeps_the_supported_pool(self):
        self.exercise_selection(['rygar'])

    def test_no_roms_still_builds_a_complete_engine(self):
        self.exercise_selection([])

    def test_configure_none_needs_no_available_roms(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            toolchain = root / 'toolchain'
            (toolchain / 'bin').mkdir(parents=True)
            (toolchain / 'bin/arm-none-eabi-gcc').touch()
            config = root / 'config.json'
            args = argparse.Namespace(roms=root / 'empty', firmware=root / 'firmware',
                toolchain=toolchain, samples=None, stock_backup=None, core_source=None, games='none')
            with patch.object(reloaded, 'CONFIG', config), patch.object(reloaded.firmware, 'extract_stock'), \
                    patch.object(reloaded, 'prepare'), patch.object(reloaded, 'scan', return_value=[]):
                reloaded.configure(args)
            self.assertEqual(json.loads(config.read_text())['games'], [])


if __name__ == '__main__':
    unittest.main()
