# Contributing

Contributions are welcome when they improve the template for authorized research, debugging, stability, or developer ergonomics.

## Guidelines

- Keep scope explicit and avoid global-hook defaults.
- Preserve fail-closed behavior in target processes.
- Keep release logging quiet by default.
- Do not add secrets, private app code, or proprietary offsets/signatures.
- Run `python3 -m unittest discover -s tests -v` for configure, ELF tool, Java contract,
  native scanner and documentation checks. See `tests/README.md` for requirements.
- Run `./gradlew :app:assembleDebug :app:assembleRelease :app:lintDebug :app:lintRelease
  -Ptemplate.localTestSigning=true` for local QA, without production signing variables.
- Keep real engine/ABI compatibility claims tied to device evidence; fixture success is not proof
  that an arbitrary protected or stripped runtime supports the same contracts.

## License note

This repository is licensed under CC BY-NC-ND 4.0. Pull requests may be accepted only when the maintainer can incorporate them under the same license terms.
