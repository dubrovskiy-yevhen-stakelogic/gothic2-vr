# SteamVR object-upload synchronization

The reported outdoor flashes disappeared after synchronizing object uploads before early GPU submission. The confirmed SteamVR test used the standard cropped HUD and desktop mirror.

`InstanceStorage::commit` schedules a worker to copy object patches into a GPU-visible buffer. The preparation command buffer reads those patches for visibility and shadow rendering. Previously, `onPrepared` could submit that command buffer before the end-of-world `WorldView::postFrameupdate` joined the worker. The GPU could therefore read stale or partially written object data.

The renderer now joins the pending upload immediately before the early preparation submission. The ordinary end-of-world join remains for the main submission and unsplit rendering. `tests/split-upload.py` exercises the production submission boundary with a blocked upload worker, including the unsplit and no-callback paths.

The desktop mirror samples the rendered left eye; it does not render the world a third time. It adds a downscale pass, a copy path and Windows presentation. Its performance cost depends on the system. Diagnostic environment overrides for mirror and HUD comparisons remain opt-in; neither is required for the synchronization fix.
