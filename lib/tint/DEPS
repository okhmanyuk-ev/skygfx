use_relative_paths = True

gclient_gn_args_file = 'build/config/gclient_args.gni'

gclient_gn_args = [
  'generate_location_tags',
]

vars = {
  'chromium_git': 'https://chromium.googlesource.com',

  'tint_gn_revision': 'git_revision:bd99dbf98cbdefe18a4128189665c5761263bcfb',

  # We don't use location metadata in our test isolates.
  'generate_location_tags': False,

  # Fetch configuration files required for the 'use_remoteexec' gn arg
  'download_remoteexec_cfg': False,
  # RBE instance to use for running remote builds
  'rbe_instance': Str('projects/rbe-chrome-untrusted/instances/default_instance'),
  # RBE project to download rewrapper config files for. Only needed if
  # different from the project used in 'rbe_instance'
  'rewrapper_cfg_project': Str(''),
  # reclient CIPD package
  'reclient_package': 'infra/rbe/client/',
  # reclient CIPD package version
  'reclient_version': 're_client_version:0.108.0.7cdbbe9-gomaip',
}

deps = {
  'third_party/gpuweb-cts': {
    'url': '{chromium_git}/external/github.com/gpuweb/cts@b0291fd966b55a5efc496772555b94842bde1085',
  },

  'third_party/vulkan-deps': {
    'url': '{chromium_git}/vulkan-deps@62a84300c2fee0c5957675b0c1e5a0fe9ea7ac40',
  },

  # Dependencies required to use GN/Clang in standalone
  'build': {
    'url': '{chromium_git}/chromium/src/build@5885d3c24833ad72845a52a1b913a2b8bc651b56',
  },

  'buildtools': {
    'url': '{chromium_git}/chromium/src/buildtools@a9a6f0c49d0e8fa0cda37337430b4736ab3dc944',
  },

  'tools/clang': {
    'url': '{chromium_git}/chromium/src/tools/clang@8f75392b4aa947fb55c7c206b36804229595e4da',
  },

  'buildtools/clang_format/script': {
    'url': '{chromium_git}/external/github.com/llvm/llvm-project/clang/tools/clang-format.git@8b525d2747f2584fc35d8c7e612e66f377858df7',
  },

  'buildtools/linux64': {
    'packages': [{
      'package': 'gn/gn/linux-amd64',
      'version': Var('tint_gn_revision'),
    }],
    'dep_type': 'cipd',
    'condition': 'host_os == "linux"',
  },
  'buildtools/mac': {
    'packages': [{
      'package': 'gn/gn/mac-${{arch}}',
      'version': Var('tint_gn_revision'),
    }],
    'dep_type': 'cipd',
    'condition': 'host_os == "mac"',
  },
  'buildtools/win': {
    'packages': [{
      'package': 'gn/gn/windows-amd64',
      'version': Var('tint_gn_revision'),
    }],
    'dep_type': 'cipd',
    'condition': 'host_os == "win"',
  },

  'buildtools/reclient': {
    'packages': [
      {
        'package': Var('reclient_package') + '${{platform}}',
        'version': Var('reclient_version'),
      }
    ],
    'dep_type': 'cipd',
  },

  'third_party/libc++/src': {
    'url': '{chromium_git}/external/github.com/llvm/llvm-project/libcxx.git@84fb809dd6dae36d556dc0bb702c6cc2ce9d4b80',
  },

  'third_party/libc++abi/src': {
    'url': '{chromium_git}/external/github.com/llvm/llvm-project/libcxxabi.git@d4760c0af99ccc9bce077960d5ddde4d66146c05',
  },

  'third_party/ninja': {
    'packages': [
      # https://chrome-infra-packages.appspot.com/p/infra/3pp/tools/ninja
      {
        'package': 'infra/3pp/tools/ninja/${{platform}}',
        'version': 'version:2@1.11.1.chromium.6',
      }
    ],
    'dep_type': 'cipd',
  },

  'third_party/abseil-cpp': {
    'url': '{chromium_git}/chromium/src/third_party/abseil-cpp@4ef9b33175828ea46d091e7e5ec28259d39a8ba5',
  },

  'third_party/dxc': {
    'url': '{chromium_git}/external/github.com/microsoft/DirectXShaderCompiler@e7b78ff9c99c19a6a0c98256db9794e0af4eb59d',
  },

  # Dependencies required for testing
  'testing': {
    'url': '{chromium_git}/chromium/src/testing@035a9b18047370df7403758b006e6c9696d6b84d',
  },

  'third_party/catapult': {
    'url': '{chromium_git}/catapult.git@37e879a7d13cbaa4925e09fc02b0f9276e060f0a',
  },

  'third_party/google_benchmark/src': {
    'url': '{chromium_git}/external/github.com/google/benchmark.git@efc89f0b524780b1994d5dddd83a92718e5be492',
  },

  'third_party/googletest': {
    'url': '{chromium_git}/external/github.com/google/googletest.git@b73f27fd164456fea9aba56163f5511355a03272',
  },

  'third_party/protobuf': {
    'url': '{chromium_git}/chromium/src/third_party/protobuf@41759e11ec427e29e1a72b9401d2af3f6e02d839',
  },
}

