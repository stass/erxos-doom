# frozen_string_literal: true

# Builds Doom as a third-party erXos app and boots it on QEMU with a virtio-gpu:
# what a deployer of erXos does today to add a native C app with a window.
# A demo, never part of erXos's tree or its default images.
#
#   ERXOS_ROOT=... WAD=.../freedoom1.wad \
#   ERX_CLANG=... ERX_LLD=... ruby run.rb build | boot OUTDIR | package
#
# package stages Doom as an erXos app directory, app/app.txt beside bin/doom,
# in $ERXOS_ROOT/build/doom/app, which erXos's packer takes into a local test
# image (docs/design-packaging.md, "Adding an app to an image"):
#
#   tools/erx pack --app build/doom/app --enable doom --local --medium
#
# Doom's license (GPL-2.0-or-later, its WAD's BSD-3-Clause) keeps it out of
# every image but a local one, which never leaves the host that built it.
#
# The app, as docs/design-apps.md defines one: a principal (doom), its session
# template, the facet rows granting it authority (the display's window tree with
# a pixel area) and a start row naming the display, all in APP_ROWS; its code is
# bin/doom in the domain release, and the session's -eval (LAUNCH) starts bin/doom
# with a window of its own, whose files it finds in its own /dev.
# Every place this reaches past what an app should need is marked BOUNDARY GAP
# and listed in README.md.

require 'etc'
require 'fileutils'
require 'open3'

ROOT = File.expand_path(ENV.fetch('ERXOS_ROOT'))
$LOAD_PATH.unshift(File.join(ROOT, 'tools/lib'))
# BOUNDARY GAP 1: there is no app SDK or packager; the deployer drives erXos's
# own internal build library (tools/lib/erx) to compile, link and pack.
require 'erx/programs'
require 'erx/display_domain'
require 'erx/session_domain'

