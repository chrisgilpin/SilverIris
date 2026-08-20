#ifndef SILVERIRIS_FILELIST_H
#define SILVERIRIS_FILELIST_H

#include <stddef.h>
#include <stdint.h>

#define PORT_FILELIST_OK 0
#define PORT_FILELIST_ERR_IO -1
#define PORT_FILELIST_ERR_PARSE -6

typedef struct {
    uint32_t offset;
    uint32_t size;
    uint8_t compressed;
    uint8_t extract;
    char name[128];
} PortFilelistEntry;

int port_filelist_parse(const char *csv, size_t len);
int port_filelist_load(const char *path);
const PortFilelistEntry *port_filelist_find(const char *name);
const PortFilelistEntry *port_filelist_find_offset(uint32_t rom_off, uint32_t size);
const PortFilelistEntry *port_filelist_at(size_t i);
size_t port_filelist_count(void);
void port_filelist_clear(void);

#endif
