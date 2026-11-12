project('dfiles', ['cpp', 'c'],
  version: '0.1.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=3',
    'werror=true',
  ]
)

# Compiler configuration
cpp = meson.get_compiler('cpp')

# Dependencies
gtk4_dep = dependency('gtk4', version: '>= 4.12')
adw_dep = dependency('libadwaita-1', version: '>= 1.4')
vte_dep = dependency('vte-2.91-gtk4', version: '>= 0.74')
gio_dep = dependency('gio-2.0', version: '>= 2.74')
gio_unix_dep = dependency('gio-unix-2.0')

dfiles_deps = [
  gtk4_dep,
  adw_dep,
  vte_dep,
  gio_dep,
  gio_unix_dep,
]

# Include directories
inc = include_directories('src')

# Core library sources (shared between executable and test targets)
core_sources = [
  'src/core/app.cpp',
  'src/model/file_item.cpp',
  'src/model/directory_model.cpp',
  'src/views/file_view.cpp',
  'src/views/pane_view.cpp',
  'src/views/split_view.cpp',
  'src/ui/headerbar.cpp',
  'src/ui/sidebar.cpp',
  'src/ui/window.cpp',
]

# Sources for main dfiles executable
sources = ['src/main.cpp'] + core_sources

# Executable target
executable('dfiles',
  sources,
  include_directories: inc,
  dependencies: dfiles_deps,
  install: true,
)

# Phase 1 Functional Test Suite
test_functional_sources = [
  'tests/test_functional.cpp',
] + core_sources

test_functional_bin = executable('test_functional',
  test_functional_sources,
  include_directories: inc,
  dependencies: dfiles_deps,
)

test('functional', test_functional_bin,
  is_parallel: false,
  env: ['G_DEBUG=fatal-criticals']
)

# Phase 1 Adversarial Stress Test Suite
test_adversarial_sources = [
  'tests/adversarial_stress.cpp',
] + core_sources

test_adversarial_bin = executable('test_adversarial',
  test_adversarial_sources,
  include_directories: inc,
  dependencies: dfiles_deps,
)

test('adversarial', test_adversarial_bin,
  is_parallel: false,
  env: ['G_DEBUG=fatal-criticals']
)