module ErxDoom
  HERE = __dir__
  # doomgeneric's sources, as upstream has them, and erXos's platform layer
  # beside them, as upstream keeps its other platforms'.
  DOOM_SRC = File.join(HERE, 'doomgeneric')
  WAD = File.expand_path(ENV.fetch('WAD', File.join(HERE, 'third_party/freedoom-0.13.0/freedoom1.wad')))
  COMPILER = ENV.fetch('ERX_CLANG')
  LINKER = ENV.fetch('ERX_LLD')
  BUILD = File.join(ROOT, 'build/doom')
  # The platform layer, and its key table, which test_keys.c tests on the host.
  GLUE = %w[doomgeneric_erxos.c erxdoom_keys.c].freeze

  # doomgeneric's portable objects (its Makefile's SRC_DOOM) less the X11
  # platform part, whose job doomgeneric_erxos.c does.
  DOOM_OBJECTS = %w[dummy am_map doomdef doomstat dstrings d_event d_items d_iwad d_loop d_main d_mode d_net
                    f_finale f_wipe g_game hu_lib hu_stuff info i_cdmus i_endoom i_joystick i_scale i_sound
                    i_system i_timer memio m_argv m_bbox m_cheat m_config m_controls m_fixed m_menu m_misc
                    m_random p_ceilng p_doors p_enemy p_floor p_inter p_lights p_map p_maputl p_mobj p_plats
                    p_pspr p_saveg p_setup p_sight p_spec p_switch p_telept p_tick p_user r_bsp r_data r_draw
                    r_main r_plane r_segs r_sky r_things sha1 sounds statdump st_lib st_stuff s_sound tables
                    v_video wi_stuff w_checksum w_file w_main w_wad z_zone w_file_stdc i_input i_video
                    doomgeneric].freeze

  PIXELS = 8 << 20
  LANE = 4096
  OUTER = 1 << 20
  # The session's own need (the app release: erx_win's client side and
  # erx_session) and bin/doom's: the linked WAD (28 MiB), Doom's 6 MiB zone,
  # its frame and code, with room. erx_program's cap is in pages.
  SESSION_BYTES = 36 << 20
  PROGRAM_BYTES = 64 << 20
  PROGRAM_CAP = PROGRAM_BYTES / 4096

  # The app's launcher: the session template's -eval. BOUNDARY GAP 2: an app's
  # own Erlang code cannot join the image's domain release
  # (ReleaseFiles.domain_boot admits only applications erXos's runtime builds),
  # so the launcher is one expression with no spaces or double quotes, which the
  # manifest's args string carries; a parenthesis stands where a space would.
  # The args must fit the boot page's strings, 2048 bytes less the domain
  # library's 295-byte reserve for node names and records and the room it
  # keeps for the session's display variable (erXos's
  # liberx/system/dom_boot_view.c, boot_strings_fit and its session check),
  # which leaves this expression under 1600 bytes; the manifest check refuses
  # a longer one.
  #
  # The launcher starts bin/doom with a window of its own (window => display):
  # the handler opens the window, starts its draw connection and gives bin/doom
  # a /dev holding draw, ctl, kbd and the window's other files
  # (docs/design-files.md, "A program's window"), so bin/doom draws and reads
  # keys itself and the launcher only waits for its end. The app release holds
  # erx_session, whose erx_session_export serves that /dev, beside erx_win.
  LAUNCH = [
    # Bin writes a binary as an atom, since the string has no double quotes.
    'Bin=fun(A)->atom_to_binary(A)end',
    # BOUNDARY GAP 7: the program's POSIX identity (pid, ppid, pgid, cwd,
    # umask, signal masks) comes from its starter; nothing gives an app's
    # launcher a pid, so it names one.
    # The display's grant and pixel area are installed after the session
    # starts, so the start answers {error,enodev} until they arrive.
    "Start=fun(F,N)->(case(erx_prog_handler:start(\#{image=>Bin('bin/doom'),cap=>#{PROGRAM_CAP}," \
    "pid=>100,ppid=>1,pgid=>100,env=>[],cwd=>Bin('/'),spawn=>false," \
    'sigmask=>[],sigignore=>[],umask=>18,window=>display,' \
    "argv=>[Bin(doom),Bin('-iwad'),Bin('freedoom1.wad')]}))of" \
    '{error,enodev}when(N>0)->timer:sleep(250),F(F,N-1);R->(R)end)end',
    '{ok,Handler,Mon,_}=Start(Start,240)',
    'erlang:display({doom_running,Handler})',
    # The handler's monitor reports the program's end, its status in the reason.
    'receive{_,Mon,process,Handler,Why}->erlang:display({doom_exit,Why})end'
  ].join(',')

  # The app's manifest rows (docs/design-apps.md, "An app"). BOUNDARY GAP 3: no
  # run-time install exists (docs/design-apps.md, "Packaging", a later design),
  # so the rows go into the machine's manifest at image build, and the
  # machine-wide sums below are grown by hand.
  APP_ROWS = <<~ROWS
    limits doom
      memory #{SESSION_BYTES + PROGRAM_BYTES}
      outer #{OUTER}
      grant @share.session
      spaces @share.session
      lane 65536
      connections 4
      crash 0
      flags spaces
      runtime_limits @runtime_limits.session
      peer_held 4096
      peer_held_total 4096
    principal doom
      memory #{SESSION_BYTES + PROGRAM_BYTES + OUTER}
      grant @share.users
      domains 1
      names 1
      default_limits @limits.native
      ceiling_limits @limits.native
    session doom
      limits @limits.doom
      runtime @artifact.emu
      release @artifact.app
      args "-- -root / -bindir /bin -progname erl -home / -nouser -nocookie -no_epmd -proto_dist erx -eval #{LAUNCH}"
    facet display
      grantee @session.doom
      service @service.display
      operations none
      max_request 131072
      in_flight 16
      reductions 100000
      period_ns 1000000000
      max_heap_size 262144
      parameter pixels #{PIXELS}
    start doom
      enable YES
      display @service.display
  ROWS

  module_function

  def manifest(source)
    areas = PIXELS + LANE
    Erx::Manifest::Edit.grow(source,
                           { ['limits display', 'outer'] => areas,
                             ['limits display', 'connections'] => 1,
                             ['principal system', 'memory'] => areas,
                             %w[machine root_memory] => areas + SESSION_BYTES + PROGRAM_BYTES + OUTER,
                             %w[machine max_domains] => 1,
                             %w[machine max_connections] => 1 }) + APP_ROWS
  end

  # Doom and the glue into libdoom.a, with the task code generation and musl's
  # headers. BOUNDARY GAP 4: an app has no place of its own for data files, so
  # the WAD is linked in (wad.S)
  # and every fopen of Doom's goes to erxdoom_fopen.
  def libdoom
    objects = File.join(BUILD, 'obj')
    FileUtils.mkdir_p(objects)
    flags = [*Erx::NativeRoot::CODE_FLAGS, '-isystem', File.join(ROOT, Erx::NativeRoot.libc_headers(ROOT)),
             '-D_DEFAULT_SOURCE', '-DNORMALUNIX', '-DLINUX', '-Dfopen=erxdoom_fopen', '-std=gnu17',
             '-I', DOOM_SRC]
    # Doom compiles with its warnings off; the glue with warnings as errors.
    sources = DOOM_OBJECTS.map { |name| [File.join(DOOM_SRC, "#{name}.c"), %w[-w]] } +
              GLUE.map { |name| [File.join(DOOM_SRC, name), %w[-Wall -Werror]] }
    jobs = sources.map do |source, warnings|
      out = File.join(objects, "#{File.basename(source, '.c')}.o")
      [out, [COMPILER, *flags, *warnings, '-c', source, '-o', out]]
    end
    wad_obj = File.join(objects, 'wad.o')
    jobs << [wad_obj, [COMPILER, *Erx::NativeRoot::CODE_FLAGS, "-DERXDOOM_WAD_PATH=\"#{WAD}\"", '-c',
                       File.join(DOOM_SRC, 'wad.S'), '-o', wad_obj]]
    jobs.each_slice(Etc.nprocessors) do |slice|
      slice.map { |_, argv| Thread.new { Open3.capture2e(*argv) } }.map(&:value).zip(slice).each do |(out, status), (_, argv)|
        raise "#{argv[-3]}: #{out}" unless status.success?
      end
    end
    archive = File.join(BUILD, 'libdoom.a')
    FileUtils.rm_f(archive)
    out, status = Open3.capture2e(File.join(File.dirname(COMPILER), 'llvm-ar'), 'rcs', archive, *jobs.map(&:first))
    raise out unless status.success?

    archive
  end

  # Builds test_keys.c with the host's compiler and runs it: true when it passes.
  def test_keys
    binary = File.join(BUILD, 'test_keys')
    FileUtils.mkdir_p(BUILD)
    out, status = Open3.capture2e(ENV.fetch('CC', 'cc'), '-std=c17', '-Wall', '-Werror', '-I', DOOM_SRC,
                                  File.join(DOOM_SRC, 'test_keys.c'), File.join(DOOM_SRC, 'erxdoom_keys.c'), '-o', binary)
    raise out unless status.success?

    out, status = Open3.capture2e(binary)
    puts out
    status.success?
  end

  # What build and package link: Fiasco, the runtime and bin/doom, linked over
  # libdoom.a as the programs suite's programs are.
  Linked = Data.define(:runner, :platform, :machine, :fiasco, :runtime, :archives, :program)

  def link
    runner = Erx::Runner.new(owner_root: ROOT, log_dir: File.join(ROOT, 'build/logs'))
    platform = Erx::Platform.development(ROOT)
    # BOUNDARY GAP 6: app code travels in the boot image, whose single file
    # must fit below the root task's place; the linked WAD pushes it past the
    # development machine's 64 MiB, so the deployer moves the root's place.
    machine = Erx::ProductionImage::Machine.development(platform).with(root_mib: 160)
    fiasco = Erx::Fiasco.new(root: ROOT, runner: runner, compiler: COMPILER)
    fiasco.build(jobs: 4)
    runtime = Erx::NativeRuntime.new(root: ROOT, runner: runner, compiler: COMPILER, linker: LINKER, fiasco: fiasco,
                                     platform: machine.platform)
    libc = runtime.build.libc
    native = Erx::NativeRoot.new(root: ROOT, runner: runner, compiler: COMPILER, linker: LINKER,
                                 platform: machine.platform, fiasco: fiasco)
    archives = [libdoom, libc, Erx::CompilerRt.archive(root: ROOT, runner: runner, compiler: COMPILER)]
    # The program links liberx's program entry and layer over libdoom.a, as the
    # programs suite's programs do.
    program = native.build_task_receipted(name: 'doom', sources: Erx::NativeRoot::PROGRAM,
                                          directory: File.join(BUILD, 'program'), archives: archives)
    Linked.new(runner: runner, platform: platform, machine: machine, fiasco: fiasco, runtime: runtime,
               archives: archives, program: program)
  end

  # The app directory erXos's packer takes, made afresh in BUILD/app: app/app.txt
  # and bin/doom. The packer plans bin/doom as the loader will and never runs it.
  def package
    program = link.program.fetch(:program)
    app = File.join(BUILD, 'app')
    FileUtils.rm_rf(app)
    FileUtils.mkdir_p(File.join(app, 'bin'))
    FileUtils.cp(File.join(HERE, 'app', 'app.txt'), app)
    FileUtils.cp(program, File.join(app, 'bin', 'doom'))
    app
  end

  def build
    linked = link
    runner, platform, machine, fiasco, runtime, archives, program =
      linked.to_h.values_at(:runner, :platform, :machine, :fiasco, :runtime, :archives, :program)
    codec = Erx::ABI::Codec.new(Erx::ABI::Generator.plan(root: ROOT).schema)
    # BOUNDARY GAP 5: the program joins the one domain release's bin/, which
    # every session shares, rather than an app's own place.
    entry = { 'path' => "#{Erx::ReleaseFiles::DOMAIN_PREFIX}bin/doom", 'kind' => codec.member('entry_kind', 'file'),
              'flags' => 0, 'id' => 'program-doom', 'artifact' => program.fetch(:program),
              'receipt' => program.fetch(:receipt) }.freeze
    library = Erx::ProductionImage::Archive.new(path: archives.first, label: 'doomgeneric libdoom.a',
                                                license: 'GPL-2.0-only', status: 'stopgap', notices: [])
    added = Erx::ProductionImage::Added.new(entries: [entry].freeze, inputs: program.fetch(:inputs).uniq.freeze,
                                            imports: {}.freeze, archives: [library].freeze)
    source = manifest(platform.manifest_source(ROOT))
    directory = Erx::ProductionImage.clear_directory(root: ROOT, path: File.join(BUILD, 'image'))
    built = Erx::ProductionImage.new(root: ROOT, runner: runner, compiler: COMPILER, linker: LINKER, machine: machine,
                                     fiasco: fiasco, runtime: runtime)
                                .build_test_image(directory: directory, added: added, machine: machine,
                                                  manifest_source: source)
    [runner, machine, source, built]
  end

  # The boot's play through QEMU's keyboard: once Doom's title shows, Escape
  # opens the menu over it and three Enters pick New Game, the first episode and
  # the default skill, all within the title's 170 tics (4.9 s): when the title
  # page ends, Doom starts its next demo and drops a game the menu asked for in
  # the same tic. In the game, with no key down, the view stands still;
  # holding Left turns it. Each step's screendump is saved as
  # OUTDIR/doom-NAME.ppm and each step's change of the view in OUTDIR/play.txt.
  TITLE_WAIT = 120      # seconds from the shell's prompt to wait for Doom's title
  MENU_STEP = 0.4       # seconds between the menu's keys
  LIT_MIN = 0.3         # the share of the view that is not black once Doom draws
  MENU_MIN = 0.01       # the share of the view the menu must change, at least
  STILL_MAX = 0.02      # the share of the view two still screens may differ in
  TURNED_MIN = 0.25     # the share of the view a turn must change, at least
  BLACK = "\0\0\0".b.freeze

  # Doom's view on the screen, as DG_Init places Doom's frame (doomgeneric's
  # 640x400, at the largest whole scale that fits, centred): the frame's top
  # 168 of 200 rows, above the status bar, whose face moves on its own.
  View = Data.define(:rows, :columns) do
    def self.of(image)
      scale = [image.width / 640, image.height / 400].min
      left = (image.width - (640 * scale)) / 2
      top = (image.height - (400 * scale)) / 2
      new(rows: (top...(top + (400 * scale * 168 / 200))).step(4).to_a.freeze,
          columns: (left...(left + (640 * scale))).step(4).to_a.freeze)
    end

    # The view's pixels in image, sampled every fourth on every fourth row.
    def samples(image)
      rows.flat_map do |y|
        row = image.row(y)
        columns.map { |x| row.byteslice(x * 3, 3) }
      end
    end
  end

  Key = Data.define(:client) do
    def press(qcode, hold: 0.1)
      [true, false].each do |down|
        client.input_events([{ 'type' => 'key',
                               'data' => { 'down' => down, 'key' => { 'type' => 'qcode', 'data' => qcode } } }])
        sleep hold if down
      end
    end
  end

  # The share of the view's samples in which two screendumps differ.
  def view_change(one, other)
    view = View.of(one)
    view.samples(one).zip(view.samples(other)).count { |a, b| a != b }.fdiv(view.rows.size * view.columns.size)
  end

  def lit(image)
    samples = View.of(image).samples(image)
    samples.count { |pixel| pixel != BLACK }.fdiv(samples.size)
  end

  # The first screendump on which Doom has drawn, taken every half second until
  # TITLE_WAIT has passed.
  def wait_title(shot)
    deadline = Erx::Qmp.monotonic + TITLE_WAIT
    loop do
      image = shot.('title')
      return image if lit(image) >= LIT_MIN
      raise Erx::Error, "Doom drew nothing in #{TITLE_WAIT} s" if Erx::Qmp.monotonic > deadline

      sleep 0.5
    end
  end

  def play(socket, dir, out)
    client = Erx::Qmp.connect(socket)
    key = Key.new(client: client)
    shot = lambda do |name|
      client.screendump_image(File.join(dir, "#{name}.ppm")).tap do |image|
        File.binwrite(File.join(out, "doom-#{name}.ppm"), image.to_ppm)
      end
    end
    title = wait_title(shot)
    key.press('esc')
    sleep MENU_STEP
    menu = shot.('menu')
    %w[episodes skills game].each do |name|
      key.press('ret')
      sleep MENU_STEP
      shot.(name)
    end
    sleep 4
    still = shot.('still')
    sleep 2
    still_again = shot.('still-again')
    key.press('left', hold: 1.0)
    sleep 1
    turned = shot.('turned')
    changes = { menu: view_change(title, menu), still: view_change(still, still_again),
                turned: view_change(still_again, turned) }
    File.write(File.join(out, 'play.txt'), changes.map { |step, share| format("%s %.4f\n", step, share) }.join)
    raise Erx::Error, "the menu changed only #{changes[:menu]} of the view" if changes[:menu] < MENU_MIN
    raise Erx::Error, "the view changed in #{changes[:still]} of its pixels while still" if changes[:still] > STILL_MAX
    raise Erx::Error, "the turn changed only #{changes[:turned]} of the view" if changes[:turned] < TURNED_MIN
  ensure
    client&.close
  end

  def boot(out)
    runner, machine, source, built = build
    FileUtils.mkdir_p(out)
    path = Erx::ProductionImage.clear_directory(root: ROOT, path: File.join(BUILD, 'boot'))
    dir = Erx::OutputDirectory.prepare(root: ROOT, path: path, mode: 0o700)
    socket = File.join(dir, 'qmp.sock')
    display = Erx::QemuPlan::Display.new(qmp_socket: socket)
    points = Erx::ProductionImage::Run.checkpoints(built, root: ROOT, watchdog: Erx::MediumBoot::Run::WATCHDOG, cpus: 1,
                                                          manifest_source: source, memory_mib: machine.memory_mib,
                                                          test_image: true)
    shell = points.last
    checkpoints = [*points[0...-1],
                   shell.merge(seconds: shell.fetch(:seconds) + 120, verify: ->(_text) { play(socket, dir, out) }).freeze]
    Erx::Qemu.new(root: ROOT, runner: runner, program: 'qemu-system-aarch64')
             .run(platform: machine.platform, accel: Erx::QemuCells::LOCAL_ACCELERATOR, cpus: 1, remote: nil,
                  image: built.single, memory_mib: machine.memory_mib, directory: dir, display: display,
                  checkpoints: checkpoints)
  ensure
    serial = dir && File.join(dir, 'serial.log')
    FileUtils.cp(serial, File.join(out, 'serial.log')) if serial && File.exist?(serial)
  end
end

case ARGV
in ['build'] then ErxDoom.build
in ['package'] then puts ErxDoom.package
in ['libdoom'] then puts ErxDoom.libdoom
in ['launch'] then puts ErxDoom::LAUNCH
in ['test'] then exit(ErxDoom.test_keys)
in ['boot', out] then ErxDoom.boot(File.expand_path(out))
end
