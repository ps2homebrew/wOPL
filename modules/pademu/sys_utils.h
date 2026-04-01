/*
   Copyright 2006-2008, Romz
   Copyright 2010, Polo
   Licenced under Academic Free License version 3.0
   Review OpenUsbLd README & LICENSE files for further details.
   */

#ifndef __SYS_UTILS_H
#define __SYS_UTILS_H

extern void *GetExportTable(char *libname, int version);
extern u32 GetExportTableSize(void *table);
extern void *GetExportEntry(void *table, u32 entry);
extern void *HookExportEntry(void *table, u32 entry, void *func);

#endif /* __MCEMU_UTILS_H */