hooks = [
  # Pull clang-format binaries using checked-in hashes.
  {
    'name': 'clang_format_win',
    'pattern': '.',
    'condition': 'host_os == "win"',
    'action': [ 'download_from_google_storage',
                '--no_resume',
                '--no_auth',
                '--bucket', 'chromium-clang-format',
                '-s', 'buildtools/win/clang-format.exe.sha1',
    ],
  },
  {
    'name': 'clang_format_mac_x64',
    'pattern': '.',
    'condition': 'host_os == "mac" and host_cpu == "x64"',
    'action': [ 'download_from_google_storage',
                '--no_resume',
                '--no_auth',
                '--bucket', 'chromium-clang-format',
                '-s', 'buildtools/mac/clang-format.x64.sha1',
                '-o', 'buildtools/mac/clang-format',
    ],
  },
  {
    'name': 'clang_format_mac_arm64',
    'pattern': '.',
    'condition': 'host_os == "mac" and host_cpu == "arm64"',
    'action': [ 'download_from_google_storage',
                '--no_resume',
                '--no_auth',
                '--bucket', 'chromium-clang-format',
                '-s', 'buildtools/mac/clang-format.arm64.sha1',
                '-o', 'buildtools/mac/clang-format',
    ],
  },
  {
    'name': 'clang_format_linux',
    'pattern': '.',
    'condition': 'host_os == "linux"',
    'action': [ 'download_from_google_storage',
                '--no_resume',
                '--no_auth',
                '--bucket', 'chromium-clang-format',
                '-s', 'buildtools/linux64/clang-format.sha1',
    ],
  },

  # Pull the compilers and system libraries for hermetic builds
  {
    'name': 'sysroot_x86',
    'pattern': '.',
    'condition': 'checkout_linux and ((checkout_x86 or checkout_x64))',
    'action': ['python3', 'build/linux/sysroot_scripts/install-sysroot.py',
               '--arch=x86'],
  },
  {
    'name': 'sysroot_x64',
    'pattern': '.',
    'condition': 'checkout_linux and (checkout_x64)',
    'action': ['python3', 'build/linux/sysroot_scripts/install-sysroot.py',
               '--arch=x64'],
  },
  {
    # Update the Mac toolchain if necessary.
    'name': 'mac_toolchain',
    'pattern': '.',
    'condition': 'checkout_mac',
    'action': ['python3', 'build/mac_toolchain.py'],
  },
  {
    # Update the Windows toolchain if necessary. Must run before 'clang' below.
    'name': 'win_toolchain',
    'pattern': '.',
    'condition': 'checkout_win',
    'action': ['python3', 'build/vs_toolchain.py', 'update', '--force'],
  },
  {
    # Note: On Win, this should run after win_toolchain, as it may use it.
    'name': 'clang',
    'pattern': '.',
    'action': ['python3', 'tools/clang/scripts/update.py'],
  },
  {
    # Pull rc binaries using checked-in hashes.
    'name': 'rc_win',
    'pattern': '.',
    'condition': 'checkout_win and (host_os == "win")',
    'action': [ 'download_from_google_storage',
                '--no_resume',
                '--no_auth',
                '--bucket', 'chromium-browser-clang/rc',
                '-s', 'build/toolchain/win/rc/win/rc.exe.sha1',
    ],
  },
  # Update build/util/LASTCHANGE.
  {
    'name': 'lastchange',
    'pattern': '.',
    'action': ['python3', 'build/util/lastchange.py',
               '-o', 'build/util/LASTCHANGE'],
  },
  {
    # Download remote exec cfg files
    'name': 'fetch_reclient_cfgs',
    'pattern': '.',
    'condition': 'download_remoteexec_cfg',
    'action': ['python3',
               'buildtools/reclient_cfgs/fetch_reclient_cfgs.py',
               '--rbe_instance',
               Var('rbe_instance'),
               '--reproxy_cfg_template',
               'reproxy.cfg.template',
               '--rewrapper_cfg_project',
               Var('rewrapper_cfg_project'),
               '--quiet',
               ],
  },
]

recursedeps = [
  # buildtools provides clang_format, libc++, and libc++abi
  'buildtools',
  # vulkan-deps provides spirv-headers, spirv-tools & gslang
  # It also provides other Vulkan tools that Tint doesn't use
  'third_party/vulkan-deps',
]
