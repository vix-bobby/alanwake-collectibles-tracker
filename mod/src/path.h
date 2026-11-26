/* Paths are kept in UTF-8 and handed to Windows in UTF-16, so that a folder name does not have
 * to fit the system's code page. */
#ifndef PATH_H
#define PATH_H

#include <windows.h>

#define PATH_WIDE (MAX_PATH * 2)  /* a path in UTF-16 */
#define PATH_UTF8 (PATH_WIDE * 3) /* the same path in UTF-8, at most */

/* Both return 0, and give "", if the text does not fit. */
static inline int to_wide(const char *text, wchar_t *out, int count)
{
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, out, count))
        return 1;
    out[0] = L'\0';
    return 0;
}

static inline int to_utf8(const wchar_t *text, char *out, int size)
{
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, out, size, NULL, NULL))
        return 1;
    out[0] = '\0';
    return 0;
}

static inline DWORD path_attributes(const char *path)
{
    wchar_t wide[PATH_WIDE];

    return to_wide(path, wide, PATH_WIDE) ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
}

static inline int path_info(const char *path, WIN32_FILE_ATTRIBUTE_DATA *info)
{
    wchar_t wide[PATH_WIDE];

    return to_wide(path, wide, PATH_WIDE)
           && GetFileAttributesExW(wide, GetFileExInfoStandard, info);
}

/* Open for reading without getting in the way of the game writing the file. */
static inline HANDLE path_open_read(const char *path)
{
    wchar_t wide[PATH_WIDE];

    if (!to_wide(path, wide, PATH_WIDE))
        return INVALID_HANDLE_VALUE;
    return CreateFileW(wide, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       0, NULL);
}

#endif
