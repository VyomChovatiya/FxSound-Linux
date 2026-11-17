# Third-party material

FxSound application resources and factory presets originate from the supplied
FxSound 1.1.16.0 Windows installation. Bonus presets originate from the supplied
BonusPresets.zip. The official source project is
https://github.com/fxsound2/fxsound-app and identifies AGPL-3.0 licensing.

The extracted resources have not yet been individually matched to an upstream
revision. This provenance check must be completed before publishing a public
release containing them; the project's code license does not independently
establish redistribution rights for every asset.

Windows executables, drivers, installer helpers, extracted Gilroy fonts, and
local diagnostic captures are excluded from the repository upload. The UI uses
fallback fonts when the optional extracted fonts are absent.

Qt and libpulse are external build/runtime dependencies and retain their own
licenses. They are not vendored here.
