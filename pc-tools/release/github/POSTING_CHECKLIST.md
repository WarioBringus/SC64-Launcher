# Posting SC64 Launcher v1.0 to GitHub: checklist

## 1. Before anything is pushed

- [ ] **Your email in the commits.** 284 of the launcher's commits record your personal email address as the author.
      Pushing makes that public. GitHub gives every account a private "noreply" address
      (Settings > Emails > "Keep my email addresses private"). The commits can be rewritten to use it **before**
      pushing (safe now, because nothing is published yet), and future commits set to use it too.
- [ ] **Personal paths in developer tools.** `coverpack.py` and `launchbox.py` default to `C:/n64/...` and `I:`
      (your PC). They're harmless (they reveal nothing about you), but they can be made generic. The release tools
      don't depend on them.
- [ ] **Build a clean release folder**, without your downloads in `tools\data` (about 65 MB of cache from your tests),
      then zip it as `SC64-Launcher-v1.0.zip`. Check the zip has: `SD card\` (with the empty game folders),
      the two `.bat` files, `tools\` (bin, extras, python, scripts), `README.txt` and `LICENSE.md`.
- [ ] **Don't upload**: ROMs, your SD card backup, `tools\data`, the demo ROM (it holds box art and the start of each
      game file), or the tour screenshots.

## 2. On GitHub

- [ ] **Fork** N64FlashcartMenu (https://github.com/n64brew/N64FlashcartMenu, "Fork" button). A fork keeps the link to the
      original and its history, which credits its authors and meets the AGPL's "source available" rule.
- [ ] **Push the `launcher` branch** to your fork and make it the fork's default branch
      (fork Settings > General > Default branch).
- [ ] **README**: replace the root `README.md` with `pc-tools/release/github/README.md` (keep the original as
      `README.upstream.md` so its information isn't lost).
- [ ] **Tag the release**: `v1.0.0` on the commit you built from.
- [ ] **Create the Release** (Releases > Draft a new release): tag `v1.0.0`, title "SC64 Launcher v1.0", paste
      `RELEASE_NOTES.md` as the description, attach `SC64-Launcher-v1.0.zip`.
- [ ] **Screenshots or a video** for the README (the demo ROM in ares is ideal for recording).
- [ ] Optional: **About** box (top right of the repo): description, topics `n64`, `summercart64`, `analogue-3d`,
      `flashcart`, `frontend`.
- [ ] Optional: **Issues** turned on, with a bug-report template asking for the build number, console and firmware.

## 3. After posting

- [ ] Download the release zip on another folder (or PC) and do one install from it, exactly as a new user would.
- [ ] Keep the SD card backup until you're happy with the release.

## Licence notes (not legal advice)

- AGPL-3.0: the source is public on your fork; keep `LICENSE.md` and the credits; anyone who receives the launcher can
  get its source (the fork link in the README covers that).
- The README says what was changed (the launcher and PC tools), and the commit history shows every change.
- Box art and game names belong to their owners; the README's disclaimers say so. The extra art in `tools\extras` is
  included in the download, credited to LaunchBox and libretro-thumbnails.
