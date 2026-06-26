/* Sonic Loader — persistent settings.

   Loads /data/sonic-loader/config.ini at boot and writes back whenever
   a setting is changed via the web UI. Format is line-oriented
   key=value text:

       kstuff_auto_toggle=1
       cheats_engine=1
       backpork=0
       pause_seconds=25
       resume_seconds=10
       fan_threshold=60

   Booleans are 0/1. Missing keys keep their compile-time defaults.
*/

#pragma once

/* Load config file (creates the directory if missing) and apply each
   value to the relevant subsystem. Safe to call multiple times. */
void config_load(void);

/* Snapshot the current state of every subsystem and write it out to
   disk. Call this from setters whenever the user changes something. */
void config_save(void);

/* Boot-order helper. Subsystem setters call config_save() unconditionally
   when the user pokes them, but during early boot we deliberately call
   those setters with compile-time defaults BEFORE config_load runs —
   if those calls were allowed to write, they'd overwrite the user's
   persisted file with defaults and we'd never see the real values
   when config_load opens the file a moment later. config_save_set_inhibit(1)
   makes config_save a no-op until cleared. config_load clears the
   inhibit on its way out. */
void config_save_set_inhibit(int on);
