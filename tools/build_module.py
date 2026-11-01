#!/usr/bin/env python3
"""Build the selected ARM926 arcade module for the SD-only X6 host.

Uses the pinned private core checkout and a user-provided ARM GNU toolchain.
No network fetch, global install, card write or flash access is performed.
"""
import argparse
import concurrent.futures
import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
from inspect_melis_module import inspect
from core import verify, UPSTREAM

def find_linker(explicit):
    if explicit: return explicit
    path = shutil.which('ld.lld')
    if path: return path
    for p in sorted((Path.home()/'.rustup/toolchains').glob('*/lib/rustlib/*/bin/gcc-ld/ld.lld')):
        if p.is_file(): return str(p)
    raise ValueError('ld.lld required; set LD_LLD=/path/to/ld.lld')

def driver_registry(names):
    for name in names:
        if not re.fullmatch(r'[a-z0-9_]+', name): raise ValueError('Invalid driver name')
    return ('#include "driver.h"\n'
            'struct GameDriver driver_0={__FILE__,0,"",0,0,0,0,0,0,0,NOT_A_DRIVER};\n'
            'extern struct GameDriver '+','.join('driver_'+n for n in names)+';\n'
            'const struct GameDriver *drivers[]={'+','.join('&driver_'+n for n in names)+',0};\n')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain',type=Path,required=True)
    parser.add_argument('--fast',action='store_true',help='ARM DrZ80/Cyclone candidate; requires --five')
    parser.add_argument('--five',action='store_true')
    parser.add_argument('--manifest',type=Path,required=True,help='selected driver manifest')
    parser.add_argument('--altbeast-target-rate',action='store_true')
    parser.add_argument('--game-samples',action='store_true',help='ROM-adjacent sample ZIP lookup and Galaga startup sample fallback')
    parser.add_argument('--area-scale',action='store_true',help='area-filter downscaled game pictures to retain thin source rows/columns')
    parser.add_argument('--timing-calibration',action='store_true',help='display cumulative tick seconds for external stopwatch calibration')
    parser.add_argument('--galaga-c-z80',action='store_true',help='Galaga-only C Z80 compatibility trial; requires --five --fast')
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--ld')
    parser.add_argument('--build-dir',type=Path,help='separate local candidate output directory')
    parser.set_defaults(qemu_test=False,host_skip_test=False,capture_audio=False,galaga_launch_test=False,altbeast_drz80_sound=False)
    args = parser.parse_args()
    if args.manifest:args.five=True
    out = (args.build_dir or root/'build/mame-arm').resolve();out.mkdir(parents=True, exist_ok=True)
    upstream = UPSTREAM
    manifest = json.loads(args.manifest.read_text())
    expanded = True
    runtime = args.toolchain.resolve()
    verify()
    revision = manifest['commit']
    patch = root/'config/core-edits.json'
    if args.fast and not args.five:raise SystemExit('--fast requires --five')
    if args.host_skip_test and not(args.five and args.qemu_test):raise SystemExit('--host-skip-test requires --five --qemu-test')
    if args.capture_audio and not(args.five and args.qemu_test):raise SystemExit('--capture-audio requires --five --qemu-test')
    if args.altbeast_drz80_sound and not(args.five and args.fast):raise SystemExit('--altbeast-drz80-sound requires --five --fast')
    if args.galaga_c_z80 and not(args.five and args.fast):raise SystemExit('--galaga-c-z80 requires --five --fast')
    if args.galaga_launch_test and not(args.five and args.qemu_test):raise SystemExit('--galaga-launch-test requires --five --qemu-test')
    defines = [value for value in manifest['defines'] if value != '-fPIC']
    if args.altbeast_target_rate:
        if not args.five:raise SystemExit('--altbeast-target-rate requires --five')
        defines+=['-DX6_ALTBEAST_TARGET_RATE']
    if args.game_samples:defines+=['-DX6_GAME_SAMPLE_PATH']
    if args.area_scale:defines+=['-DX6_AREA_SCALE']
    if args.timing_calibration:defines+=['-DX6_TIMING_CALIBRATION']
    if args.galaga_c_z80:defines+=['-DX6_GALAGA_C_Z80']
    if args.altbeast_drz80_sound:defines+=['-DX6_ALTBEAST_DRZ80_SOUND']
    if args.fast:defines+=['-DHAS_DRZ80=1','-DHAS_CYCLONE=1','-DHAS_M68010=1','-DIS_ARM=1']
    flags = ['--target=arm-none-eabi','-mcpu=arm926ej-s','-marm','-mfloat-abi=soft',
             '-mno-unaligned-access','--sysroot='+str(runtime/'arm-none-eabi'),
             '-isystem',str(runtime/'arm-none-eabi/include'),'-std=gnu99','-O2','-fcommon',
             '-ffunction-sections','-fdata-sections','-fno-builtin','-ffreestanding',
             '-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
             *defines,'-I'+str(root/'include'),'-DX6_MELIS_RUNTIME','-DGIT_VERSION="181b42e"','-DNDEBUG']
    own = ['src/arcade/frontend.c','src/arcade/music.c','src/arcade/catalog.c','src/arcade/catalog_mame.c','src/ui/gfx.c','src/ui/menu.c','src/ui/font_data.c','src/arcade/module.c','src/arcade/timing.c','src/arcade/monitor.c','src/runtime/timer.S','src/runtime/memory.c',
           'src/runtime/files.c','src/runtime/newlib.c','src/runtime/stack.c','src/runtime/stack.S','src/probe/services.S']
    if args.area_scale:own+=['src/arcade/scale.c']
    if args.five: own+=([] if expanded else ['src/arcade/drivers.c'])+['src/runtime/large_memory.c','src/runtime/pages.S']
    if args.fast:
        manifest['sources']+=['./src/cpu/z80_drz80/drz80_z80.c','./src/cpu/m68000_cyclone/c68000.c','./src/cpu/z80_drz80/drz80.s','./src/cpu/m68000_cyclone/cyclone.s']
    sources = [(name,upstream/name) for name in manifest['sources']] + [(name,root/name) for name in own]
    if expanded:
        registry=out/'drivers_generated.c';registry.write_text(driver_registry(manifest['drivers']))
        sources.append(('drivers_generated.c',registry))

    def compile_one(item):
        name, source = item
        obj = out/(name.removeprefix('./')+'.o');obj.parent.mkdir(parents=True, exist_ok=True)
        command = [str(runtime/'bin/arm-none-eabi-as'),'-mcpu=arm926ej-s',str(source),'-o',str(obj)] if source.suffix=='.s' else [args.cc,*flags,'-c',str(source),'-o',str(obj)]
        result = subprocess.run(command,cwd=upstream,capture_output=True)
        obj.with_suffix('.log').write_bytes(result.stdout+result.stderr)
        return name,obj,result.returncode

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(compile_one,sources))
    failures = [name for name,_,code in results if code]
    if failures:
        raise SystemExit('ARM compile failed; see object logs: '+', '.join(failures))
    linker = find_linker(args.ld)
    target = out/'x6-dkong.experimental.elf'
    libraries = [runtime/'arm-none-eabi/lib/libm.a',runtime/'arm-none-eabi/lib/libc.a',
                 runtime/'lib/gcc/arm-none-eabi/13.3.1/libgcc.a']
    command = [linker,'-m','armelf','--nmagic','--no-undefined','--gc-sections',
               '-T',str(root/'src/arcade/module.ld'),*[str(obj) for _,obj,_ in results],
               '--start-group',*map(str,libraries),'--end-group','-Map='+str(out/'link.map'),'-o',str(target)]
    linked = subprocess.run(command,capture_output=True)
    (out/'link.log').write_bytes(linked.stdout+linked.stderr);linked.check_returncode()
    elf = inspect(target)
    allocated = [section for section in elf['sections'] if int(section['flags'],16)&2 and section['name']!='MAGIC']
    if elf['elf_entry']!='0xe9100000' or {s['name'] for s in allocated} != {'EXEC_MOD','EXEC_DATA','EXEC_BSS'}:
        raise SystemExit('Unexpected entry or orphan allocated section')
    if elf['magic']['header_words_after_signature']!=['0x1000000','0x91','0x4f0000','0x400']:
        raise SystemExit('Unexpected module descriptor')
    tools = runtime/'bin'
    nm = subprocess.check_output([str(tools/'arm-none-eabi-nm'),'--undefined-only','--format=posix',str(target)],text=True)
    unresolved = [line.split() for line in nm.splitlines() if line.strip()]
    if any(parts[1] not in {'w','v'} for parts in unresolved):
        raise SystemExit('Unexpected strong undefined symbol')
    disassembly = subprocess.check_output([str(tools/'arm-none-eabi-objdump'),'-d',str(target)],text=True)
    (out/'disassembly.txt').write_text(disassembly)
    calls = [int(value,0) for value in re.findall(r'\bsvc\s+#?(0x[0-9a-fA-F]+|[0-9]+)',disassembly)]
    # 0x2605 fwrite: per-game difficulty settings file on F: (feat/menu-contrast-music).
    expected = [0x241c,0x241f,0x2507,0x2508,0x2509,0x2602,0x2603,0x2604,0x2605,0x2606,0x2607]
    if args.five: expected.remove(0x2509);expected+=[0x2500,0x2501];expected.sort()
    if sorted(calls)!=expected:
        raise SystemExit('SVC instructions differ from the reviewed file/heap/timer services')
    report = {'date':datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'kind':'experimental ARM926 Melis module; hardware game execution pending',
              'source_commit':revision,'upstream_patch_sha256':sha(patch),
              'compiler':subprocess.check_output([args.cc,'--version'],text=True).splitlines()[0],
              'compile_flags':flags,'link_command':command,'objects_compiled':len(results),
              'own_source_sha256':{name:sha(root/name) for name in own},
              'own_header_sha256':{str(p.relative_to(root)):sha(p) for p in sorted((root/'include').glob('*.h'))},
              'runtime_toolchain':{'directory':str(runtime),'gcc_version':subprocess.check_output([str(runtime/'bin/arm-none-eabi-gcc'),'--version'],text=True).splitlines()[0]},'elf':elf,
              'resident_span_bytes':max(int(s['address'],16)+s['size'] for s in allocated)-0xe9100000,
              'strong_undefined_symbols':[],
              'optional_weak_unwind_hooks':[parts[0] for parts in unresolved],
              'cxx_exceptions_supported':False,'svc_numbers':[hex(n) for n in sorted(calls)],
              'private_stack_capacity_bytes':65536,'framebuffer_bytes_owned_by_host':320*240*4,'qemu_test':'not run',
              'game_samples':args.game_samples,'area_scale':args.area_scale,'timing_calibration':args.timing_calibration,'galaga_c_z80':args.galaga_c_z80,'altbeast_target_rate':args.altbeast_target_rate,'altbeast_drz80_sound':args.altbeast_drz80_sound,'arm_optimized_cpu_cores':args.fast,'control_monitor_enabled':True,'timing_tick_hz_assumption':100,'target_fps':60,'timer_services_device_tested':False,'registered_drivers':manifest.get('drivers',['dkong']),'physical_input_map_verified':False,'x6_game_tested':False,'installed':False}
    (out/'compile-report.json').write_text(json.dumps({'flags':flags,'sources':[str(p) for _,p in sources]},indent=2)+'\n')
    (out/'link-command.json').write_text(json.dumps(command,indent=2)+'\n')
    (out/'build-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Built module: {target}')

if __name__ == '__main__':
    main()
