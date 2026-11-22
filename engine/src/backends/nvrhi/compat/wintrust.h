/*
 * Minimal <wintrust.h> for the MinGW cross-build.
 *
 * Ubuntu's mingw-w64 headers (unlike current mingw-w64 and the Windows SDK)
 * describe the pre-Windows-8 WINTRUST_DATA and therefore lack the
 * WINTRUST_SIGNATURE_SETTINGS / pSignatureSettings members that Streamline's
 * sl_security.h uses to verify NVIDIA's secondary signature.
 *
 * Only the declarations sl_security.h needs are provided here. The layouts
 * match the public Windows SDK definitions (pSignatureSettings was appended
 * after dwUIContext, so the older prefix stays compatible). On MSVC the real
 * SDK header is used instead (the compatibility directory is only added for
 * MinGW builds).
 */
#ifndef WINTRUST_H
#define WINTRUST_H

#include <wincrypt.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <pshpack8.h>

#define WTD_UI_ALL 1
#define WTD_UI_NONE 2
#define WTD_UI_NOBAD 3
#define WTD_UI_NOGOOD 4

#define WTD_REVOKE_NONE 0x00000000
#define WTD_REVOKE_WHOLECHAIN 0x00000001

#define WTD_CHOICE_FILE 1
#define WTD_CHOICE_CATALOG 2
#define WTD_CHOICE_BLOB 3
#define WTD_CHOICE_SIGNER 4
#define WTD_CHOICE_CERT 5
#define WTD_CHOICE_DETACHED_SIG 6

#define WTD_STATEACTION_IGNORE 0x00000000
#define WTD_STATEACTION_VERIFY 0x00000001
#define WTD_STATEACTION_CLOSE 0x00000002
#define WTD_STATEACTION_AUTO_CACHE 0x00000003
#define WTD_STATEACTION_AUTO_CACHE_FLUSH 0x00000004

#define WTD_SAFER_FLAG 0x00000100

typedef struct WINTRUST_SIGNATURE_SETTINGS_ {
    DWORD cbStruct;
    DWORD dwIndex;
    DWORD dwFlags;
    DWORD cSecondarySigs;
    DWORD dwVerifiedSigIndex;
    PCERT_STRONG_SIGN_PARA pCryptoPolicy;
} WINTRUST_SIGNATURE_SETTINGS, *PWINTRUST_SIGNATURE_SETTINGS;

#define WSS_VERIFY_SPECIFIC 0x00000001
#define WSS_GET_SECONDARY_SIG_COUNT 0x00000002

typedef struct WINTRUST_FILE_INFO_ {
    DWORD cbStruct;
    LPCWSTR pcwszFilePath;
    HANDLE hFile;
    GUID* pgKnownSubject;
} WINTRUST_FILE_INFO, *PWINTRUST_FILE_INFO;

typedef struct _WINTRUST_DATA {
    DWORD cbStruct;
    LPVOID pPolicyCallbackData;
    LPVOID pSIPClientData;
    DWORD dwUIChoice;
    DWORD fdwRevocationChecks;
    DWORD dwUnionChoice;
    union {
        struct WINTRUST_FILE_INFO_* pFile;
        struct WINTRUST_CATALOG_INFO_* pCatalog;
        struct WINTRUST_BLOB_INFO_* pBlob;
        struct WINTRUST_SGNR_INFO_* pSgnr;
        struct WINTRUST_CERT_INFO_* pCert;
        struct WINTRUST_DETACHED_SIG_INFO_* pDetachedSig;
    };
    DWORD dwStateAction;
    HANDLE hWVTStateData;
    WCHAR* pwszURLReference;
    DWORD dwProvFlags;
    DWORD dwUIContext;
    struct WINTRUST_SIGNATURE_SETTINGS_* pSignatureSettings;
} WINTRUST_DATA, *PWINTRUST_DATA;

#include <poppack.h>

extern LONG WINAPI WinVerifyTrust(HWND hwnd, GUID* pgActionID, LPVOID pWVTData);

#ifdef __cplusplus
}
#endif

#endif // WINTRUST_H
