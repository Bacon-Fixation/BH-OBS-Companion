# Native model tuning - 0.7.0 Proto

All detection, import/export and corrections run in C++ using the existing Qt dependency. Actual translations still go through Bacons Helper.

## Reduce false triggers first
In Translation, enable the local detector and stay in preview mode. The cautious preset uses a 95% minimum score, six useful words, a 15 percentage-point gap between the top two candidates, and exact reliable-language decisions instead of related-language cluster decisions. Adjust these controls against representative chat. Stricter settings skip more legitimate messages as well as false triggers. Defaults preserve the previous 85%, four-word policy.

Scores describe model evidence; they are not calibrated probabilities of correctness. Changing a threshold does not train the model or make its scores statistically calibrated. There is no claimed measured reduction in production false triggers in this release. (In other words, this release is a proof of concept that detection can be done locally and then translated through bacons helper)

## Correct a detection
1. Select a history row, or enter a sentence in the local tester. Set its preferred/target language and enter the correct language code (for example `en`, `es`, `de`).
2. Choose **Preview correction**. Text must contain at least four useful words and ten letters, and use an existing model language.
3. Inspect the before/after language and score, plus improvement, regression and false-trigger counts. Apply is blocked if the candidate introduces a new false trigger on a saved preferred-language example or changes a previously correct reviewed detection to an incorrect one.
4. Choose **Apply previewed correction** to save. Model operations turn automatic translation off; evaluate more examples before re-enabling it.

Each correction performs a small incremental update: blend 8% of the example's normalized weighted n-gram vector into the corrected language profile, then normalize it. This tunes the real model weights; it is not full corpus retraining and cannot add new languages. Other profiles are unchanged. Scores can go down as well as up.

Up to 200 labeled review examples are retained locally. Preview evaluates those examples plus the new correction using the default trigger policy, not your stricter live settings. These are training/review examples, not an independent held-out accuracy estimate. Representative unseen messages still need evaluation. An unchanged review result does not guarantee safe behavior on other chat.

For a repeated phrase that should never request translation, choose **Ignore this normalized text**. This suppresses an exact normalized phrase, not a substring or an entire language. Up to 1,000 phrase hashes are supported.

## Import, export and recovery
**Export active model** creates a portable JSON package containing model weights, metadata and ignored phrase hashes. **Import model** validates this native package before replacing the active model. Arbitrary model formats and raw upstream model.json files are not supported. Imports clear saved review examples; run local tests before enabling automation. Maximum import size is 40 MiB.

**Undo last model change** restores one previous model and its review examples. **Restore bundled model** resets to the shipped model and clears current review examples. A previous snapshot remains available for undo. Preferences such as the threshold are saved separately and are not imported/exported with a model.

The custom model is saved beside the Companion settings as `language-model-custom.json`, with one `.previous` snapshot. These local files include raw reviewed messages; exports deliberately exclude those messages. Normalized ignored phrases are stored as SHA-256 hashes, which are not a guarantee of secrecy for guessable text. Review only messages you are comfortable retaining on that computer. To remove retained review text completely, close OBS and delete both custom-model files; the next session loads the bundled model.

Invalid imports leave the active model unchanged. Writes are atomic; no partially written candidate becomes active. If saved custom data cannot load, use Import or Restore bundled model and re-enable detection. Export to a separate file, outside active storage.

## Build and validation
Use the existing native build workflow. The source bundle is not a precompiled Windows DLL. Standalone C++/Qt tests cover reference parity, worker lifecycle and model transactions. Production OBS loading, UI layout and live Twitch operation still need platform validation.
