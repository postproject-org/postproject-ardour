# Ardour per-target brief

Checked on 2026-09-29 against Ardour `master` commit
`7968ec504ba8b70e6de5c09d0470264581a5e979` (committed 2026-09-29).
Ardour 9.8, released 2026-08-23, is the current stable series. The current
tree is active and includes changes from the day of this review. Line numbers
below refer to the checked commit.

Provisional decision: **go for a resolver pilot if the post-Kdenlive/Blender
evidence review still needs pkg-config, an exception-enabled C++ host, or an
audio application. Do not use Ardour peak files as PostProject managed jobs.**

## Pilot outcome

The evidence review selected Ardour for all three named gaps, so the resolver
pilot proceeded. The maintained patch series now:

- discovers the unmodified installed `postproject.pc` optionally and has an
  explicit `--no-postproject` upstream-only build;
- records regular audio file sources after session saves under revision origin
  `org.ardour`, using the persisted source ID as
  `org.ardour:source_id`;
- passes that source ID through Ardour's synchronous missing-file signal and
  accepts an exact absolute replacement path, which permits a renamed file;
- searches Ardour's audio paths by content, accepts exactly one
  `resolved_exact` candidate, and falls through to the existing dialog for no
  match, ambiguity, or any exception.

The source ID was necessary because PostProject deliberately converts only an
existing native path to a canonical locator. Reconstructing a missing file URI
inside Ardour would have duplicated platform-sensitive locator semantics. No
Ardour session XML extension was added.

The installed-package scenario passes locally with a one-second, 48 kHz stereo
WAV and verifies rename recovery, duplicate ambiguity, revision origin,
external identity, and typed C++ exception handling. The host adapter also
compiles against the pinned Ardour headers locally. The complete patched host
build (with and without PostProject) and the macOS installed-package scenario
are encoded in CI; they are not presented here as remotely run results.

Interactive Ardour UI verification has not been performed. The synchronous
fingerprinting/search cost and the environment-variable production selector
remain pilot limitations. Peak files remain an explicit no-go for managed-job
semantics.

## Current behavior

**Source identity and location.** Every `ARDOUR::Source` has a `PBD::ID` and
writes it as the `id` property of its XML state (`libs/ardour/source.cc:120`).
An audio file source also persists its source path; regions refer to source IDs,
so edits do not rely on a file name as object identity. `FileSource::find()`
searches the session's audio or MIDI source path for a relative source name
(`libs/ardour/file_source.cc:230`). It de-duplicates equivalent paths, calls
`FileSource::AmbiguousFileName` when several files have the name, and accepts
the sole matching path without comparing content (`file_source.cc:270`–`303`).
Absolute paths are used directly.

The search path contains the session media directories and the explicit
`audio-search-path` or `midi-search-path` setting
(`libs/ardour/session.cc:7160`–`7206`). The Missing File dialog lets the user
add one directory to that setting or provide an absolute replacement
(`gtk2_ardour/missing_file_dialog.cc:137`–`190`). It has no content identity and
does not recursively discover renamed material.

**Clean recovery seam.** Session loading catches a missing source and invokes
the process-wide synchronous `Session::MissingFile` signal
(`libs/ardour/session_state.cc:3467`). The GTK application connects that signal
to `ARDOUR_UI::missing_file()` and separately connects
`FileSource::AmbiguousFileName` to an ambiguity dialog
(`gtk2_ardour/ardour_ui.cc:452`–`456`,
`gtk2_ardour/ardour_ui_startup.cc:127`–`155`). The callback can return a
replacement/search-path decision before loading continues. That is a narrow
adapter seam: query PostProject first, then preserve the existing dialog for
zero or multiple matches. It requires no change to the session XML format.

**Peak files.** Audio waveforms are private `.peak` caches. Missing peaks are
queued by `SourceFactory::setup_peakfile()` and built by two internal threads
(`libs/ardour/source_factory.cc:66`–`150`). `AudioSource::PeaksReady` announces
successful availability, while the UI polls only the total queued/active count
(`libs/ardour/ardour/audiosource.h:68`,
`gtk2_ardour/ardour_ui.cc:1336`). There is no per-item claim token, progress,
failure terminal, or cancellation seam. Peak creation may also occur lazily
from a read. These files are regenerable display caches, but mapping this
lifecycle to PostProject jobs would require invasive changes and would add no
useful cross-application representation. They are not a managed-artifact
candidate.

**Converted and consolidated audio.** Import wraps incompatible sources in
`ResampledSource` and writes session audio through the import path
(`libs/ardour/import.cc:94`–`133`). Bounce and consolidate create ordinary
audio sources and a whole-file region tagged `(bounce)`
(`libs/ardour/session.cc:6391`–`6720`). Freeze likewise creates session-owned
audio while preserving a private freeze record. These outputs can be meaningful
media, unlike peak caches, but their UI operations and recipes are broader than
the smallest resolver experiment. A later pilot could record a completed
bounce as observed derived media; it should not infer a reproducible recipe
from the resulting file alone.

