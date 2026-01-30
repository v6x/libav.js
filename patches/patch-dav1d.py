#!/usr/bin/env python3
import sys

with open(sys.argv[1], 'r') as f:
    content = f.read()

# Add emscripten and emscripten-thr cases before 'else'
old_else = """    rc_data.set('COPYRIGHT_YEARS', '2018-2024')
else
    thread_dependency = dependency('threads')
    thread_compat_dep = []"""

new_else = """    rc_data.set('COPYRIGHT_YEARS', '2018-2024')
elif host_machine.system() == 'emscripten'
    # Emscripten non-threaded build: disable pthread
    thread_dependency = []
    thread_compat_dep = []
    rt_dependency = []
    have_posix_memalign = false
    have_memalign = false
    have_aligned_alloc = true
elif host_machine.system() == 'emscripten-thr'
    # Emscripten threaded build: use pthread
    thread_dependency = dependency('threads')
    thread_compat_dep = []
    rt_dependency = []
    have_posix_memalign = false
    have_memalign = false
    have_aligned_alloc = true
else
    thread_dependency = dependency('threads')
    thread_compat_dep = []"""

content = content.replace(old_else, new_else)

# Skip wasm check for emscripten
old_wasm = "if host_machine.cpu_family().startswith('wasm')"
new_wasm = "if host_machine.cpu_family().startswith('wasm') and host_machine.system() not in ['emscripten', 'emscripten-thr']"
content = content.replace(old_wasm, new_wasm)

with open(sys.argv[1], 'w') as f:
    f.write(content)

print("Patched successfully")
