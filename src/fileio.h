#ifndef FILEIO_H
#define FILEIO_H

#include "grid.h"

/* Save spreadsheet to a .ss file. Prompts user for filename. */
void file_save(Spreadsheet *sheet);

/* Load spreadsheet from a .ss file. Prompts user for filename. */
void file_load(Spreadsheet *sheet);

/* Load spreadsheet from a given file path (no prompt).
 * Returns true on success, false on failure. */
bool file_load_path(Spreadsheet *sheet, const char *path);

#endif /* FILEIO_H */
