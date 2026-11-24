# OBS Companion 0.7.9: countdown and Discord streams

Baseline: attached BH-OBS-Companion 0.7.6 complete project and Bacons Helper Twitch complete project dated 2026-11-16.

## Installation

The FULL-PROJECTS archive contains both complete source projects. The UPDATED-FILES archive contains only new/changed files, under the same two project directories. Merge each directory into its matching project; do not put one project inside the other.

1. Apply the Bacons-Helper-Twitch API changes and restart both the bot and website processes. Existing countdown/Discord services and their database tables are reused; no additional application dependency is needed.
2. Build/package BH-OBS-Companion using its existing platform build scripts or GitHub Actions, then install the built plugin and restart OBS. These archives contain source, not compiled plugin installers.
3. Pair as usual or keep the existing pairing, then click Refresh in the dock.
4. In Discord Streams, use Connect Discord / Choose Destination for the dashboard's existing Discord account and server/channel authorization flow. Return to OBS and Refresh after saving the destination.

## Countdown

The Countdown tab now provides remaining-time announcements, Automatic or 5–3600-second intervals, final five-second count, completion announcements, and command-start announcements. Set Automatic to 0. Values 1–4 are rejected before starting/saving.

Start Countdown and the preset buttons apply the visible choices to that run. Save Chat Defaults persists them to the existing channel_countdown_settings store for future runs from every entry point. Changing defaults never modifies an active timer. The command-start setting affects the Twitch command acknowledgment; starting from OBS retains its existing behavior. Disable remaining-time announcements to suppress both periodic and final-five updates; disable completion too for no countdown chat output from OBS. Overlay timing remains active.

## Discord Streams

Controls cover own/watched announcements, silent delivery for your stream, title/category updates, end-of-stream deletion, and periodic refresh (Off, 10, 15, 30, 60 minutes). Add/remove watched Twitch streamers and choose Send Silently or Enable Notifications for each. Silent embeds still post and update; Discord push/desktop notifications are suppressed at creation. Delivery and edit timing use the existing bot service, including the ten-minute minimum between successful embed edits.

Settings and watch entries persist through the same database-backed stores as the dashboard. The dock can modify the paired owner's existing destination behavior. Connecting Discord or changing destinations remains in the dashboard, which verifies the Discord account's server permissions. The dock never accepts destination IDs or another Twitch channel ID from the client.

Refresh replaces unsaved panel edits with the saved settings. Watch-list actions refresh Discord data without resetting countdown choices. Existing servers without the API update retain their original countdown controls and show update-required guidance for the new panels.

## Validation and limits

148 focused Node tests pass across countdown behavior/recovery, the new OBS endpoints, pairing and input validation, Discord persistence/watch lists/delivery, and dashboard regressions. Changed JavaScript passes syntax checks. ZIP entries were compared with both supplied baselines to verify the updated-files package includes every changed/new project file.

Qt/OBS development dependencies are unavailable in this environment. The native plugin was not compiled or run, and live Twitch/Discord delivery was not exercised. Use the existing platform build workflow and verify both panels in OBS before deploying broadly.

## Display names (0.7.8)

Discord Streams resolves watched streamers to their current Twitch display names, and shows the destination as Server Name / #channel-name. IDs remain unchanged internally for selection, requests and persistence. If a Twitch profile lookup fails, the dock uses a stored readable name or login, then Unknown Streamer. If Discord destination names are unavailable, it shows a name-unavailable label instead of numeric IDs. Existing settings remain usable.

Discord destination names are cached for five minutes to reduce repeated requests, with one-minute retry delays after failed/partial lookups. A Discord rename can therefore take up to five minutes plus a dock Refresh to appear. Apply both the API files and the rebuilt Companion; restart the website and OBS, then Refresh.

The updated-files archive for this release is relative to the previously supplied 0.7.8 full-projects archive. The full-projects archive includes all prior countdown and Discord controls.

## Command History restoration (0.7.9)

The 0.7.7/0.7.8 combined backend archives omitted the earlier Command History changes even though the Companion retained its history panel. This release restores the dashboard tab, actual delivered output capture, and the paired OBS API endpoint. History shows time, user, command/alias and recorded output, with Latest/Older pages of 50 records. Command History is in the Companion's section dropdown before Event History, and has an additional shortcut under More.

Apply the backend files and restart BOTH the bot and website processes. Build/install the Companion and restart OBS for the new More shortcut. Refresh the dock, then select Command History. Refresh the web dashboard for its restored tab. Existing command_log records are reused; no database migration is required. Past rows that never recorded output cannot have their output recovered retroactively; newly delivered command replies are recorded after the bot restarts. Stored output is limited to the earlier implementation's 490-character preview.

Whisper runs are excluded from new history logging. The bot account's legacy history is restricted to rows marked as public channel commands. OBS history is scoped exclusively to the paired channel; request parameters cannot select another channel. Database failures return errors rather than a successful empty result.

148 focused tests pass, including command output capture/concurrency, scoped dashboard and OBS history, cursor validation and pagination, pairing authentication, whisper exclusions, and the existing countdown/Discord/shared-chat/remove-bot-now regressions. Native compilation, live OBS/database checks and rendering the restored Pug page were not performed in this environment.
