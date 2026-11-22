/*
 * Minimally compatible <Softpub.h> for MinGW.
 *
 * The Windows SDK header (and the one MSYS2 ships) provides the Authenticode
 * policy GUIDs. The mingw-w64 package used by the Linux cross-compile CI does
 * not, but Streamline's sl_security.h only needs
 * WINTRUST_ACTION_GENERIC_VERIFY_V2. Keeping the same value here lets the SDK
 * header compile unchanged.
 */
#ifndef CUBE_MINGW_SOFTPUB_H
#define CUBE_MINGW_SOFTPUB_H

#include <wintrust.h>

#ifndef WINTRUST_ACTION_GENERIC_VERIFY_V2
#define WINTRUST_ACTION_GENERIC_VERIFY_V2 \
    { 0x00aac56b, 0xcd44, 0x11d0, { 0x8c, 0xc2, 0x00, 0xc0, 0x4f, 0xc2, 0x95, 0xee } }
#endif

#endif // CUBE_MINGW_SOFTPUB_H