**Build, errors, and platforms.** Ardour uses Waf/autowaf. External libraries
are discovered with `autowaf.check_pkg`, for example libsndfile, libcurl, and
libarchive in the top-level `wscript:1181`–`1193`. The unmodified installed
`postproject.pc` is sufficient for the same mechanism:
`autowaf.check_pkg(conf, 'postproject', uselib_store='POSTPROJECT',
mandatory=False)`. It supplies the installed include directory and
`-lpostproject`; no Cargo invocation or custom finder is needed.

The configure step verifies C++17 support (`wscript:579`) and offers the C++17
compiler flag. Ardour uses C++ exceptions normally: constructors and file
operations throw and catch `failed_constructor`, `Glib::FileError`, and other
typed errors; the tree does not disable exceptions globally. The PostProject
C++ result wrapper therefore fits its normal error style, with errors converted
to Ardour logging and the existing fallback dialog at the adapter boundary.

The maintained source has Linux, macOS, MinGW, and MSVC build paths. Official
releases support Linux, macOS, and Windows. The repository contains Linux and
macOS packaging plus `tools/x-win/` for cross-building Windows. A Linux pilot
build is practical. macOS is the least disruptive second CI host because it
uses the same Waf build and exception model; Windows is feasible but its
cross-build and packaging toolchain make it a larger acceptance gate.

## User pain

A renamed source is not found because Ardour searches by its stored name. A
moved source is found only after the user adds the right directory. Two files
with the same name require a manual ambiguity decision with no content evidence,
and the decision is local to one session. A production with the same recording
used in a picture editor cannot share a confirmed move with Ardour.

## Smallest PostProject experiment

Add an optional adapter at the existing missing-source callback. On session
save, it records each external audio source in an explicitly selected
production with:

- the Ardour source ID as an application identifier qualified
  `org.ardour:source_id`;
- its file representation, content observation, and confirmed locator;
- revision origin `org.ardour`.

On a missing audio source, the callback looks up the recorded media and asks
PostProject to resolve it in the session folder, Ardour audio search paths, and
the old source folder. Exactly one content-confirmed candidate is offered as
the absolute replacement. No match, ambiguity, any PostProject error, or an
absent production falls through to Ardour's current Missing File dialog. MIDI,
Freesound, silent, playlist, and non-file sources retain upstream behavior.

Adoption is explicit when the chosen production already knows the file through
another host. The adapter attaches Ardour's identifier to the existing asset;
it does not merge assets by content. Search directories are not stored as
locations.

## Files and modules that would change

The pilot would be a small patch series against Ardour:

- top-level `wscript` and `gtk2_ardour/wscript` for an optional pkg-config
  dependency and adapter source;
- `gtk2_ardour/ardour_ui.cc`, `ardour_ui_startup.cc`, and the missing-file
  signal to identify and resolve the source ahead of the existing dialog;
- `gtk2_ardour/postproject_adapter.{h,cc}` for the Ardour boundary and
  `postproject_resolver.{h,cc}` for the independently executable policy;
- the existing `StateSaved` signal to record file sources;
- a focused temporary-production scenario compiled through `postproject.pc`.

No Ardour XML element, source class, peak builder, import engine, or DSP path
changes.

## Integration route and cost

This pilot exercises the installed `postproject.pc`, the C++17 wrapper in an
exception-enabled application, and audio-file recovery. The optional runtime
cost is the PostProject shared library, including SQLite, plus one local
`.pproj` file. No Rust compiler, service, background daemon, or network access
is required. Configuring without PostProject compiles the upstream path only.

## Removal

Build without the optional pkg-config dependency or remove the adapter patch.
Delete the selected `.pproj` file if its production knowledge is unwanted.
Ardour session files remain valid and need no migration; the stored source IDs
already belong to Ardour.

## Success and evidence

Success is an installed-package build that relinks a renamed external audio
file by content, never selects among duplicate candidates, preserves the
existing dialog on every fallback, and passes on Linux plus macOS. Its CI trace
would add evidence not supplied by the maintained pilots: pkg-config package
discovery, normal exception-enabled C++ use, an audio application, and a second
desktop platform if macOS is run.

The pilot is a no-go if the Phase 4.5 review finds those routes already covered,
if invoking the local database from the synchronous load callback blocks
Ardour's supported session-recovery flow, or if the adapter cannot remain
entirely optional in official-style Waf builds. Peak/job support is separately
a no-go unless Ardour first exposes per-output start, progress or renewal,
success, failure, and cancellation events.
